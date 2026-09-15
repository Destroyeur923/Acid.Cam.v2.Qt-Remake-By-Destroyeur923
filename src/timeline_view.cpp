#include "timeline_view.h"
#include "filter_utils.h"
#include <QFontMetricsF>
#include <algorithm>
#include <cmath>

const double TimelineView::TrackTop = 8.0;
const double TimelineView::VideoTrackHeight = 60.0;
const double TimelineView::AudioTrackHeight = 46.0;
const double TimelineView::TrackGap = 6.0;
const double TimelineView::EffectTrackHeight = 26.0;
const double TimelineView::EdgeGrabZone = 8.0;
const double TimelineView::MinEffectSeconds = 0.3;
const double TimelineView::TargetThumbnailWidth = 70.0;
const int TimelineView::MinThumbnails = 10;
const int TimelineView::MaxThumbnails = 200;

TimelineView::TimelineView(QWidget *parent)
    : QGraphicsView(parent), video_track(nullptr), audio_track(nullptr),
      video_label(nullptr), audio_label(nullptr), playhead(nullptr),
      waveform_item(nullptr), effects(nullptr),
      duration_seconds(0.0), current_seconds(0.0), px_per_sec(60.0), dragging_playhead(false),
      effect_drag_mode(DragNone), drag_effect_index(-1), drag_current_track(0), drag_start_scene_x(0.0) {
    scene_ = new QGraphicsScene(this);
    setScene(scene_);
    setRenderHint(QPainter::Antialiasing, false);
    setBackgroundBrush(QColor(24, 24, 24));
    setMinimumHeight(220);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setDragMode(QGraphicsView::NoDrag);
    setAcceptDrops(true);
}

int TimelineView::maxEffectTrack() const {
    int max_track = 0;
    if(effects)
        for(const auto &e : *effects)
            max_track = std::max(max_track, e.track);
    return max_track;
}

double TimelineView::effectAreaHeight() const {
    return (maxEffectTrack() + 1) * (EffectTrackHeight + 2.0);
}

void TimelineView::rebuildTracks() {
    double width = std::max(1.0, duration_seconds * px_per_sec);
    double video_top = effectAreaTop() + effectAreaHeight() + TrackGap;
    double audio_top = video_top + VideoTrackHeight + TrackGap;
    double bottom = audio_top + AudioTrackHeight + TrackGap;

    if(!video_track) {
        video_track = scene_->addRect(0, video_top, width, VideoTrackHeight,
            QPen(QColor(70, 70, 70)), QBrush(QColor(60, 100, 150)));
        video_label = scene_->addText(tr("Vidéo"));
        video_label->setDefaultTextColor(Qt::white);
        video_label->setZValue(5);
    } else {
        video_track->setRect(0, video_top, width, VideoTrackHeight);
    }

    if(!audio_track) {
        audio_track = scene_->addRect(0, audio_top, width, AudioTrackHeight,
            QPen(QColor(70, 70, 70)), QBrush(QColor(90, 140, 90)));
        audio_label = scene_->addText(tr("Audio"));
        audio_label->setDefaultTextColor(Qt::white);
        audio_label->setZValue(5);
    } else {
        audio_track->setRect(0, audio_top, width, AudioTrackHeight);
    }

    // Reposition the "Vidéo"/"Audio" labels each time (track tops move as
    // effect rows are added/removed).
    video_label->setPos(4, video_top + 2);
    audio_label->setPos(4, audio_top + 2);

    if(!playhead) {
        playhead = scene_->addLine(0, 0, 0, bottom, QPen(Qt::white, 2));
        playhead->setZValue(10);
    } else {
        playhead->setLine(playhead->line().x1(), 0, playhead->line().x1(), bottom);
    }

    scene_->setSceneRect(0, 0, width, bottom);
    layoutThumbnails();
    layoutWaveform();
    layoutEffects();
}

void TimelineView::setVideoThumbnails(const QVector<QImage> &thumbs) {
    for(auto *item : thumbnail_items) {
        scene_->removeItem(item);
        delete item;
    }
    thumbnail_items.clear();
    thumbnails.clear();

    int target_height = static_cast<int>(VideoTrackHeight - 4);
    for(const auto &img : thumbs)
        thumbnails.push_back(QPixmap::fromImage(img).scaledToHeight(target_height, Qt::SmoothTransformation));

    for(const auto &pix : thumbnails) {
        QGraphicsPixmapItem *item = scene_->addPixmap(pix);
        item->setZValue(1);
        thumbnail_items.push_back(item);
    }
    layoutThumbnails();
}

void TimelineView::layoutThumbnails() {
    if(thumbnail_items.isEmpty() || duration_seconds <= 0.0) return;
    double width = duration_seconds * px_per_sec;
    double slot_width = width / thumbnail_items.size();
    double video_top = effectAreaTop() + effectAreaHeight() + TrackGap;
    for(int i = 0; i < thumbnail_items.size(); ++i) {
        QGraphicsPixmapItem *item = thumbnail_items[i];
        double native_width = thumbnails[i].width();
        double scale_x = native_width > 0.0 ? slot_width / native_width : 1.0;
        // Stretch each tile to exactly fill its slot so the filmstrip has no
        // gaps, regardless of the source video's aspect ratio or zoom level.
        item->setTransform(QTransform::fromScale(scale_x, 1.0));
        item->setPos(i * slot_width, video_top + 2);
    }
}

int TimelineView::desiredThumbnailCount() const {
    double width = duration_seconds * px_per_sec;
    int count = static_cast<int>(std::ceil(width / TargetThumbnailWidth));
    return std::clamp(count, MinThumbnails, MaxThumbnails);
}

void TimelineView::setWaveform(const QVector<float> &peaks) {
    waveform_peaks = peaks;
    layoutWaveform();
}

void TimelineView::layoutWaveform() {
    if(!waveform_item) {
        waveform_item = scene_->addPath(QPainterPath(), QPen(Qt::NoPen), QBrush(QColor(200, 235, 200)));
        waveform_item->setZValue(1);
    }
    if(waveform_peaks.isEmpty() || duration_seconds <= 0.0) {
        waveform_item->setPath(QPainterPath());
        return;
    }
    double video_top = effectAreaTop() + effectAreaHeight() + TrackGap;
    double audio_top = video_top + VideoTrackHeight + TrackGap;
    double mid = audio_top + AudioTrackHeight / 2.0;
    double half_height = AudioTrackHeight / 2.0 - 3.0;
    double width = duration_seconds * px_per_sec;
    int n = waveform_peaks.size();
    double step = width / n;

    QPainterPath path;
    path.moveTo(0, mid);
    for(int i = 0; i < n; ++i) {
        double x = i * step;
        double y = mid - waveform_peaks[i] * half_height;
        path.lineTo(x, y);
    }
    for(int i = n - 1; i >= 0; --i) {
        double x = i * step;
        double y = mid + waveform_peaks[i] * half_height;
        path.lineTo(x, y);
    }
    path.closeSubpath();
    waveform_item->setPath(path);
}

void TimelineView::setEffects(const QVector<TimelineEffectClip> *effs) {
    effects = effs;
    rebuildTracks();
}

void TimelineView::layoutEffects() {
    for(auto *item : effect_items) {
        scene_->removeItem(item);
        delete item;
    }
    effect_items.clear();

    if(!effects || duration_seconds <= 0.0) return;

    for(int i = 0; i < effects->size(); ++i) {
        const TimelineEffectClip &clip = (*effects)[i];
        double x = clip.start * px_per_sec;
        double w = std::max(2.0, (clip.end - clip.start) * px_per_sec);
        double y = effectAreaTop() + clip.track * (EffectTrackHeight + 2.0);
        // The clip wears the colour of wherever the filter was dragged from:
        // its collection, the favorites' red, or the neutral brown.
        const QColor fill = clip.color.isValid() ? clip.color : filterNeutralColor();
        QGraphicsRectItem *rect = scene_->addRect(x, y, w, EffectTrackHeight,
            QPen(QColor(20, 20, 20)), QBrush(fill));
        rect->setZValue(6);
        QString shown = clip.label.isEmpty() ? clip.filterName : clip.label;
        // Only flagged when dosed, so a normal clip keeps a clean label.
        if(clip.intensity < 100)
            shown += QStringLiteral("  (%1 %)").arg(clip.intensity);
        rect->setToolTip(shown);

        QGraphicsSimpleTextItem *label = new QGraphicsSimpleTextItem(rect);
        // Dark text on a pale collection colour, white on a dark one.
        label->setBrush(fill.lightness() > 150 ? QColor(20, 20, 20) : QColor(Qt::white));
        QFont f = label->font();
        f.setPointSize(8);
        label->setFont(f);
        // Names longer than the block get an ellipsis instead of spilling
        // over the neighbouring clips; the full one stays in the tooltip.
        const double text_width = w - 6.0;
        label->setText(text_width > 8.0
            ? QFontMetricsF(f).elidedText(shown, Qt::ElideRight, text_width)
            : QString());
        // The rect item itself sits at the scene origin and carries its
        // geometry in its rect(), so a child has to be placed at the block's
        // absolute position - not at (3, 4), which is the top-left corner of
        // the whole timeline.
        label->setPos(x + 3.0, y + (EffectTrackHeight - QFontMetricsF(f).height()) / 2.0);

        effect_items.push_back(rect);
    }
}

void TimelineView::setDuration(double seconds) {
    duration_seconds = std::max(0.0, seconds);
    current_seconds = 0.0;
    setVideoThumbnails(QVector<QImage>());
    setWaveform(QVector<float>());
    rebuildTracks();
    if(playhead)
        playhead->setLine(0, playhead->line().y1(), 0, playhead->line().y2());
}

void TimelineView::setPosition(double seconds) {
    current_seconds = std::clamp(seconds, 0.0, duration_seconds);
    if(playhead) {
        double x = current_seconds * px_per_sec;
        playhead->setLine(x, playhead->line().y1(), x, playhead->line().y2());
        // Keep the playhead visible while playing.
        if(!dragging_playhead)
            ensureVisible(x, 0, 1, 1, 40, 0);
    }
}

void TimelineView::setZoom(double pixelsPerSecond) {
    px_per_sec = std::max(2.0, pixelsPerSecond);
    rebuildTracks();
    setPosition(current_seconds);
    emit zoomChanged(px_per_sec);
}

void TimelineView::handleSeek(const QPoint &viewportPos) {
    if(duration_seconds <= 0.0) return;
    QPointF scenePos = mapToScene(viewportPos);
    double seconds = std::clamp(scenePos.x() / px_per_sec, 0.0, duration_seconds);
    setPosition(seconds);
    emit seekRequested(seconds);
}

void TimelineView::mousePressEvent(QMouseEvent *event) {
    QPointF scenePos = mapToScene(event->pos());

    if(event->button() == Qt::RightButton) {
        for(int i = 0; i < effect_items.size(); ++i) {
            if(effect_items[i]->rect().contains(scenePos)) {
                emit effectMenuRequested(i);
                return;
            }
        }
        return;
    }

    if(event->button() == Qt::LeftButton) {
        for(int i = 0; i < effect_items.size(); ++i) {
            QRectF r = effect_items[i]->rect();
            if(!r.contains(scenePos)) continue;
            drag_effect_index = i;
            drag_current_track = effects ? (*effects)[i].track : 0;
            drag_start_scene_x = scenePos.x();
            drag_orig_rect = r;
            if(scenePos.x() - r.left() < EdgeGrabZone)
                effect_drag_mode = DragResizeLeft;
            else if(r.right() - scenePos.x() < EdgeGrabZone)
                effect_drag_mode = DragResizeRight;
            else
                effect_drag_mode = DragMove;
            return;
        }
        dragging_playhead = true;
        handleSeek(event->pos());
        return;
    }
    QGraphicsView::mousePressEvent(event);
}

void TimelineView::mouseMoveEvent(QMouseEvent *event) {
    QPointF scenePos = mapToScene(event->pos());

    if(effect_drag_mode != DragNone) {
        double delta_x = scenePos.x() - drag_start_scene_x;
        QRectF r = drag_orig_rect;
        double min_width = MinEffectSeconds * px_per_sec;

        // Clamp against the nearest neighbor on the current track so blocks
        // can't be dragged/resized on top of each other.
        double left_bound = 0.0;
        double right_bound = duration_seconds * px_per_sec;
        if(effects) {
            for(int i = 0; i < effects->size(); ++i) {
                if(i == drag_effect_index || (*effects)[i].track != drag_current_track) continue;
                double ox1 = (*effects)[i].start * px_per_sec;
                double ox2 = (*effects)[i].end * px_per_sec;
                if(ox2 <= drag_orig_rect.left() + 0.5) left_bound = std::max(left_bound, ox2);
                if(ox1 >= drag_orig_rect.right() - 0.5) right_bound = std::min(right_bound, ox1);
            }
        }

        if(effect_drag_mode == DragResizeLeft) {
            r.setLeft(std::clamp(r.left() + delta_x, left_bound, r.right() - min_width));
        } else if(effect_drag_mode == DragResizeRight) {
            r.setRight(std::clamp(r.right() + delta_x, r.left() + min_width, right_bound));
        } else if(effect_drag_mode == DragMove) {
            double w = r.width();
            double new_left = std::clamp(r.left() + delta_x, left_bound, right_bound - w);
            r.moveLeft(new_left);

            // Dragging up/down onto a different row reassigns the track,
            // but only if the block's current horizontal span is free there
            // - multiple tracks can overlap in time, just not on one row.
            int candidate = static_cast<int>(std::round((scenePos.y() - effectAreaTop()) / (EffectTrackHeight + 2.0)));
            candidate = std::clamp(candidate, 0, maxEffectTrack() + 1);
            if(candidate != drag_current_track && effects) {
                bool overlap = false;
                for(int i = 0; i < effects->size(); ++i) {
                    if(i == drag_effect_index || (*effects)[i].track != candidate) continue;
                    if(new_left < (*effects)[i].end * px_per_sec && new_left + w > (*effects)[i].start * px_per_sec) {
                        overlap = true;
                        break;
                    }
                }
                if(!overlap)
                    drag_current_track = candidate;
            }
            r.moveTop(effectAreaTop() + drag_current_track * (EffectTrackHeight + 2.0));
        }
        effect_items[drag_effect_index]->setRect(r);
        return;
    }

    if(dragging_playhead && (event->buttons() & Qt::LeftButton)) {
        handleSeek(event->pos());
        return;
    }

    // Hover feedback: show a resize cursor near a block's edges.
    bool over_edge = false;
    for(auto *item : effect_items) {
        QRectF r = item->rect();
        if(scenePos.y() < r.top() || scenePos.y() > r.bottom()) continue;
        if(std::abs(scenePos.x() - r.left()) < EdgeGrabZone || std::abs(scenePos.x() - r.right()) < EdgeGrabZone) {
            over_edge = true;
            break;
        }
    }
    setCursor(over_edge ? Qt::SizeHorCursor : Qt::ArrowCursor);

    QGraphicsView::mouseMoveEvent(event);
}

void TimelineView::mouseReleaseEvent(QMouseEvent *event) {
    if(effect_drag_mode != DragNone) {
        QRectF r = effect_items[drag_effect_index]->rect();
        int index = drag_effect_index;
        double new_start = r.left() / px_per_sec;
        double new_end = r.right() / px_per_sec;
        int new_track = drag_current_track;
        effect_drag_mode = DragNone;
        drag_effect_index = -1;
        emit effectEdited(index, new_start, new_end, new_track);
        return;
    }
    dragging_playhead = false;
    QGraphicsView::mouseReleaseEvent(event);
}

void TimelineView::wheelEvent(QWheelEvent *event) {
    if(event->modifiers() & Qt::ControlModifier) {
        double factor = event->angleDelta().y() > 0 ? 1.15 : 1.0 / 1.15;
        setZoom(px_per_sec * factor);
        event->accept();
        return;
    }
    QGraphicsView::wheelEvent(event);
}

void TimelineView::dragEnterEvent(QDragEnterEvent *event) {
    if(event->mimeData()->hasText() && duration_seconds > 0.0)
        event->acceptProposedAction();
}

void TimelineView::dragMoveEvent(QDragMoveEvent *event) {
    if(event->mimeData()->hasText() && duration_seconds > 0.0)
        event->acceptProposedAction();
}

void TimelineView::dropEvent(QDropEvent *event) {
    if(!event->mimeData()->hasText() || duration_seconds <= 0.0) return;
    QString filterName = event->mimeData()->text();
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QPointF viewportPos = event->position();
#else
    QPointF viewportPos = event->pos();
#endif
    QPointF scenePos = mapToScene(viewportPos.toPoint());
    double seconds = std::clamp(scenePos.x() / px_per_sec, 0.0, duration_seconds);
    // The source list tags the drag with the colour of the tab it came from;
    // a drag from somewhere else falls back to the neutral brown.
    QColor color(QString::fromUtf8(event->mimeData()->data(FilterColorMimeType)));
    if(!color.isValid())
        color = filterNeutralColor();
    emit effectDropped(filterName, seconds, color);
    event->acceptProposedAction();
}
