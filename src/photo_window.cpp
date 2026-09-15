#include "photo_window.h"
#include "main_window.h"
#include "tokenize.h"
#include <algorithm>
#include <QSettings>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QTabWidget>
#include <QTabBar>
#include <QMenu>
#include <QInputDialog>
#include <QColorDialog>
#include <QKeySequence>
#include <QRandomGenerator>
#include <QResizeEvent>
#include <QKeyEvent>
#include <QMenuBar>
#include "help_about.h"
#include <cstdlib>

namespace {
const int MaxUndoSteps = 50;

// Many libacidcam filters are stateful - they keep previously seen frames in
// globals and blend/xor against them. On a still image that state must be
// wiped before every single filter call, otherwise the same filter gives a
// different result depending on what ran before it: previewing a filter on a
// fresh state and then committing it at the end of a chain (with the previous
// layers' leftovers in the buffers) would not match.
// `seed` re-seeds the C rand() that the vast majority of libacidcam filters
// use, so re-running the same filter on the same input reproduces the same
// output. (A handful of filters hold their own static RNG or re-seed
// themselves from the clock - those stay non-reproducible, which is why the
// per-layer snapshots below matter too.)
void applyFilterToMat(cv::Mat &frame, const FilterValue &fv, unsigned int seed) {
    if(fv.filter < 0 || fv.filter >= static_cast<int>(ac::draw_strings.size()))
        return;
    ac::release_all_objects();
    srand(seed);
    ac::setSubFilter(fv.subfilter);
    ac::CallFilter(ac::draw_strings[fv.filter], frame);
    ac::setSubFilter(-1);
}

// Dose a filter: cross-fade between the image as it was before it ran and
// what it produced. 100 keeps the filter alone, 0 discards it entirely.
void blendIntensity(const cv::Mat &before, cv::Mat &after, int intensity) {
    if(intensity >= 100 || before.empty() || after.empty())
        return;
    if(before.size() != after.size() || before.type() != after.type())
        return;  // a filter that changed the geometry cannot be cross-faded
    if(intensity <= 0) {
        after = before.clone();
        return;
    }
    const double x = intensity / 100.0;
    cv::addWeighted(before, 1.0 - x, after, x, 0.0, after);
}

// cv::imread/imwrite take a std::string and hand it to fopen(), which on
// Windows reads it in the system's ANSI codepage - not UTF-8. Any accented
// or non-ASCII character in the path (common on this machine: "météo",
// "créat...", etc.) makes OpenCV silently fail to find/write the file, which
// looks exactly like "this format isn't supported" even for a plain PNG/JPG.
// Routing the bytes through Qt's own (Unicode-correct) file I/O and letting
// OpenCV decode/encode an in-memory buffer sidesteps the path entirely.
cv::Mat readImageUnicodeSafe(const QString &path) {
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly))
        return cv::Mat();
    QByteArray data = file.readAll();
    file.close();
    if(data.isEmpty())
        return cv::Mat();
    std::vector<uchar> buf(data.begin(), data.end());
    return cv::imdecode(buf, cv::IMREAD_COLOR);
}

bool writeImageUnicodeSafe(const QString &path, const cv::Mat &frame) {
    QString ext = QFileInfo(path).suffix().toLower();
    std::string extWithDot = "." + ext.toStdString();
    std::vector<int> params;
    if(ext == "jpg" || ext == "jpeg")
        params = {cv::IMWRITE_JPEG_QUALITY, 95};
    else if(ext == "png")
        params = {cv::IMWRITE_PNG_COMPRESSION, 3};

    std::vector<uchar> buf;
    if(!cv::imencode(extWithDot, frame, buf, params))
        return false;

    QFile file(path);
    if(!file.open(QIODevice::WriteOnly))
        return false;
    qint64 written = file.write(reinterpret_cast<const char *>(buf.data()), static_cast<qint64>(buf.size()));
    file.close();
    return written == static_cast<qint64>(buf.size());
}
}

PhotoDropLabel::PhotoDropLabel(QWidget *parent) : QLabel(parent) {
    setAcceptDrops(true);
}

void PhotoDropLabel::dragEnterEvent(QDragEnterEvent *event) {
    if(event->mimeData()->hasText())
        event->acceptProposedAction();
}

void PhotoDropLabel::dragMoveEvent(QDragMoveEvent *event) {
    if(event->mimeData()->hasText())
        event->acceptProposedAction();
}

void PhotoDropLabel::dropEvent(QDropEvent *event) {
    if(event->mimeData()->hasText()) {
        emit filterDropped(event->mimeData()->text());
        event->acceptProposedAction();
    }
}

PhotoWindow::PhotoWindow(AC_MainWindow *parent)
    : QDialog(parent), main_window(parent), next_layer_id(0), preview_seed(0), preview_active(false) {
    setWindowTitle(tr("Photo - Filtres et superposition"));
    setWindowIcon(QPixmap(":/images/icon.png"));
    resize(1150, 780);
    createControls();

    // Ctrl+Z is owned by the Édition menu's action, which already has window
    // scope. A second QShortcut on the same key would make Qt treat both as
    // ambiguous and fire neither.
    qApp->installEventFilter(this);
}

PhotoWindow::~PhotoWindow() {
    if(qApp)
        qApp->removeEventFilter(this);
}

bool PhotoWindow::eventFilter(QObject *watched, QEvent *event) {
    if((event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease)
       && isActiveWindow() && !base_image.empty()) {
        QKeyEvent *key = static_cast<QKeyEvent *>(event);
        // Space would otherwise be typed into the filter search box.
        const bool typing = qobject_cast<QLineEdit *>(QApplication::focusWidget()) != nullptr;
        if(key->key() == Qt::Key_Space && !key->isAutoRepeat() && !typing) {
            const bool down = (event->type() == QEvent::KeyPress);
            if(showing_original != down) {
                showing_original = down;
                renderPhoto();
                status_label->setText(down
                    ? tr("Image d'origine (relâche Espace pour revenir au résultat).")
                    : tr("Résultat avec les filtres."));
            }
            return true;
        }
    }
    return QDialog::eventFilter(watched, event);
}

QMenuBar *PhotoWindow::createMenuBar() {
    QMenuBar *bar = new QMenuBar(this);

    QMenu *file_menu = bar->addMenu(tr("&Fichier"));
    QAction *load_image_action = file_menu->addAction(tr("Charger une image..."));
    load_image_action->setShortcut(QKeySequence::Open);
    connect(load_image_action, SIGNAL(triggered()), this, SLOT(loadImage()));
    file_menu->addSeparator();
    QAction *load_montage_action = file_menu->addAction(tr("Charger un montage..."));
    connect(load_montage_action, SIGNAL(triggered()), this, SLOT(loadPhotoMontage()));
    save_montage_action = file_menu->addAction(tr("Enregistrer le montage..."));
    save_montage_action->setShortcut(QKeySequence::Save);
    save_montage_action->setEnabled(false);
    connect(save_montage_action, SIGNAL(triggered()), this, SLOT(savePhotoMontage()));
    file_menu->addSeparator();
    export_action = file_menu->addAction(tr("Exporter la photo..."));
    export_action->setShortcut(QKeySequence(tr("Ctrl+E")));
    export_action->setEnabled(false);
    connect(export_action, SIGNAL(triggered()), this, SLOT(exportPhoto()));
    file_menu->addSeparator();
    QAction *close_action = file_menu->addAction(tr("Fermer"));
    close_action->setShortcut(QKeySequence::Close);
    connect(close_action, SIGNAL(triggered()), this, SLOT(close()));

    QMenu *edit_menu = bar->addMenu(tr("&Édition"));
    QAction *undo_action = edit_menu->addAction(tr("Annuler"));
    undo_action->setShortcut(QKeySequence::Undo);
    connect(undo_action, SIGNAL(triggered()), this, SLOT(undoLastAction()));
    edit_menu->addSeparator();
    QAction *clear_preview_action = edit_menu->addAction(tr("Retirer le filtre d'aperçu"));
    connect(clear_preview_action, SIGNAL(triggered()), this, SLOT(clearPreviewFilter()));

    QMenu *view_menu = bar->addMenu(tr("&Affichage"));
    basic_preview_action = view_menu->addAction(tr("Filtre IMG basic"));
    basic_preview_action->setCheckable(true);
    connect(basic_preview_action, SIGNAL(toggled(bool)), this, SLOT(basicPreviewFromMenu(bool)));
    view_menu->addSeparator();
    // Not an action: it only exists while the key is held, so it is listed
    // purely so the shortcut is discoverable from the menu.
    QAction *hint = view_menu->addAction(tr("Voir l'original : maintenir Espace"));
    hint->setEnabled(false);

    QMenu *help_menu = bar->addMenu(tr("&Aide"));
    QAction *help_action = help_menu->addAction(tr("Comment ça marche... / How it works..."));
    help_action->setShortcut(QKeySequence::HelpContents);
    connect(help_action, SIGNAL(triggered()), this, SLOT(showHelp()));
    help_menu->addSeparator();
    QAction *about_action = help_menu->addAction(tr("À propos"));
    connect(about_action, SIGNAL(triggered()), this, SLOT(showAbout()));

    return bar;
}

void PhotoWindow::showHelp() {
    showHelpDialog(this, HelpTopic::Photo);
}

void PhotoWindow::showAbout() {
    showAboutDialog(this);
}

void PhotoWindow::createControls() {
    QHBoxLayout *outer = new QHBoxLayout(this);
    // One-off commands go in a menu bar at the top of the window; the
    // controls used constantly (aperçu, intensité) stay in the page.
    outer->setMenuBar(createMenuBar());
    QVBoxLayout *left = new QVBoxLayout();
    outer->addLayout(left, 3);

    QHBoxLayout *images_row = new QHBoxLayout();
    photo_label = new PhotoDropLabel(this);
    photo_label->setMinimumSize(480, 360);
    // Ignore the pixmap's own size hint so the label follows the window
    // instead of the image, in both directions (grow and shrink).
    photo_label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    photo_label->setAlignment(Qt::AlignCenter);
    photo_label->setStyleSheet("background-color: #101010; color: #888;");
    photo_label->setText(tr("Charge une image pour commencer\n\n(glisse un filtre ici depuis la droite pour l'appliquer pour de vrai)"));
    connect(photo_label, SIGNAL(filterDropped(QString)), this, SLOT(onFilterDropped(QString)));
    images_row->addWidget(photo_label, 2);

    QVBoxLayout *basic_col = new QVBoxLayout();
    basic_col->addWidget(new QLabel(tr("Filtre IMG basic (aperçu seul, jamais empilé)"), this));
    basic_preview_label = new QLabel(this);
    basic_preview_label->setMinimumSize(220, 165);
    basic_preview_label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    basic_preview_label->setAlignment(Qt::AlignCenter);
    basic_preview_label->setStyleSheet("background-color: #181818; color: #888; border: 1px solid #444;");
    basic_preview_label->hide();
    basic_col->addWidget(basic_preview_label, 1);
    basic_col->addStretch();
    images_row->addLayout(basic_col, 1);
    left->addLayout(images_row, 3);

    QHBoxLayout *buttons_row = new QHBoxLayout();
    buttons_row->addStretch();
    QPushButton *clear_preview_button = new QPushButton(tr("✕"), this);
    clear_preview_button->setFixedWidth(28);
    clear_preview_button->setToolTip(tr("Retirer le filtre d'aperçu (Tous/Favoris)"));
    connect(clear_preview_button, SIGNAL(pressed()), this, SLOT(clearPreviewFilter()));
    buttons_row->addWidget(clear_preview_button);
    basic_preview_check = new QCheckBox(tr("Filtre IMG basic"), this);
    basic_preview_check->setToolTip(tr("Montre une 2e copie de l'image avec uniquement le filtre en cours d'aperçu - jamais empilé, jamais appliqué pour de vrai. Sert à voir la vraie nature d'un filtre avant de l'ajouter."));
    connect(basic_preview_check, SIGNAL(toggled(bool)), this, SLOT(toggleBasicPreview(bool)));
    buttons_row->addWidget(basic_preview_check);
    left->addLayout(buttons_row);

    left->addWidget(new QLabel(tr("Filtres appliqués pour de vrai (glisse pour réordonner, clic droit pour retirer) :"), this));
    layer_list = new QListWidget(this);
    layer_list->setDragDropMode(QAbstractItemView::InternalMove);
    layer_list->setMaximumHeight(130);
    layer_list->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(layer_list, SIGNAL(customContextMenuRequested(const QPoint &)), this, SLOT(showLayerContextMenu(const QPoint &)));
    connect(layer_list->model(), SIGNAL(rowsMoved(QModelIndex, int, int, QModelIndex, int)), this, SLOT(onLayersReordered()));
    connect(layer_list, SIGNAL(itemSelectionChanged()), this, SLOT(layerSelectionChanged()));
    left->addWidget(layer_list);

    QHBoxLayout *intensity_row = new QHBoxLayout();
    intensity_row->addWidget(new QLabel(tr("Intensité :"), this));
    intensity_slider = new QSlider(Qt::Horizontal, this);
    intensity_slider->setRange(0, 100);
    intensity_slider->setValue(100);
    intensity_slider->setEnabled(false);
    intensity_slider->setToolTip(tr("Dose le calque sélectionné : 100 % = le filtre seul, 0 % = invisible. "
                                    "Entre les deux, l'image d'avant et l'image filtrée sont mélangées."));
    connect(intensity_slider, SIGNAL(valueChanged(int)), this, SLOT(intensityChanged(int)));
    connect(intensity_slider, SIGNAL(sliderReleased()), this, SLOT(intensityReleased()));
    intensity_row->addWidget(intensity_slider, 1);
    intensity_label = new QLabel(tr("—"), this);
    intensity_label->setMinimumWidth(46);
    intensity_row->addWidget(intensity_label);
    left->addLayout(intensity_row);

    status_label = new QLabel(tr("Charge une image pour commencer. Clique un filtre à droite pour l'essayer, glisse-le sur la photo pour l'ajouter pour de vrai. Maintiens Espace pour voir l'original."), this);
    status_label->setWordWrap(true);
    left->addWidget(status_label);

    filter_browser = new FilterBrowserPanel(this);
    connect(filter_browser, SIGNAL(filterClicked(QString)), this, SLOT(previewFilterSelected(QString)));
    connect(filter_browser, SIGNAL(statusMessage(QString)), this, SLOT(onStatusMessage(QString)));
    // A rename only changes labels, so just relabel the layer list.
    connect(filter_browser, SIGNAL(renamesChanged()), this, SLOT(refreshLayerList()));
    outer->addWidget(filter_browser, 1);
}

QString PhotoWindow::displayName(const QString &realName) const {
    return filter_browser->displayName(realName);
}

void PhotoWindow::onStatusMessage(QString text) {
    status_label->setText(text);
}

QImage PhotoWindow::matToImage(const cv::Mat &mat) const {
    cv::Mat rgb;
    cv::cvtColor(mat, rgb, cv::COLOR_BGR2RGB);
    return QImage(rgb.data, rgb.cols, rgb.rows, static_cast<int>(rgb.step), QImage::Format_RGB888).copy();
}

void PhotoWindow::rebuildFrom(int start_index) {
    if(base_image.empty()) {
        layer_snapshots.clear();
        stacked_result = cv::Mat();
        preview_result = cv::Mat();
        basic_result = cv::Mat();
        return;
    }

    // Only the layers from start_index upward are re-run; everything below
    // keeps its cached snapshot, so removing/moving a layer never disturbs
    // the ones underneath it.
    if(start_index < 0) start_index = 0;
    layer_snapshots.resize(applied_layers.size());
    for(int i = start_index; i < applied_layers.size(); ++i) {
        const cv::Mat &input = (i == 0) ? base_image : layer_snapshots[i - 1];
        cv::Mat out = input.clone();
        applyFilterToMat(out, applied_layers[i].filter, applied_layers[i].seed);
        blendIntensity(input, out, applied_layers[i].intensity);
        layer_snapshots[i] = out;
    }

    stacked_result = applied_layers.isEmpty() ? base_image.clone() : layer_snapshots.last();
    // The preview sits on top of the stack, so it depends on this too.
    recomputePreviewResult();
}

void PhotoWindow::recomputePreviewResult() {
    if(stacked_result.empty()) {
        preview_result = cv::Mat();
        basic_result = cv::Mat();
        return;
    }
    if(!preview_active) {
        preview_result = stacked_result.clone();
        basic_result = base_image.clone();
        return;
    }

    // preview_result is "the stack + this filter" - exactly what committing
    // the filter will produce, because committing reuses these very pixels
    // instead of running the filter a second time.
    preview_result = stacked_result.clone();
    applyFilterToMat(preview_result, preview_filter, preview_seed);

    if(applied_layers.isEmpty()) {
        // Nothing applied yet, so the isolated view has the exact same input
        // as the main one and must show the exact same thing. Running the
        // filter a second time would drift: plenty of filters keep their own
        // internal counters/buffers that release_all_objects() cannot reach,
        // so a second call lands on a different state. Share the one render.
        basic_result = preview_result.clone();
    } else {
        // Genuinely different input (untouched image vs the stack), so this
        // one has to be rendered on its own.
        basic_result = base_image.clone();
        applyFilterToMat(basic_result, preview_filter, preview_seed);
    }
}

void PhotoWindow::renderPhoto() {
    // Pure display - never re-runs a filter. Some filters are deliberately
    // random (a new pattern each call); recomputing here on every unrelated
    // UI action (toggling the basic-preview checkbox, resizing, etc.) would
    // re-roll them and make an already-applied filter look like it keeps
    // changing on its own. Only recomputeStackedResult()/recomputePreviewResult()
    // - called when the stack or the preview actually changes - touch pixels.
    if(base_image.empty() || stacked_result.empty()) {
        photo_pixmap = QPixmap();
        basic_pixmap = QPixmap();
        photo_label->setText(tr("Charge une image pour commencer\n\n(glisse un filtre ici depuis la droite pour l'appliquer pour de vrai)"));
        basic_preview_label->clear();
        return;
    }

    // Holding the before/after key swaps in the untouched original. It is a
    // display-only swap: nothing is recomputed, so releasing the key brings
    // back the exact same pixels.
    const cv::Mat &display = showing_original
        ? base_image
        : (preview_active ? preview_result : stacked_result);
    photo_pixmap = QPixmap::fromImage(matToImage(display));
    basic_pixmap = basic_result.empty() ? QPixmap() : QPixmap::fromImage(matToImage(basic_result));
    updateLabelPixmaps();
}

void PhotoWindow::updateLabelPixmaps() {
    // Scaling only - kept separate from renderPhoto() so a window resize can
    // refit the image without touching a single filter.
    if(photo_pixmap.isNull()) return;

    photo_label->setPixmap(photo_pixmap.scaled(photo_label->size(),
        Qt::KeepAspectRatio, Qt::SmoothTransformation));

    if(basic_preview_check->isChecked() && !basic_pixmap.isNull()) {
        basic_preview_label->setPixmap(basic_pixmap.scaled(basic_preview_label->size(),
            Qt::KeepAspectRatio, Qt::SmoothTransformation));
        basic_preview_label->show();
    } else {
        basic_preview_label->hide();
    }
}

void PhotoWindow::resizeEvent(QResizeEvent *event) {
    QDialog::resizeEvent(event);
    updateLabelPixmaps();
}

void PhotoWindow::loadImage() {
    QString path = QFileDialog::getOpenFileName(this, tr("Charger une image"), QString(),
        tr("Images (*.png *.jpg *.jpeg *.bmp *.webp);;Tous les fichiers (*)"));
    if(path.isEmpty()) return;

    cv::Mat img = readImageUnicodeSafe(path);
    if(img.empty()) {
        QMessageBox::warning(this, tr("Erreur"), tr("Impossible d'ouvrir cette image."));
        return;
    }

    base_image = img;
    image_path = path;
    applied_layers.clear();
    undo_stack.clear();
    preview_active = false;
    preview_filter_name.clear();
    next_layer_id = 0;
    layer_snapshots.clear();
    refreshLayerList();
    rebuildFrom(0);
    renderPhoto();

    export_action->setEnabled(true);
    save_montage_action->setEnabled(true);
    status_label->setText(tr("Image chargée : %1").arg(QFileInfo(path).fileName()));
}

void PhotoWindow::previewFilterSelected(QString realName) {
    if(realName.isEmpty() || base_image.empty()) return;
    int subfilter = -1;
    if(filterNeedsSubFilter(realName)) {
        // Without a second filter to combine with, this one produces nothing
        // usable - so ask instead of showing an empty "preview".
        subfilter = chooseSubFilter(this, displayName(realName), last_subfilter);
        if(subfilter < 0) {
            status_label->setText(tr("« %1 » a besoin d'un sous-filtre : annulé.").arg(displayName(realName)));
            return;
        }
        last_subfilter = subfilter;
    }
    // The browser always hands over the real name, never a rename.
    preview_filter_name = realName;
    preview_filter = filter_map[preview_filter_name.toStdString()];
    preview_filter.subfilter = subfilter;
    // A fresh roll for each newly picked filter; committing the preview
    // carries this seed into the layer so it can be reproduced later.
    preview_seed = QRandomGenerator::global()->generate();
    preview_active = true;
    // Only the preview changed - the already-applied stack keeps its
    // baked-in pixels untouched, so any random filter already in it does
    // not re-roll just because a different filter is now being previewed.
    recomputePreviewResult();
    renderPhoto();
    status_label->setText(tr("Aperçu : « %1 » — glisse-le sur la photo pour l'ajouter pour de vrai, ✕ pour l'enlever de l'aperçu.")
        .arg(subFilterLabel(displayName(preview_filter_name), subfilter)));
}

void PhotoWindow::clearPreviewFilter() {
    if(!preview_active) return;
    preview_active = false;
    preview_filter_name.clear();
    recomputePreviewResult();
    renderPhoto();
    status_label->setText(tr("Aperçu retiré."));
}

void PhotoWindow::onFilterDropped(QString filterName) {
    if(base_image.empty()) {
        status_label->setText(tr("Charge d'abord une image avant de déposer un filtre."));
        return;
    }
    const bool committing_preview =
        preview_active && filterName == preview_filter_name && !preview_result.empty();

    // Dropping the filter that is already previewed keeps its sub-filter;
    // dropping a different sub-filter host has to ask for one.
    int subfilter = -1;
    if(filterNeedsSubFilter(filterName)) {
        if(committing_preview && preview_filter.subfilter >= 0) {
            subfilter = preview_filter.subfilter;
        } else {
            subfilter = chooseSubFilter(this, displayName(filterName), last_subfilter);
            if(subfilter < 0) {
                status_label->setText(tr("« %1 » a besoin d'un sous-filtre : rien n'a été ajouté.")
                    .arg(displayName(filterName)));
                return;
            }
            last_subfilter = subfilter;
        }
    }

    pushUndoState();
    PhotoFilterLayer layer;
    layer.id = next_layer_id++;
    layer.filterName = filterName;
    layer.filter = filter_map[filterName.toStdString()];
    layer.filter.subfilter = subfilter;
    // Committing the preview keeps the preview's seed so a later rebuild
    // reproduces the look that was actually picked.
    layer.seed = committing_preview ? preview_seed : QRandomGenerator::global()->generate();
    applied_layers.push_back(layer);
    refreshLayerList();

    if(committing_preview) {
        // What you see is what you get: the preview already holds "stack +
        // this filter", so keep those exact pixels instead of running the
        // filter a second time. Re-running it would land on a different
        // result for every random filter, which is exactly what made the
        // committed image differ from the preview that was clicked.
        layer_snapshots.push_back(preview_result.clone());
        stacked_result = layer_snapshots.last();
        preview_active = false;
        preview_filter_name.clear();
        recomputePreviewResult();
    } else {
        // Dropping something other than the current preview: apply it on
        // top of the cached stack, leaving the existing layers' pixels
        // untouched (no re-roll of filters already applied).
        rebuildFrom(applied_layers.size() - 1);
    }

    renderPhoto();
    status_label->setText(tr("Filtre « %1 » ajouté pour de vrai (couche %2/%3).")
        .arg(subFilterLabel(displayName(filterName), subfilter))
        .arg(applied_layers.size()).arg(applied_layers.size()));
}

void PhotoWindow::refreshLayerList() {
    rebuilding_layer_list = true;
    layer_list->clear();
    for(const auto &layer : applied_layers) {
        QString text = subFilterLabel(displayName(layer.filterName), layer.filter.subfilter);
        // Only worth showing when it is not the plain full-strength case.
        if(layer.intensity < 100)
            text += tr("  (%1 %)").arg(layer.intensity);
        QListWidgetItem *item = new QListWidgetItem(text);
        item->setData(Qt::UserRole, layer.id);
        layer_list->addItem(item);
    }
    rebuilding_layer_list = false;
    layerSelectionChanged();
}

void PhotoWindow::layerSelectionChanged() {
    const int row = layer_list->currentRow();
    const bool has_layer = row >= 0 && row < applied_layers.size();
    intensity_slider->setEnabled(has_layer);
    // Reflecting the selected layer's value must not be mistaken for the user
    // dragging the slider, or it would push an undo step and rebuild.
    intensity_slider->blockSignals(true);
    intensity_slider->setValue(has_layer ? applied_layers[row].intensity : 100);
    intensity_slider->blockSignals(false);
    intensity_label->setText(has_layer ? tr("%1 %").arg(applied_layers[row].intensity) : tr("—"));
}

void PhotoWindow::intensityChanged(int value) {
    const int row = layer_list->currentRow();
    if(row < 0 || row >= applied_layers.size()) return;
    if(applied_layers[row].intensity == value) return;

    // One undo step for the whole drag, recorded before the first change.
    if(!intensity_undo_pushed) {
        pushUndoState();
        intensity_undo_pushed = true;
    }
    applied_layers[row].intensity = value;
    intensity_label->setText(tr("%1 %").arg(value));
    // Only this layer and the ones above it depend on the change; the layers
    // below keep their exact pixels, so nothing random re-rolls.
    rebuildFrom(row);
    renderPhoto();
    // Relabelling the list would reset the selection mid-drag, so the row's
    // text is patched in place instead.
    if(QListWidgetItem *item = layer_list->item(row)) {
        QString text = subFilterLabel(displayName(applied_layers[row].filterName),
                                      applied_layers[row].filter.subfilter);
        if(value < 100)
            text += tr("  (%1 %)").arg(value);
        item->setText(text);
    }
}

void PhotoWindow::intensityReleased() {
    // Drag finished: the next one starts a new undo step.
    intensity_undo_pushed = false;
    const int row = layer_list->currentRow();
    if(row < 0 || row >= applied_layers.size()) return;
    status_label->setText(tr("« %1 » réglé à %2 %.")
        .arg(displayName(applied_layers[row].filterName))
        .arg(applied_layers[row].intensity));
}

void PhotoWindow::onLayersReordered() {
    if(rebuilding_layer_list) return;
    QVector<PhotoFilterLayer> new_order;
    for(int i = 0; i < layer_list->count(); ++i) {
        int id = layer_list->item(i)->data(Qt::UserRole).toInt();
        for(const auto &layer : applied_layers) {
            if(layer.id == id) {
                new_order.push_back(layer);
                break;
            }
        }
    }
    if(new_order.size() != applied_layers.size()) return;

    // Only the layers from the first changed position upward need re-running.
    int first_change = 0;
    while(first_change < new_order.size() &&
          new_order[first_change].id == applied_layers[first_change].id)
        ++first_change;

    pushUndoState();
    applied_layers = new_order;
    rebuildFrom(first_change);
    renderPhoto();
    status_label->setText(tr("Ordre des filtres modifié."));
}

void PhotoWindow::showLayerContextMenu(const QPoint &pos) {
    QListWidgetItem *item = layer_list->itemAt(pos);
    if(!item) return;
    QMenu menu(this);
    QAction *removeAction = menu.addAction(tr("Retirer cette couche"));
    QAction *chosen = menu.exec(layer_list->mapToGlobal(pos));
    if(chosen != removeAction) return;

    int id = item->data(Qt::UserRole).toInt();
    int removed_at = -1;
    for(int i = 0; i < applied_layers.size(); ++i) {
        if(applied_layers[i].id == id) {
            removed_at = i;
            break;
        }
    }
    if(removed_at < 0) return;

    pushUndoState();
    applied_layers.remove(removed_at);
    if(removed_at < layer_snapshots.size())
        layer_snapshots.remove(removed_at);
    refreshLayerList();
    // Layers below the removed one keep their cached pixels untouched; only
    // the ones that sat on top of it are re-run. Removing the topmost layer
    // therefore recomputes nothing at all.
    rebuildFrom(removed_at);
    renderPhoto();
    status_label->setText(tr("Couche retirée."));
}

void PhotoWindow::pushUndoState() {
    undo_stack.push_back(applied_layers);
    if(undo_stack.size() > MaxUndoSteps)
        undo_stack.removeFirst();
}

void PhotoWindow::undoLastAction() {
    if(undo_stack.isEmpty()) {
        status_label->setText(tr("Rien à annuler."));
        return;
    }
    applied_layers = undo_stack.takeLast();
    refreshLayerList();
    // The whole chain is rebuilt here, but each layer carries its own seed
    // so it reproduces the same look it had before.
    layer_snapshots.clear();
    rebuildFrom(0);
    renderPhoto();
    status_label->setText(tr("Dernière modification annulée."));
}

void PhotoWindow::toggleBasicPreview(bool checked) {
    // The checkbox and the Affichage menu entry are two views of the same
    // setting; the guard keeps them from bouncing the change back at each other.
    if(basic_preview_action && basic_preview_action->isChecked() != checked)
        basic_preview_action->setChecked(checked);
    renderPhoto();
}

void PhotoWindow::basicPreviewFromMenu(bool checked) {
    if(basic_preview_check->isChecked() != checked)
        basic_preview_check->setChecked(checked);
}

void PhotoWindow::exportPhoto() {
    if(base_image.empty()) {
        QMessageBox::warning(this, tr("Erreur"), tr("Charge d'abord une image."));
        return;
    }
    QString outPath = QFileDialog::getSaveFileName(this, tr("Exporter la photo"), QString(),
        tr("Image PNG (*.png);;Image JPEG (*.jpg)"));
    if(outPath.isEmpty()) return;
    QString outExt = QFileInfo(outPath).suffix().toLower();
    if(outExt != "png" && outExt != "jpg" && outExt != "jpeg")
        outPath += ".png";

    // Export exactly what's on screen (the cached stack, no preview filter
    // on top) instead of recomputing - some filters are random, so a fresh
    // recompute could bake in a different result than what was displayed.
    if(stacked_result.empty()) {
        QMessageBox::warning(this, tr("Erreur"), tr("Charge d'abord une image."));
        return;
    }

    if(!writeImageUnicodeSafe(outPath, stacked_result)) {
        QMessageBox::warning(this, tr("Erreur"), tr("Impossible d'enregistrer l'image."));
        return;
    }
    status_label->setText(tr("Photo exportée : %1").arg(outPath));
}

void PhotoWindow::savePhotoMontage() {
    if(image_path.isEmpty()) {
        QMessageBox::warning(this, tr("Erreur"), tr("Charge d'abord une image."));
        return;
    }
    QString path = QFileDialog::getSaveFileName(this, tr("Enregistrer le montage photo"), QString(),
        tr("Montage Photo Acid Cam (*.acphoto)"));
    if(path.isEmpty()) return;
    if(!path.endsWith(".acphoto", Qt::CaseInsensitive))
        path += ".acphoto";

    QJsonObject root;
    root["image_path"] = image_path;
    QJsonArray arr;
    for(const auto &layer : applied_layers) {
        QJsonObject o;
        o["filterName"] = layer.filterName;
        // Saved so a reloaded montage renders identically instead of
        // re-rolling every random filter.
        o["seed"] = static_cast<double>(layer.seed);
        o["intensity"] = layer.intensity;
        // Sub-filter hosts do nothing without it. Stored by name, since the
        // numeric index can shift between libacidcam builds.
        if(layer.filter.subfilter >= 0 && layer.filter.subfilter < static_cast<int>(ac::draw_strings.size()))
            o["subFilterName"] = QString::fromStdString(ac::draw_strings[layer.filter.subfilter]);
        arr.append(o);
    }
    root["layers"] = arr;

    QFile file(path);
    if(!file.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, tr("Erreur"), tr("Impossible d'écrire le fichier."));
        return;
    }
    file.write(QJsonDocument(root).toJson());
    file.close();
    status_label->setText(tr("Montage photo enregistré : %1 (%2 couches).").arg(path).arg(applied_layers.size()));
}

void PhotoWindow::loadPhotoMontage() {
    QString path = QFileDialog::getOpenFileName(this, tr("Charger un montage photo"), QString(),
        tr("Montage Photo Acid Cam (*.acphoto)"));
    if(path.isEmpty()) return;

    QFile file(path);
    if(!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("Erreur"), tr("Impossible de lire le fichier."));
        return;
    }
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    if(!doc.isObject()) {
        QMessageBox::warning(this, tr("Erreur"), tr("Fichier de montage invalide."));
        return;
    }

    QJsonObject root = doc.object();
    QString imgPath = root["image_path"].toString();
    cv::Mat img = readImageUnicodeSafe(imgPath);
    if(img.empty()) {
        QMessageBox::warning(this, tr("Erreur"), tr("Impossible de recharger l'image source :\n%1\n\nLe fichier a peut-être été déplacé.").arg(imgPath));
        return;
    }

    base_image = img;
    image_path = imgPath;
    applied_layers.clear();
    undo_stack.clear();
    preview_active = false;
    preview_filter_name.clear();
    next_layer_id = 0;
    for(const QJsonValue &v : root["layers"].toArray()) {
        QJsonObject o = v.toObject();
        PhotoFilterLayer layer;
        layer.id = next_layer_id++;
        layer.filterName = o["filterName"].toString();
        layer.filter = filter_map[layer.filterName.toStdString()];
        const QString sub_name = o["subFilterName"].toString();
        layer.filter.subfilter = sub_name.isEmpty()
            ? -1
            : filter_map[sub_name.toStdString()].filter;
        // Montages saved before seeds existed simply get a fresh roll.
        layer.seed = o.contains("seed")
            ? static_cast<unsigned int>(o["seed"].toDouble())
            : QRandomGenerator::global()->generate();
        // Saved before intensities existed: full strength, as it looked then.
        layer.intensity = o.contains("intensity")
            ? std::clamp(o["intensity"].toInt(), 0, 100)
            : 100;
        applied_layers.push_back(layer);
    }
    refreshLayerList();
    layer_snapshots.clear();
    rebuildFrom(0);
    renderPhoto();

    export_action->setEnabled(true);
    save_montage_action->setEnabled(true);
    status_label->setText(tr("Montage photo chargé : %1 (%2 couches).").arg(path).arg(applied_layers.size()));
}
