#include "audio_player.h"
#include <cstdint>
#include <cstring>

AudioPlayer::AudioPlayer(QObject *parent)
    : QThread(parent), fmt_ctx(nullptr), codec_ctx(nullptr), swr_ctx(nullptr),
      stream_index(-1), has_audio(false), device(0), out_sample_rate(44100),
      out_channels(2), bytes_per_frame(2 * static_cast<int>(sizeof(int16_t))),
      stop_flag(false), playing_flag(false), seek_requested(false),
      seek_seconds(0.0), samples_queued_total(0), muted(false) {
    stream_time_base = AVRational{1, 1};
}

AudioPlayer::~AudioPlayer() {
    closeAudio();
}

void AudioPlayer::teardownDecoder() {
    if(device) {
        SDL_CloseAudioDevice(device);
        device = 0;
    }
    if(swr_ctx) {
        swr_free(&swr_ctx);
        swr_ctx = nullptr;
    }
    if(codec_ctx) {
        avcodec_free_context(&codec_ctx);
        codec_ctx = nullptr;
    }
    if(fmt_ctx) {
        avformat_close_input(&fmt_ctx);
        fmt_ctx = nullptr;
    }
    has_audio = false;
    stream_index = -1;
}

void AudioPlayer::closeAudio() {
    stop_flag = true;
    if(isRunning())
        wait();
    stop_flag = false;
    teardownDecoder();
}

bool AudioPlayer::open(const std::string &path) {
    closeAudio();
    SDL_InitSubSystem(SDL_INIT_AUDIO);

    if(avformat_open_input(&fmt_ctx, path.c_str(), nullptr, nullptr) != 0) {
        fmt_ctx = nullptr;
        return false;
    }
    if(avformat_find_stream_info(fmt_ctx, nullptr) < 0) {
        avformat_close_input(&fmt_ctx);
        return false;
    }

    const AVCodec *decoder = nullptr;
    stream_index = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, &decoder, 0);
    if(stream_index < 0 || decoder == nullptr) {
        avformat_close_input(&fmt_ctx);
        fmt_ctx = nullptr;
        has_audio = false;
        return true;
    }

    AVStream *stream = fmt_ctx->streams[stream_index];
    stream_time_base = stream->time_base;

    codec_ctx = avcodec_alloc_context3(decoder);
    avcodec_parameters_to_context(codec_ctx, stream->codecpar);
    if(avcodec_open2(codec_ctx, decoder, nullptr) < 0) {
        teardownDecoder();
        return true;
    }

    AVChannelLayout out_layout;
    av_channel_layout_default(&out_layout, out_channels);
    swr_ctx = nullptr;
    swr_alloc_set_opts2(&swr_ctx,
        &out_layout, AV_SAMPLE_FMT_S16, out_sample_rate,
        &codec_ctx->ch_layout, codec_ctx->sample_fmt, codec_ctx->sample_rate,
        0, nullptr);
    av_channel_layout_uninit(&out_layout);
    if(!swr_ctx || swr_init(swr_ctx) < 0) {
        teardownDecoder();
        return true;
    }

    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = out_sample_rate;
    want.format = AUDIO_S16SYS;
    want.channels = static_cast<Uint8>(out_channels);
    want.samples = 1024;
    want.callback = nullptr;
    device = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if(device == 0) {
        teardownDecoder();
        return true;
    }

    has_audio = true;
    samples_queued_total = 0;
    stop_flag = false;
    start();
    return true;
}

void AudioPlayer::startPlayback() {
    if(!has_audio) return;
    playing_flag = true;
    SDL_PauseAudioDevice(device, 0);
}

void AudioPlayer::pausePlayback() {
    if(!has_audio) return;
    playing_flag = false;
    SDL_PauseAudioDevice(device, 1);
}

void AudioPlayer::seekTo(double seconds) {
    if(!has_audio) return;
    seek_seconds = seconds;
    seek_requested = true;
}

double AudioPlayer::position() const {
    if(!has_audio) return 0.0;
    Uint32 queued_bytes = SDL_GetQueuedAudioSize(device);
    double queued_seconds = static_cast<double>(queued_bytes) / (out_sample_rate * bytes_per_frame);
    double total_seconds = static_cast<double>(samples_queued_total.load()) / out_sample_rate;
    return total_seconds - queued_seconds;
}

void AudioPlayer::run() {
    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    uint8_t *out_buf = nullptr;
    int out_buf_samples_cap = 0;

    while(!stop_flag) {
        if(!has_audio || !playing_flag) {
            QThread::msleep(15);
            continue;
        }

        if(seek_requested) {
            double target = seek_seconds;
            seek_requested = false;
            int64_t ts = static_cast<int64_t>(target / av_q2d(stream_time_base));
            av_seek_frame(fmt_ctx, stream_index, ts, AVSEEK_FLAG_BACKWARD);
            avcodec_flush_buffers(codec_ctx);
            SDL_ClearQueuedAudio(device);
            samples_queued_total = static_cast<long long>(target * out_sample_rate);
        }

        Uint32 queued_bytes = SDL_GetQueuedAudioSize(device);
        double queued_seconds = static_cast<double>(queued_bytes) / (out_sample_rate * bytes_per_frame);
        if(queued_seconds > 0.5) {
            QThread::msleep(10);
            continue;
        }

        int read_ret = av_read_frame(fmt_ctx, pkt);
        if(read_ret < 0) {
            playing_flag = false;
            av_packet_unref(pkt);
            continue;
        }
        if(pkt->stream_index != stream_index) {
            av_packet_unref(pkt);
            continue;
        }
        if(avcodec_send_packet(codec_ctx, pkt) == 0) {
            while(avcodec_receive_frame(codec_ctx, frame) == 0) {
                int out_samples = static_cast<int>(av_rescale_rnd(
                    swr_get_delay(swr_ctx, codec_ctx->sample_rate) + frame->nb_samples,
                    out_sample_rate, codec_ctx->sample_rate, AV_ROUND_UP));
                if(out_samples > out_buf_samples_cap) {
                    if(out_buf) av_freep(&out_buf);
                    av_samples_alloc(&out_buf, nullptr, out_channels, out_samples, AV_SAMPLE_FMT_S16, 0);
                    out_buf_samples_cap = out_samples;
                }
                int converted = swr_convert(swr_ctx, &out_buf, out_samples,
                    const_cast<const uint8_t **>(frame->data), frame->nb_samples);
                if(converted > 0) {
                    if(muted)
                        memset(out_buf, 0, static_cast<size_t>(converted) * bytes_per_frame);
                    SDL_QueueAudio(device, out_buf, static_cast<Uint32>(converted * bytes_per_frame));
                    samples_queued_total += converted;
                }
                av_frame_unref(frame);
            }
        }
        av_packet_unref(pkt);
    }

    if(out_buf) av_freep(&out_buf);
    av_frame_free(&frame);
    av_packet_free(&pkt);
}
