/*
 * Acid Cam v2 - Qt/OpenCV Edition
 * Background workers that generate a video thumbnail filmstrip and an
 * audio waveform for the Montage V2 timeline, without blocking the UI.
 */

#ifndef __TIMELINE_ANALYSIS_H__
#define __TIMELINE_ANALYSIS_H__

#include "qtheaders.h"
#include <QThread>
#include <QVector>
#include <QImage>

class ThumbnailWorker : public QThread {
    Q_OBJECT
public:
    ThumbnailWorker(const QString &path, int count, QObject *parent = nullptr);

protected:
    void run() override;

signals:
    void thumbnailsReady(QString path, QVector<QImage> thumbs);

private:
    QString path_;
    int count_;
};

class WaveformWorker : public QThread {
    Q_OBJECT
public:
    WaveformWorker(const QString &path, int buckets, QObject *parent = nullptr);

protected:
    void run() override;

signals:
    void waveformReady(QString path, QVector<float> peaks);

private:
    QString path_;
    int buckets_;
};

#endif
