#include "timeline_analysis.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
}
#include <cstdint>
#include <cmath>

ThumbnailWorker::ThumbnailWorker(const QString &path, int count, QObject *parent)
    : QThread(parent), path_(path), count_(count) {}

void ThumbnailWorker::run() {
    QVector<QImage> thumbs;
    cv::VideoCapture cap(path_.toStdString());
    if(cap.isOpened()) {
        long total = static_cast<long>(cap.get(cv::CAP_PROP_FRAME_COUNT));
        for(int i = 0; i < count_ && total > 0; ++i) {
            long frame_idx = static_cast<long>((static_cast<double>(i) / count_) * total);
            cap.set(cv::CAP_PROP_POS_FRAMES, static_cast<double>(frame_idx));
            cv::Mat frame;
            if(!cap.read(frame) || frame.empty())
                continue;
            cv::Mat rgb;
            cv::cvtColor(frame, rgb, cv::COLOR_BGR2RGB);
            QImage img(rgb.data, rgb.cols, rgb.rows, static_cast<int>(rgb.step), QImage::Format_RGB888);
            thumbs.push_back(img.copy());
        }
    }
    emit thumbnailsReady(path_, thumbs);
}

WaveformWorker::WaveformWorker(const QString &path, int buckets, QObject *parent)
    : QThread(parent), path_(path), buckets_(buckets) {}

void WaveformWorker::run() {
    QVector<float> peaks(buckets_, 0.0f);

    AVFormatContext *fmt_ctx = nullptr;
    if(avformat_open_input(&fmt_ctx, path_.toStdString().c_str(), nullptr, nullptr) != 0) {
        emit waveformReady(path_, peaks);
        return;
    }
    if(avformat_find_stream_info(fmt_ctx, nullptr) < 0) {
        avformat_close_input(&fmt_ctx);
        emit waveformReady(path_, peaks);
        return;
    }

    const AVCodec *decoder = nullptr;
    int stream_index = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, &decoder, 0);
    if(stream_index < 0 || decoder == nullptr) {
        avformat_close_input(&fmt_ctx);
        emit waveformReady(path_, peaks);
        return;
    }

    AVStream *stream = fmt_ctx->streams[stream_index];
    AVCodecContext *codec_ctx = avcodec_alloc_context3(decoder);
    avcodec_parameters_to_context(codec_ctx, stream->codecpar);
    if(avcodec_open2(codec_ctx, decoder, nullptr) < 0) {
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&fmt_ctx);
        emit waveformReady(path_, peaks);
        return;
    }

    const int out_rate = 44100;
    const int out_channels = 1;
    AVChannelLayout out_layout;
    av_channel_layout_default(&out_layout, out_channels);
    SwrContext *swr_ctx = nullptr;
    swr_alloc_set_opts2(&swr_ctx,
        &out_layout, AV_SAMPLE_FMT_S16, out_rate,
        &codec_ctx->ch_layout, codec_ctx->sample_fmt, codec_ctx->sample_rate,
        0, nullptr);
    av_channel_layout_uninit(&out_layout);

    double duration = fmt_ctx->duration > 0
        ? static_cast<double>(fmt_ctx->duration) / AV_TIME_BASE
        : (stream->duration > 0 ? stream->duration * av_q2d(stream->time_base) : 0.0);

    if(swr_ctx && swr_init(swr_ctx) >= 0 && duration > 0.0) {
        AVPacket *pkt = av_packet_alloc();
        AVFrame *frame = av_frame_alloc();
        uint8_t *out_buf = nullptr;
        int out_buf_cap = 0;
        long long samples_emitted = 0;

        while(av_read_frame(fmt_ctx, pkt) >= 0) {
            if(pkt->stream_index == stream_index && avcodec_send_packet(codec_ctx, pkt) == 0) {
                while(avcodec_receive_frame(codec_ctx, frame) == 0) {
                    int out_samples = static_cast<int>(av_rescale_rnd(
                        swr_get_delay(swr_ctx, codec_ctx->sample_rate) + frame->nb_samples,
                        out_rate, codec_ctx->sample_rate, AV_ROUND_UP));
                    if(out_samples > out_buf_cap) {
                        if(out_buf) av_freep(&out_buf);
                        av_samples_alloc(&out_buf, nullptr, out_channels, out_samples, AV_SAMPLE_FMT_S16, 0);
                        out_buf_cap = out_samples;
                    }
                    int converted = swr_convert(swr_ctx, &out_buf, out_samples,
                        const_cast<const uint8_t **>(frame->data), frame->nb_samples);
                    if(converted > 0) {
                        const int16_t *samples = reinterpret_cast<const int16_t *>(out_buf);
                        for(int s = 0; s < converted; ++s) {
                            double t = static_cast<double>(samples_emitted + s) / out_rate;
                            int bucket = static_cast<int>((t / duration) * buckets_);
                            if(bucket < 0) bucket = 0;
                            if(bucket >= buckets_) bucket = buckets_ - 1;
                            float amp = std::abs(static_cast<float>(samples[s])) / 32768.0f;
                            if(amp > peaks[bucket])
                                peaks[bucket] = amp;
                        }
                        samples_emitted += converted;
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

    if(swr_ctx) swr_free(&swr_ctx);
    avcodec_free_context(&codec_ctx);
    avformat_close_input(&fmt_ctx);

    emit waveformReady(path_, peaks);
}
