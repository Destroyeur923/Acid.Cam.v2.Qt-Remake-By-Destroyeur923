#include "webcam_window.h"
#include "main_window.h"
#include "MXWrite/mxwrite.hpp"
#include <QMenu>
#include <QMenuBar>
#include "help_about.h"
#include <QKeySequence>
#include <QStandardPaths>
#include <QScrollArea>
#include <QDialogButtonBox>
#include <QSettings>
#include <ctime>

namespace {
const int MaxUndoSteps = 50;
}

// ---------------------------------------------------------------- layer row

WebcamLayerRow::WebcamLayerRow(int id, const QString &label, int intensity, QWidget *parent)
    : QWidget(parent), layer_id(id) {
    QHBoxLayout *row = new QHBoxLayout(this);
    row->setContentsMargins(4, 2, 4, 2);

    name_label = new QLabel(label, this);
    name_label->setMinimumWidth(130);
    name_label->setToolTip(label);
    row->addWidget(name_label, 1);

    slider = new QSlider(Qt::Horizontal, this);
    slider->setRange(0, 100);
    slider->setValue(intensity);
    slider->setFixedWidth(120);
    slider->setToolTip(tr("Intensité de ce filtre : 100 % = le filtre seul, 0 % = invisible."));
    connect(slider, SIGNAL(valueChanged(int)), this, SLOT(onSliderMoved(int)));
    row->addWidget(slider);

    value_label = new QLabel(tr("%1 %").arg(intensity), this);
    value_label->setMinimumWidth(42);
    row->addWidget(value_label);

    QPushButton *remove_button = new QPushButton(tr("✕"), this);
    remove_button->setFixedWidth(26);
    remove_button->setToolTip(tr("Retirer ce filtre"));
    connect(remove_button, &QPushButton::clicked, this, [this]() {
        emit removeRequested(layer_id);
    });
    row->addWidget(remove_button);
}

void WebcamLayerRow::setLabel(const QString &label) {
    name_label->setText(label);
    name_label->setToolTip(label);
}

void WebcamLayerRow::onSliderMoved(int value) {
    value_label->setText(tr("%1 %").arg(value));
    emit intensityChanged(layer_id, value);
}

// ------------------------------------------------------------ camera picker

CameraPickerDialog::CameraPickerDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle(tr("Choisir une caméra"));
    resize(420, 320);

    QVBoxLayout *layout = new QVBoxLayout(this);
    info = new QLabel(tr("Caméras détectées sur cet ordinateur :"), this);
    info->setWordWrap(true);
    layout->addWidget(info);

    list = new QListWidget(this);
    connect(list, &QListWidget::itemDoubleClicked, this, &QDialog::accept);
    layout->addWidget(list, 1);

    QDialogButtonBox *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    QPushButton *refresh_button = buttons->addButton(tr("Rechercher à nouveau"), QDialogButtonBox::ActionRole);
    connect(refresh_button, SIGNAL(clicked()), this, SLOT(refresh()));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    ok_button = buttons->button(QDialogButtonBox::Ok);
    layout->addWidget(buttons);

    refresh();
}

void CameraPickerDialog::refresh() {
    list->clear();
    const QVector<CameraDevice> cameras = enumerateCameras();
    for(const CameraDevice &cam : cameras) {
        QListWidgetItem *item = new QListWidgetItem(cam.name, list);
        item->setData(Qt::UserRole, cam.index);
    }
    if(cameras.isEmpty()) {
        info->setText(tr("Aucune caméra détectée.\n\nVérifie qu'elle est branchée et qu'aucune autre "
                         "application ne l'utilise, puis clique sur « Rechercher à nouveau »."));
    } else {
        info->setText(tr("Caméras détectées sur cet ordinateur :"));
        list->setCurrentRow(0);
    }
    ok_button->setEnabled(!cameras.isEmpty());
}

int CameraPickerDialog::selectedIndex() const {
    QListWidgetItem *item = list->currentItem();
    return item ? item->data(Qt::UserRole).toInt() : -1;
}

QString CameraPickerDialog::selectedName() const {
    QListWidgetItem *item = list->currentItem();
    return item ? item->text() : QString();
}

// ------------------------------------------------------------ webcam window

WebcamWindow::WebcamWindow(AC_MainWindow *parent) : QDialog(parent), main_window(parent) {
    setWindowTitle(tr("WebCam - Filtres en direct"));
    setWindowIcon(QPixmap(":/images/icon.png"));
    resize(1250, 780);

    frame_timer = new QTimer(this);
    connect(frame_timer, SIGNAL(timeout()), this, SLOT(grabFrame()));

    createControls();
}

WebcamWindow::~WebcamWindow() {
    stopRecording();
    stopCamera();
}

QMenuBar *WebcamWindow::createMenuBar() {
    QMenuBar *bar = new QMenuBar(this);

    QMenu *file_menu = bar->addMenu(tr("&Fichier"));
    QAction *choose = file_menu->addAction(tr("Choisir une caméra..."));
    choose->setShortcut(QKeySequence::Open);
    connect(choose, SIGNAL(triggered()), this, SLOT(chooseCamera()));
    file_menu->addSeparator();
    record_action = file_menu->addAction(tr("Démarrer l'enregistrement"));
    record_action->setShortcut(QKeySequence(tr("Ctrl+R")));
    record_action->setEnabled(false);
    connect(record_action, SIGNAL(triggered()), this, SLOT(toggleRecording()));
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
    QAction *clear_all_action = edit_menu->addAction(tr("Retirer tous les filtres appliqués"));
    connect(clear_all_action, SIGNAL(triggered()), this, SLOT(clearAllLayers()));

    QMenu *view_menu = bar->addMenu(tr("&Affichage"));
    mirror_action = view_menu->addAction(tr("Miroir (inverser horizontalement)"));
    mirror_action->setCheckable(true);
    mirror_action->setShortcut(QKeySequence(tr("Ctrl+M")));
    mirror_action->setToolTip(tr("Fait réagir l'image comme un miroir : ta main droite apparaît à droite."));
    // Remembered between sessions - it is a property of how the person is
    // filmed, not of a particular recording.
    {
        QSettings settings("LostSideDead", "Acid Cam Qt");
        mirrored = settings.value("WebcamMirror", false).toBool();
    }
    mirror_action->setChecked(mirrored);
    connect(mirror_action, SIGNAL(toggled(bool)), this, SLOT(toggleMirror(bool)));

    QMenu *help_menu = bar->addMenu(tr("&Aide"));
    QAction *help_action = help_menu->addAction(tr("Comment ça marche... / How it works..."));
    help_action->setShortcut(QKeySequence::HelpContents);
    connect(help_action, SIGNAL(triggered()), this, SLOT(showHelp()));
    help_menu->addSeparator();
    QAction *about_action = help_menu->addAction(tr("À propos"));
    connect(about_action, SIGNAL(triggered()), this, SLOT(showAbout()));

    return bar;
}

void WebcamWindow::showHelp() {
    showHelpDialog(this, HelpTopic::Webcam);
}

void WebcamWindow::showAbout() {
    showAboutDialog(this);
}

void WebcamWindow::toggleMirror(bool checked) {
    mirrored = checked;
    QSettings settings("LostSideDead", "Acid Cam Qt");
    settings.setValue("WebcamMirror", mirrored);
    status_label->setText(mirrored
        ? tr("Miroir activé : l'image est inversée horizontalement (l'enregistrement aussi).")
        : tr("Miroir désactivé : l'image brute de la caméra."));
}

void WebcamWindow::createControls() {
    QHBoxLayout *outer = new QHBoxLayout(this);
    outer->setMenuBar(createMenuBar());
    QVBoxLayout *left = new QVBoxLayout();
    outer->addLayout(left, 3);

    QHBoxLayout *views_row = new QHBoxLayout();

    // The two views only make sense if you can tell at a glance which is
    // which, so their captions are big, bold and centred over each one.
    // It has to go through a stylesheet: the app-wide QSS sets
    // "QLabel { font-size: 10px }", and a stylesheet beats setFont().
    const char *caption_style = "font-size: 15px; font-weight: 700; color: #f2f2f2; padding: 4px;";

    QVBoxLayout *live_column = new QVBoxLayout();
    QLabel *live_caption = new QLabel(tr("DIRECT — filtres appliqués"), this);
    live_caption->setStyleSheet(caption_style);
    live_caption->setAlignment(Qt::AlignCenter);
    live_caption->setToolTip(tr("C'est cette vue qui est enregistrée."));
    live_column->addWidget(live_caption);
    QLabel *live_sub = new QLabel(tr("(c'est cette vue qui est enregistrée)"), this);
    live_sub->setAlignment(Qt::AlignCenter);
    live_sub->setStyleSheet("color: #888;");
    live_column->addWidget(live_sub);
    camera_view = new FilterDropLabel(this);
    camera_view->setMinimumSize(420, 320);
    camera_view->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    camera_view->setAlignment(Qt::AlignCenter);
    camera_view->setStyleSheet("background-color: #101010; color: #888;");
    camera_view->setText(tr("Aucune caméra connectée\n\nFichier → Choisir une caméra..."));
    connect(camera_view, SIGNAL(filterDropped(QString)), this, SLOT(onFilterDropped(QString)));
    live_column->addWidget(camera_view, 1);
    views_row->addLayout(live_column, 1);

    QVBoxLayout *preview_column = new QVBoxLayout();
    QLabel *preview_caption = new QLabel(tr("APERÇU — + le filtre sélectionné"), this);
    preview_caption->setStyleSheet(caption_style);
    preview_caption->setAlignment(Qt::AlignCenter);
    preview_caption->setToolTip(tr("Les filtres appliqués, plus le filtre cliqué dans la liste. Rien n'est enregistré ici."));
    preview_column->addWidget(preview_caption);
    QLabel *preview_sub = new QLabel(tr("(essai en direct, rien n'est appliqué)"), this);
    preview_sub->setAlignment(Qt::AlignCenter);
    preview_sub->setStyleSheet("color: #888;");
    preview_column->addWidget(preview_sub);
    preview_view = new QLabel(this);
    preview_view->setMinimumSize(420, 320);
    preview_view->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    preview_view->setAlignment(Qt::AlignCenter);
    preview_view->setStyleSheet("background-color: #101010; color: #888;");
    preview_view->setText(tr("Clique un filtre à droite pour le voir ici en direct"));
    preview_column->addWidget(preview_view, 1);
    views_row->addLayout(preview_column, 1);

    left->addLayout(views_row, 3);

    QHBoxLayout *buttons_row = new QHBoxLayout();
    record_button = new QPushButton(tr("⏺ Enregistrer"), this);
    record_button->setEnabled(false);
    connect(record_button, SIGNAL(pressed()), this, SLOT(toggleRecording()));
    buttons_row->addWidget(record_button);
    camera_label = new QLabel(tr("Aucune caméra"), this);
    buttons_row->addWidget(camera_label);
    buttons_row->addStretch();
    preview_check = new QCheckBox(tr("Vue d'aperçu"), this);
    preview_check->setChecked(true);
    preview_check->setToolTip(tr("Décoche pour n'afficher que la vue de gauche - utile sur une machine lente, "
                                 "car l'aperçu fait tourner un filtre de plus à chaque image."));
    buttons_row->addWidget(preview_check);
    QPushButton *clear_preview_button = new QPushButton(tr("✕"), this);
    clear_preview_button->setFixedWidth(28);
    clear_preview_button->setToolTip(tr("Retirer le filtre d'aperçu"));
    connect(clear_preview_button, SIGNAL(pressed()), this, SLOT(clearPreviewFilter()));
    buttons_row->addWidget(clear_preview_button);
    left->addLayout(buttons_row);

    left->addWidget(new QLabel(tr("Filtres appliqués (glisse un filtre sur la vue de gauche pour l'ajouter) :"), this));
    layer_area = new QScrollArea(this);
    layer_area->setWidgetResizable(true);
    layer_area->setMinimumHeight(140);
    layer_area->setMaximumHeight(200);
    // A QScrollArea paints its viewport with the palette's Base colour - white
    // on most themes - which left the filter names unreadable against the rest
    // of the window. Window is the colour the surrounding widgets use, and
    // taking it from the palette keeps it right in a light theme too.
    layer_area->setBackgroundRole(QPalette::Window);
    layer_area->viewport()->setBackgroundRole(QPalette::Window);
    layer_container = new QWidget(layer_area);
    layer_container->setBackgroundRole(QPalette::Window);
    layer_layout = new QVBoxLayout(layer_container);
    layer_layout->setContentsMargins(0, 0, 0, 0);
    layer_layout->setSpacing(1);
    empty_layers_label = new QLabel(tr("Aucun filtre appliqué."), layer_container);
    empty_layers_label->setStyleSheet("color: #888;");
    layer_layout->addWidget(empty_layers_label);
    layer_layout->addStretch();
    layer_area->setWidget(layer_container);
    left->addWidget(layer_area);

    status_label = new QLabel(tr("Choisis une caméra pour commencer (Fichier → Choisir une caméra...)."), this);
    status_label->setWordWrap(true);
    left->addWidget(status_label);

    filter_browser = new FilterBrowserPanel(this);
    filter_browser->setToolTip(tr("Clique pour voir le filtre en direct dans la vue d'aperçu, "
                                  "glisse-le sur la vue de gauche pour l'appliquer pour de vrai."));
    connect(filter_browser, SIGNAL(filterClicked(QString)), this, SLOT(previewFilterSelected(QString)));
    connect(filter_browser, SIGNAL(statusMessage(QString)), this, SLOT(onStatusMessage(QString)));
    connect(filter_browser, SIGNAL(renamesChanged()), this, SLOT(refreshLayerLabels()));
    outer->addWidget(filter_browser, 1);
}

// ------------------------------------------------------------------- camera

void WebcamWindow::chooseCamera() {
    CameraPickerDialog picker(this);
    if(picker.exec() != QDialog::Accepted) return;
    const int index = picker.selectedIndex();
    if(index < 0) return;

    // Recording is tied to one camera's frame size, so a camera change ends it.
    if(recording)
        stopRecording();
    stopCamera();

    capture.open(index);
    if(!capture.isOpened()) {
        QMessageBox::warning(this, tr("Erreur"),
            tr("Impossible d'ouvrir « %1 ».\n\nElle est peut-être déjà utilisée par une autre application.")
                .arg(picker.selectedName()));
        camera_view->setText(tr("Aucune caméra connectée\n\nFichier → Choisir une caméra..."));
        return;
    }

    camera_index = index;
    camera_name = picker.selectedName();
    camera_fps = capture.get(cv::CAP_PROP_FPS);
    if(camera_fps <= 1.0 || camera_fps > 120.0)
        camera_fps = 30.0;

    camera_label->setText(camera_name);
    record_button->setEnabled(true);
    record_action->setEnabled(true);

    // Filters that blend against previously seen frames would otherwise start
    // from whatever the last session left behind.
    ac::release_all_objects();

    frame_timer->start(static_cast<int>(1000.0 / camera_fps));
    status_label->setText(tr("« %1 » connectée. Clique un filtre pour le voir en direct à droite, "
                             "glisse-le sur la vue de gauche pour l'appliquer.").arg(camera_name));
}

void WebcamWindow::stopCamera() {
    frame_timer->stop();
    if(capture.isOpened())
        capture.release();
    camera_index = -1;
}

QImage WebcamWindow::matToImage(const cv::Mat &mat) const {
    if(mat.empty()) return QImage();
    // Deep copy: the cv::Mat is a local that dies before the pixmap is used.
    return QImage(mat.data, mat.cols, mat.rows, static_cast<int>(mat.step),
                  QImage::Format_BGR888).copy();
}

void WebcamWindow::applyLayer(cv::Mat &frame, const FilterValue &fv, int intensity) const {
    if(fv.filter < 0 || fv.filter >= static_cast<int>(ac::draw_strings.size()))
        return;
    if(intensity <= 0)
        return;
    // Note: unlike the Photo tab, nothing is reset between frames here. Trail
    // and accumulation effects need the state they build up from one frame to
    // the next - wiping it would turn them into a flicker.
    cv::Mat before;
    if(intensity < 100)
        before = frame.clone();
    ac::setSubFilter(fv.subfilter);
    ac::CallFilter(ac::draw_strings[fv.filter], frame);
    ac::setSubFilter(-1);
    if(intensity < 100 && !before.empty()
       && before.size() == frame.size() && before.type() == frame.type()) {
        const double x = intensity / 100.0;
        cv::addWeighted(before, 1.0 - x, frame, x, 0.0, frame);
    }
}

void WebcamWindow::grabFrame() {
    if(!capture.isOpened()) return;
    cv::Mat frame;
    if(!capture.read(frame) || frame.empty()) return;

    // Flip before the filters run, so the two views and the recording all see
    // the same image - a flip applied only at display time would record the
    // opposite of what the person was looking at.
    if(mirrored)
        cv::flip(frame, frame, 1);

    for(const WebcamFilterLayer &layer : applied_layers)
        applyLayer(frame, layer.filter, layer.intensity);

    camera_view->setPixmap(QPixmap::fromImage(matToImage(frame))
        .scaled(camera_view->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));

    if(recording && recorder) {
        // The encoder was opened for the first recorded frame's size; a camera
        // that changes resolution mid-recording would corrupt the file.
        if(frame.size() == recording_size) {
            cv::Mat rgba;
            cv::cvtColor(frame, rgba, cv::COLOR_BGR2RGBA);
            // An MP4 plays its frames back at the fixed rate written in its
            // header. Writing one frame per grab is only correct when the
            // capture loop really runs at that rate - and it does not: every
            // filter added slows it down, so the file ended up played back
            // fast-forward. Instead the current frame is repeated until the
            // file holds as many frames as the declared rate says it should
            // by now, which keeps the recording's duration equal to real time
            // however fast or slow the loop happens to run.
            qint64 due = static_cast<qint64>(record_clock.elapsed() * recording_fps / 1000.0);
            // After a long stall (a modal dialog, a heavy filter), don't try
            // to make up the whole gap at once.
            const qint64 max_catch_up = frames_written + 5;
            if(due > max_catch_up)
                due = max_catch_up;
            while(frames_written < due) {
                recorder->write(rgba.data);
                ++frames_written;
            }
        }
    }

    // The preview view is the left result plus the one selected filter - not a
    // second full pass over the stack. That is both what makes it a preview
    // ("what would this filter add?") and what keeps the cost to one extra
    // filter call per frame.
    if(preview_active && preview_check->isChecked()) {
        cv::Mat preview_frame = frame.clone();
        applyLayer(preview_frame, preview_filter, 100);
        preview_view->setPixmap(QPixmap::fromImage(matToImage(preview_frame))
            .scaled(preview_view->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    } else if(preview_check->isChecked()) {
        preview_view->setPixmap(QPixmap::fromImage(matToImage(frame))
            .scaled(preview_view->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
}

// ------------------------------------------------------------------ filters

QString WebcamWindow::layerLabel(const WebcamFilterLayer &layer) const {
    return subFilterLabel(filter_browser->displayName(layer.filterName), layer.filter.subfilter);
}

void WebcamWindow::previewFilterSelected(QString realName) {
    if(realName.isEmpty()) return;
    const QString shown = filter_browser->displayName(realName);
    int subfilter = -1;
    if(filterNeedsSubFilter(realName)) {
        subfilter = chooseSubFilter(this, shown, last_subfilter);
        if(subfilter < 0) {
            status_label->setText(tr("« %1 » a besoin d'un sous-filtre : annulé.").arg(shown));
            return;
        }
        last_subfilter = subfilter;
    }
    preview_filter_name = realName;
    preview_filter = filter_map[realName.toStdString()];
    preview_filter.subfilter = subfilter;
    preview_active = true;
    if(!preview_check->isChecked())
        preview_check->setChecked(true);
    status_label->setText(tr("Aperçu en direct : « %1 » — glisse-le sur la vue de gauche pour l'appliquer.")
        .arg(subFilterLabel(shown, subfilter)));
}

void WebcamWindow::clearPreviewFilter() {
    if(!preview_active) return;
    preview_active = false;
    preview_filter_name.clear();
    status_label->setText(tr("Filtre d'aperçu retiré."));
}

void WebcamWindow::onFilterDropped(QString filterName) {
    if(!capture.isOpened()) {
        status_label->setText(tr("Connecte d'abord une caméra (Fichier → Choisir une caméra...)."));
        return;
    }

    // Dropping the filter already being previewed keeps its sub-filter instead
    // of asking for it a second time.
    int subfilter = -1;
    if(filterNeedsSubFilter(filterName)) {
        if(preview_active && preview_filter_name == filterName && preview_filter.subfilter >= 0) {
            subfilter = preview_filter.subfilter;
        } else {
            subfilter = chooseSubFilter(this, filter_browser->displayName(filterName), last_subfilter);
            if(subfilter < 0) {
                status_label->setText(tr("« %1 » a besoin d'un sous-filtre : rien n'a été ajouté.")
                    .arg(filter_browser->displayName(filterName)));
                return;
            }
            last_subfilter = subfilter;
        }
    }

    pushUndoState();
    WebcamFilterLayer layer;
    layer.id = next_layer_id++;
    layer.filterName = filterName;
    layer.filter = filter_map[filterName.toStdString()];
    layer.filter.subfilter = subfilter;
    applied_layers.push_back(layer);
    rebuildLayerRows();

    // It is now part of the stack, so keeping it in the preview too would
    // show it applied twice.
    if(preview_active && preview_filter_name == filterName) {
        preview_active = false;
        preview_filter_name.clear();
    }

    status_label->setText(tr("« %1 » appliqué en direct (%2 filtre(s) actif(s)).")
        .arg(layerLabel(layer)).arg(applied_layers.size()));
}

void WebcamWindow::rebuildLayerRows() {
    // Drop the existing rows (everything but the trailing stretch).
    while(QLayoutItem *item = layer_layout->takeAt(0)) {
        if(QWidget *w = item->widget())
            w->deleteLater();
        delete item;
    }

    if(applied_layers.isEmpty()) {
        empty_layers_label = new QLabel(tr("Aucun filtre appliqué."), layer_container);
        empty_layers_label->setStyleSheet("color: #888;");
        layer_layout->addWidget(empty_layers_label);
    } else {
        for(const WebcamFilterLayer &layer : applied_layers) {
            WebcamLayerRow *row = new WebcamLayerRow(layer.id, layerLabel(layer), layer.intensity, layer_container);
            connect(row, SIGNAL(intensityChanged(int, int)), this, SLOT(onLayerIntensityChanged(int, int)));
            connect(row, SIGNAL(removeRequested(int)), this, SLOT(onLayerRemoveRequested(int)));
            layer_layout->addWidget(row);
        }
    }
    layer_layout->addStretch();
}

void WebcamWindow::refreshLayerLabels() {
    rebuildLayerRows();
}

void WebcamWindow::onLayerIntensityChanged(int id, int value) {
    for(WebcamFilterLayer &layer : applied_layers) {
        if(layer.id == id) {
            // No rebuild and no undo step here: this fires on every pixel of
            // the drag, and the next frame already shows the new value.
            layer.intensity = value;
            return;
        }
    }
}

void WebcamWindow::onLayerRemoveRequested(int id) {
    for(int i = 0; i < applied_layers.size(); ++i) {
        if(applied_layers[i].id == id) {
            const QString name = layerLabel(applied_layers[i]);
            pushUndoState();
            applied_layers.remove(i);
            rebuildLayerRows();
            status_label->setText(tr("« %1 » retiré.").arg(name));
            return;
        }
    }
}

void WebcamWindow::clearAllLayers() {
    if(applied_layers.isEmpty()) return;
    pushUndoState();
    applied_layers.clear();
    rebuildLayerRows();
    status_label->setText(tr("Tous les filtres appliqués ont été retirés."));
}

void WebcamWindow::pushUndoState() {
    undo_stack.push_back(applied_layers);
    if(undo_stack.size() > MaxUndoSteps)
        undo_stack.removeFirst();
}

void WebcamWindow::undoLastAction() {
    if(undo_stack.isEmpty()) {
        status_label->setText(tr("Rien à annuler."));
        return;
    }
    applied_layers = undo_stack.takeLast();
    rebuildLayerRows();
    status_label->setText(tr("Annulé."));
}

void WebcamWindow::onStatusMessage(QString text) {
    status_label->setText(text);
}

// --------------------------------------------------------------- recording

bool WebcamWindow::startRecording() {
    if(!capture.isOpened()) return false;

    cv::Mat probe;
    if(!capture.read(probe) || probe.empty()) {
        QMessageBox::warning(this, tr("Erreur"), tr("Impossible de lire une image de la caméra."));
        return false;
    }

    QString dir = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    if(dir.isEmpty()) dir = QDir::homePath();
    dir += "/AcidCamWebCam";
    QDir().mkpath(dir);

    time_t t = time(0);
    struct tm *m = localtime(&t);
    recording_path = QString("%1/WebCam.%2-%3-%4_%5-%6-%7.mp4")
        .arg(dir)
        .arg(m->tm_year + 1900).arg(m->tm_mon + 1, 2, 10, QChar('0')).arg(m->tm_mday, 2, 10, QChar('0'))
        .arg(m->tm_hour, 2, 10, QChar('0')).arg(m->tm_min, 2, 10, QChar('0')).arg(m->tm_sec, 2, 10, QChar('0'));

    mx::EncodeOptions opts;
    opts.codec = "libx264";
    opts.crf = 20;            // live capture: good quality without starving the encoder
    opts.preset = "veryfast"; // has to keep up with the camera in real time
    opts.realtime = true;
    // The opposite of the offline export: here a full queue must NEVER stall
    // the capture loop, because that would freeze the live view. Dropping a
    // frame is the lesser evil.
    opts.block_when_full = false;

    recorder = new mx::Writer();
    recording_size = probe.size();
    recording_fps = camera_fps;
    frames_written = 0;
    record_clock.start();
    if(!recorder->open(recording_path.toStdString(), probe.cols, probe.rows,
                       static_cast<float>(recording_fps), opts)) {
        delete recorder;
        recorder = nullptr;
        QMessageBox::warning(this, tr("Erreur"),
            tr("Impossible de créer le fichier d'enregistrement (encodeur H.264 indisponible)."));
        return false;
    }
    return true;
}

void WebcamWindow::stopRecording() {
    if(!recorder) return;
    recorder->close();
    delete recorder;
    recorder = nullptr;
    recording = false;
}

void WebcamWindow::toggleRecording() {
    if(recording) {
        const QString path = recording_path;
        stopRecording();
        record_button->setText(tr("⏺ Enregistrer"));
        record_action->setText(tr("Démarrer l'enregistrement"));
        status_label->setText(tr("Enregistrement terminé : %1").arg(path));
        return;
    }

    if(!startRecording())
        return;
    recording = true;
    record_button->setText(tr("⏹ Arrêter"));
    record_action->setText(tr("Arrêter l'enregistrement"));
    status_label->setText(tr("Enregistrement en cours (sans audio) : %1").arg(recording_path));
}

void WebcamWindow::closeEvent(QCloseEvent *event) {
    stopRecording();
    stopCamera();
    QDialog::closeEvent(event);
}
