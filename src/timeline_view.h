/*
 * Acid Cam v2 - Qt/OpenCV Edition
 * Zoomable timeline widget for Montage V2: separate video/audio lanes,
 * a draggable playhead, and effect tracks above the video with drag-and-drop
 * placement. Resizing a block and multi-track composition come later.
 */

#ifndef __TIMELINE_VIEW_H__
#define __TIMELINE_VIEW_H__

#include "qtheaders.h"
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsRectItem>
#include <QGraphicsLineItem>
#include <QGraphicsPixmapItem>
#include <QGraphicsPathItem>
#include <QGraphicsSimpleTextItem>
#include <QWheelEvent>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>

// One filter placed on an effect track between [start, end) seconds.
struct TimelineEffectClip {
    double start;
    double end;
    QString filterName;   // real libacidcam name - what actually gets applied
    QString label;        // what the user sees (their rename, if any)
    QColor color;         // colour of the tab the filter was dragged from
    FilterValue filter;
    int track;
    // 0-100: cross-fade between the frame before this effect and its output,
    // so a heavy filter can be dosed instead of being all-or-nothing.
    int intensity = 100;
};

class TimelineView : public QGraphicsView {
    Q_OBJECT
public:
    explicit TimelineView(QWidget *parent = nullptr);

    void setDuration(double seconds);
    void setPosition(double seconds);
    void setZoom(double pixelsPerSecond);
    double zoom() const { return px_per_sec; }
    void setVideoThumbnails(const QVector<QImage> &thumbs);
    void setWaveform(const QVector<float> &peaks);
    int desiredThumbnailCount() const;
    int currentThumbnailCount() const { return thumbnails.size(); }
    void setEffects(const QVector<TimelineEffectClip> *effects);

signals:
    void seekRequested(double seconds);
    void zoomChanged(double pixelsPerSecond);
    void effectDropped(QString filterName, double seconds, QColor color);
    // Right-click on a clip: the host opens its own menu (delete, intensity).
    void effectMenuRequested(int index);
    void effectEdited(int index, double newStart, double newEnd, int newTrack);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    void handleSeek(const QPoint &viewportPos);
    void rebuildTracks();
    void layoutThumbnails();
    void layoutWaveform();
    void layoutEffects();
    double effectAreaTop() const { return TrackTop; }
    double effectAreaHeight() const;
    int maxEffectTrack() const;

    QGraphicsScene *scene_;
    QGraphicsRectItem *video_track;
    QGraphicsRectItem *audio_track;
    QGraphicsTextItem *video_label;
    QGraphicsTextItem *audio_label;
    QGraphicsLineItem *playhead;
    QVector<QGraphicsPixmapItem *> thumbnail_items;
    QVector<QPixmap> thumbnails;
    QGraphicsPathItem *waveform_item;
    QVector<float> waveform_peaks;
    const QVector<TimelineEffectClip> *effects;
    QVector<QGraphicsRectItem *> effect_items;

    double duration_seconds;
    double current_seconds;
    double px_per_sec;
    bool dragging_playhead;

    enum EffectDragMode { DragNone, DragMove, DragResizeLeft, DragResizeRight };
    EffectDragMode effect_drag_mode;
    int drag_effect_index;
    int drag_current_track;
    double drag_start_scene_x;
    QRectF drag_orig_rect;

    static const double TrackTop;
    static const double VideoTrackHeight;
    static const double AudioTrackHeight;
    static const double TrackGap;
    static const double EffectTrackHeight;
    static const double EdgeGrabZone;
    static const double MinEffectSeconds;
    static const double TargetThumbnailWidth;
    static const int MinThumbnails;
    static const int MaxThumbnails;
};

#endif
