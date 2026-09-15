/*
 * Acid Cam v2 - Qt/OpenCV Edition
 * Decodes the audio track of a media file (via FFmpeg libav) and plays it
 * through SDL2 audio, in sync with the Lab/Montage video preview.
 */

#ifndef __AUDIO_PLAYER_H__
#define __AUDIO_PLAYER_H__

#include <QThread>
#include <atomic>
#include <string>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
}
#include <SDL.h>

class AudioPlayer : public QThread {
    Q_OBJECT
public:
    explicit AudioPlayer(QObject *parent = nullptr);
    ~AudioPlayer();

    bool open(const std::string &path);
    void closeAudio();
    bool hasAudio() const { return has_audio; }

    void startPlayback();
    void pausePlayback();
    void seekTo(double seconds);
    double position() const;
    void setMuted(bool m) { muted = m; }
    bool isMuted() const { return muted; }

protected:
    void run() override;

private:
    void teardownDecoder();

    AVFormatContext *fmt_ctx;
    AVCodecContext *codec_ctx;
    SwrContext *swr_ctx;
    int stream_index;
    AVRational stream_time_base;
    bool has_audio;

    SDL_AudioDeviceID device;
    int out_sample_rate;
    int out_channels;
    int bytes_per_frame;

    std::atomic<bool> stop_flag;
    std::atomic<bool> playing_flag;
    std::atomic<bool> seek_requested;
    std::atomic<double> seek_seconds;
    std::atomic<long long> samples_queued_total;
    std::atomic<bool> muted;
};

#endif
