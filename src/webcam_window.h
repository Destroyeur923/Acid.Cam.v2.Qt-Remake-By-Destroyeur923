/*
 * Acid Cam v2 - Qt/OpenCV Edition
 * WebCam: apply filters to a live camera.
 *
 * Two live views side by side, the same idea as the Photo tab but moving:
 * the left one shows the camera with the filters that were actually dropped
 * on it (and it is what gets recorded), the right one shows that same image
 * plus the filter currently selected in the list - a live look at what the
 * filter would do before committing to it.
 *
 * Each dropped filter gets its OWN intensity slider in the stack list, so a
 * filter can be dosed on the spot during a live session without going
 * through a menu or a dialog.
 */

#ifndef __WEBCAM_WINDOW_H__
#define __WEBCAM_WINDOW_H__

#include "qtheaders.h"
#include "filter_utils.h"
#include "filter_browser_panel.h"
#include "camera_enum.h"
#include <QVector>
#include <QScrollArea>
#include <QElapsedTimer>

namespace mx { class Writer; }

class AC_MainWindow;

// One row of the applied-filters stack: name, its own intensity slider, and
// a button to drop it.
class WebcamLayerRow : public QWidget {
    Q_OBJECT
public:
    WebcamLayerRow(int id, const QString &label, int intensity, QWidget *parent = nullptr);
    void setLabel(const QString &label);

signals:
    void intensityChanged(int id, int value);
    void removeRequested(int id);

private slots:
    void onSliderMoved(int value);

private:
    int layer_id;
    QLabel *name_label;
    QSlider *slider;
    QLabel *value_label;
};

// Modal picker listing the machine's cameras.
class CameraPickerDialog : public QDialog {
    Q_OBJECT
public:
    explicit CameraPickerDialog(QWidget *parent = nullptr);
    // -1 when nothing was chosen.
    int selectedIndex() const;
    QString selectedName() const;

private slots:
    void refresh();

private:
    QListWidget *list;
    QLabel *info;
    QPushButton *ok_button;
};

struct WebcamFilterLayer {
    int id;
    QString filterName;
    FilterValue filter;
    int intensity = 100;
};

class WebcamWindow : public QDialog {
    Q_OBJECT
public:
    explicit WebcamWindow(AC_MainWindow *parent);
    ~WebcamWindow();
    void stopCamera();

protected:
    void closeEvent(QCloseEvent *event) override;

public slots:
    void chooseCamera();
    void grabFrame();
    void previewFilterSelected(QString realName);
    void clearPreviewFilter();
    void onFilterDropped(QString filterName);
    void toggleRecording();
    void undoLastAction();
    void clearAllLayers();
    void onStatusMessage(QString text);
    void refreshLayerLabels();
    void onLayerIntensityChanged(int id, int value);
    void onLayerRemoveRequested(int id);
    void toggleMirror(bool checked);
    void showHelp();
    void showAbout();

private:
    void createControls();
    QMenuBar *createMenuBar();
    void rebuildLayerRows();
    void pushUndoState();
    void applyLayer(cv::Mat &frame, const FilterValue &fv, int intensity) const;
    QString layerLabel(const WebcamFilterLayer &layer) const;
    bool startRecording();
    void stopRecording();
    QImage matToImage(const cv::Mat &mat) const;

    AC_MainWindow *main_window;
    cv::VideoCapture capture;
    QTimer *frame_timer;
    int camera_index = -1;
    QString camera_name;
    double camera_fps = 30.0;

    QVector<WebcamFilterLayer> applied_layers;
    int next_layer_id = 0;
    QVector<QVector<WebcamFilterLayer>> undo_stack;

    FilterValue preview_filter;
    QString preview_filter_name;
    bool preview_active = false;
    int last_subfilter = -1;
    // A raw camera feed is not mirrored: raising your right hand shows it on
    // the left of the picture, which reads as the wrong hand. Flipping makes
    // the view behave like a mirror. Applied to the frame before the filters,
    // so the two views and the recording all agree.
    bool mirrored = false;

    // Recording writes the left view - the one with the filters actually
    // applied - and runs the encoder in realtime mode so a slow machine
    // drops frames instead of stalling the camera.
    mx::Writer *recorder = nullptr;
    bool recording = false;
    QString recording_path;
    cv::Size recording_size;
    // An MP4 carries one fixed frame rate, but the real capture rate falls as
    // soon as filters cost time - and it changes again every time one is added
    // or removed. Writes are therefore paced against this clock rather than
    // being one-per-grab, so the file's duration always matches real time.
    double recording_fps = 30.0;
    QElapsedTimer record_clock;
    qint64 frames_written = 0;

    FilterDropLabel *camera_view;
    QLabel *preview_view;
    QCheckBox *preview_check;
    QScrollArea *layer_area;
    QWidget *layer_container;
    QVBoxLayout *layer_layout;
    QLabel *empty_layers_label;
    QPushButton *record_button;
    QLabel *camera_label;
    QLabel *status_label;
    QAction *record_action;
    QAction *mirror_action;
    FilterBrowserPanel *filter_browser;
};

#endif
