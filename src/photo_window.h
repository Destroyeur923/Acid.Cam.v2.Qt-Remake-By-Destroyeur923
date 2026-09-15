/*
 * Acid Cam v2 - Qt/OpenCV Edition
 * Photo tab: import a still image, stack filters on it (drag a filter onto
 * the photo to apply it for real, click just previews), reorder the stack
 * by dragging within the layer list, and a "Filtre IMG basic" side view
 * that always shows the single filter being previewed in isolation on a
 * pristine copy of the image - never stacked, never committed.
 */

#ifndef __PHOTO_WINDOW_H__
#define __PHOTO_WINDOW_H__

#include "qtheaders.h"
#include "filter_utils.h"
#include "filter_browser_panel.h"
#include <QSet>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>

class AC_MainWindow;

struct PhotoFilterLayer {
    int id;
    QString filterName;
    FilterValue filter;
    // 0-100. The layer's pixels are a blend between the image as it was
    // before this filter ran and the filter's own output, so a heavy filter
    // can be dosed instead of being all-or-nothing. 100 = filter only.
    int intensity = 100;
    // Most libacidcam filters draw their randomness from the C rand(), so
    // re-seeding with this stored value before running the layer makes it
    // reproduce the exact same result on a later rebuild.
    unsigned int seed;
};

// Accepts a filter name dropped from the filter list and turns it into a
// permanent layer (as opposed to a click, which only previews).
class PhotoDropLabel : public QLabel {
    Q_OBJECT
public:
    explicit PhotoDropLabel(QWidget *parent = nullptr);

signals:
    void filterDropped(QString filterName);

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
};

class PhotoWindow : public QDialog {
    Q_OBJECT
public:
    PhotoWindow(AC_MainWindow *parent);
    ~PhotoWindow();

protected:
    void resizeEvent(QResizeEvent *event) override;
    // Holding the before/after key has to work wherever the focus happens to
    // be (layer list, filter list...), and a shortcut gives no release event,
    // so the key is watched application-wide while this window is active.
    bool eventFilter(QObject *watched, QEvent *event) override;

public slots:
    void loadImage();
    void previewFilterSelected(QString realName);
    void clearPreviewFilter();
    void onFilterDropped(QString filterName);
    void onLayersReordered();
    void showLayerContextMenu(const QPoint &pos);
    void toggleBasicPreview(bool checked);
    void basicPreviewFromMenu(bool checked);
    void showHelp();
    void showAbout();
    void exportPhoto();
    void savePhotoMontage();
    void loadPhotoMontage();
    void undoLastAction();
    void onStatusMessage(QString text);
    void refreshLayerList();
    void layerSelectionChanged();
    void intensityChanged(int value);
    void intensityReleased();

private:
    void createControls();
    QMenuBar *createMenuBar();
    QString displayName(const QString &realName) const;
    void renderPhoto();
    void updateLabelPixmaps();
    void rebuildFrom(int start_index);
    void recomputePreviewResult();
    QImage matToImage(const cv::Mat &mat) const;
    void pushUndoState();

    AC_MainWindow *main_window;
    cv::Mat base_image;
    // Cached, baked-in renders - recomputed only when the thing they depend
    // on actually changes, never on every paint. Several filters are
    // deliberately random (a new pattern each time they run); recomputing
    // the whole stack on every unrelated UI action (toggling a checkbox,
    // adding an unrelated layer) would re-roll them for no reason and make
    // an already-applied filter appear to keep changing.
    cv::Mat stacked_result;  // base_image + all applied_layers
    cv::Mat preview_result;  // stacked_result + preview_filter (if active)
    cv::Mat basic_result;    // base_image + preview_filter only (if active)
    // layer_snapshots[i] = the image after layers 0..i. Keeping one render
    // per layer means removing or moving a layer only re-runs the layers
    // ABOVE the change - the ones below keep their exact pixels instead of
    // re-rolling (several filters are random).
    QVector<cv::Mat> layer_snapshots;
    // Full-resolution renders kept around so a window resize only has to
    // rescale them, without re-running any filter.
    QPixmap photo_pixmap;
    QPixmap basic_pixmap;
    QString image_path;
    QVector<PhotoFilterLayer> applied_layers;
    int next_layer_id;
    FilterValue preview_filter;
    QString preview_filter_name;
    unsigned int preview_seed;
    bool preview_active;
    bool rebuilding_layer_list = false;
    QVector<QVector<PhotoFilterLayer>> undo_stack;
    // Last sub-filter chosen, preselected next time.
    int last_subfilter = -1;
    // While held, the main view shows the untouched original.
    bool showing_original = false;
    // Undo is recorded once when the intensity drag starts, not on every
    // pixel the slider moves through.
    bool intensity_undo_pushed = false;

    PhotoDropLabel *photo_label;
    QLabel *basic_preview_label;
    QCheckBox *basic_preview_check;
    QListWidget *layer_list;
    QSlider *intensity_slider;
    QLabel *intensity_label;
    // File-style commands live in the window's own menu bar; only the two
    // that depend on a loaded image are kept as members, to enable them.
    QAction *export_action;
    QAction *save_montage_action;
    QAction *basic_preview_action;
    QLabel *status_label;

    FilterBrowserPanel *filter_browser;
};

#endif
