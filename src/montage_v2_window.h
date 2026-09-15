/*
 * Acid Cam v2 - Qt/OpenCV Edition
 * Montage V2 - Stage 1: zoomable timeline with a draggable playhead,
 * separate video/audio lanes. Effect tracks come in later stages.
 */

#ifndef __MONTAGE_V2_WINDOW_H__
#define __MONTAGE_V2_WINDOW_H__

#include "qtheaders.h"
#include "audio_player.h"
#include "timeline_view.h"
#include "timeline_analysis.h"
#include "filter_utils.h"
#include "filter_browser_panel.h"
#include <QElapsedTimer>
#include <QPointer>
#include <QSet>

class AC_MainWindow;

class MontageV2Window : public QDialog {
    Q_OBJECT
public:
    MontageV2Window(AC_MainWindow *parent);
    ~MontageV2Window();

protected:
    void closeEvent(QCloseEvent *event) override;

public slots:
    void loadMedia();
    void seekFromTimeline(double seconds);
    void togglePlay();
    void toggleMute(bool checked);
    void muteFromMenu(bool checked);
    void zoomSliderChanged(int value);
    void playbackTick();
    void onThumbnailsReady(QString path, QVector<QImage> thumbs);
    void onWaveformReady(QString path, QVector<float> peaks);
    void onZoomChanged(double pixelsPerSecond);
    void regenerateThumbnails();
    void onEffectDropped(QString filterName, double seconds, QColor color);
    void onEffectMenuRequested(int index);
    void onEffectDeleteRequested(int index);
    void editEffectIntensity(int index);
    void onEffectEdited(int index, double newStart, double newEnd, int newTrack);
    void exportMontage();
    void saveMontage();
    void loadMontage();
    void previewFilterSelected(QString realName);
    void clearPreviewFilter();
    void undoLastAction();
    void onStatusMessage(QString text);
    void refreshEffectLabels();
    void showHelp();
    void showAbout();

private:
    void createControls();
    QMenuBar *createMenuBar();
    bool openMedia(const QString &path);
    void showFrame(long index);
    void startThumbnailWorker(int count);
    // -1 for a normal filter; for a sub-filter host, the second filter to
    // combine with (reusing the preview's choice, else asking).
    int resolveSubFilter(const QString &realName);
    void applyEffectsAt(cv::Mat &frame, long frameIndex) const;
    void applyPreviewFilter(cv::Mat &frame) const;
    void pushUndoState();

    AC_MainWindow *main_window;
    cv::VideoCapture capture;
    AudioPlayer *audio;
    QString media_path;
    long total_frames;
    double fps;
    long current_frame;
    QElapsedTimer playback_clock;
    double playback_clock_offset;
    QTimer *play_timer;
    bool playing;
    QPointer<ThumbnailWorker> thumbnail_worker;
    QPointer<WaveformWorker> waveform_worker;
    QTimer *thumbnail_regen_timer;
    QVector<TimelineEffectClip> effects;
    FilterValue preview_filter;
    QString preview_filter_name;
    bool preview_active;
    int last_subfilter = -1;
    QVector<QVector<TimelineEffectClip>> undo_stack;

    QLabel *preview;
    // File-style commands live in the window's own menu bar; only the two
    // that depend on a loaded video are kept as members, to enable them.
    QAction *export_action;
    QAction *save_montage_action;
    QAction *mute_action;
    QLabel *media_label;
    QLabel *time_label;
    QPushButton *play_button;
    QCheckBox *mute_check;
    QSlider *zoom_slider;
    TimelineView *timeline;
    QLabel *status_label;
    FilterBrowserPanel *filter_browser;
    QPushButton *clear_preview_button;
};

#endif
