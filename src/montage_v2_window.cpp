#include "montage_v2_window.h"
#include "main_window.h"
#include "tokenize.h"
#include "MXWrite/mxwrite.hpp"
#include <algorithm>
#include <cstdlib>
#include <QSettings>
#include <QProgressDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QTabWidget>
#include <QShortcut>
#include <QKeySequence>
#include <QMenu>
#include <QMenuBar>
#include "help_about.h"
#include <QDialogButtonBox>

namespace {
const int MaxUndoSteps = 50;
}

namespace {
QString formatTimeV2(double seconds) {
    if(seconds < 0) seconds = 0;
    int total = static_cast<int>(seconds);
    int m = total / 60;
    int s = total % 60;
    return QString("%1:%2").arg(m).arg(s, 2, 10, QChar('0'));
}
}

MontageV2Window::MontageV2Window(AC_MainWindow *parent)
    : QDialog(parent), main_window(parent), total_frames(0), fps(24.0), current_frame(0),
      playback_clock_offset(0.0), playing(false), thumbnail_worker(nullptr), waveform_worker(nullptr),
      preview_active(false) {
    setWindowTitle(tr("Montage V2 (beta) - Timeline"));
    setWindowIcon(QPixmap(":/images/icon.png"));
    resize(1100, 700);
    play_timer = new QTimer(this);
    connect(play_timer, SIGNAL(timeout()), this, SLOT(playbackTick()));
    thumbnail_regen_timer = new QTimer(this);
    thumbnail_regen_timer->setSingleShot(true);
    connect(thumbnail_regen_timer, SIGNAL(timeout()), this, SLOT(regenerateThumbnails()));
    audio = new AudioPlayer(this);
    createControls();

    // WindowShortcut context fires regardless of which child widget (filter
    // list, search box, timeline) currently has focus - e.g. right after
    // clicking a filter to preview it. QLineEdit still intercepts Space/
    // Ctrl+Z itself while it has focus and there is text to act on, via
    // Qt's normal ShortcutOverride handling, so typing in the search box
    // is unaffected.
    QShortcut *space_shortcut = new QShortcut(QKeySequence(Qt::Key_Space), this);
    space_shortcut->setContext(Qt::WindowShortcut);
    connect(space_shortcut, SIGNAL(activated()), this, SLOT(togglePlay()));

    // Ctrl+Z is owned by the Édition menu's action, which already has window
    // scope. A second QShortcut on the same key would make Qt treat both as
    // ambiguous and fire neither.
}

MontageV2Window::~MontageV2Window() {
    audio->closeAudio();
    if(thumbnail_worker) thumbnail_worker->wait();
    if(waveform_worker) waveform_worker->wait();
    if(capture.isOpened()) capture.release();
}

QMenuBar *MontageV2Window::createMenuBar() {
    QMenuBar *bar = new QMenuBar(this);

    QMenu *file_menu = bar->addMenu(tr("&Fichier"));
    QAction *load_media = file_menu->addAction(tr("Charger une vidéo..."));
    load_media->setShortcut(QKeySequence::Open);
    connect(load_media, SIGNAL(triggered()), this, SLOT(loadMedia()));
    file_menu->addSeparator();
    QAction *load_montage_action = file_menu->addAction(tr("Charger un montage..."));
    connect(load_montage_action, SIGNAL(triggered()), this, SLOT(loadMontage()));
    save_montage_action = file_menu->addAction(tr("Enregistrer le montage..."));
    save_montage_action->setShortcut(QKeySequence::Save);
    save_montage_action->setEnabled(false);
    connect(save_montage_action, SIGNAL(triggered()), this, SLOT(saveMontage()));
    file_menu->addSeparator();
    export_action = file_menu->addAction(tr("Exporter la vidéo..."));
    export_action->setShortcut(QKeySequence(tr("Ctrl+E")));
    export_action->setEnabled(false);
    connect(export_action, SIGNAL(triggered()), this, SLOT(exportMontage()));
    file_menu->addSeparator();
    QAction *close_action = file_menu->addAction(tr("Fermer"));
    close_action->setShortcut(QKeySequence::Close);
    connect(close_action, SIGNAL(triggered()), this, SLOT(close()));

    QMenu *edit_menu = bar->addMenu(tr("&Édition"));
    QAction *undo_action = edit_menu->addAction(tr("Annuler"));
    undo_action->setShortcut(QKeySequence::Undo);
    connect(undo_action, SIGNAL(triggered()), this, SLOT(undoLastAction()));
    edit_menu->addSeparator();
    QAction *clear_preview_action = edit_menu->addAction(tr("Retirer le filtre de test"));
    connect(clear_preview_action, SIGNAL(triggered()), this, SLOT(clearPreviewFilter()));

    QMenu *play_menu = bar->addMenu(tr("&Lecture"));
    QAction *play_action = play_menu->addAction(tr("Lecture / Pause"));
    play_action->setShortcut(QKeySequence(Qt::Key_Space));
    // The window already has a Space shortcut for this; leaving this one
    // active too would fire the toggle twice per press.
    play_action->setShortcutVisibleInContextMenu(true);
    play_action->setShortcutContext(Qt::WidgetShortcut);
    connect(play_action, SIGNAL(triggered()), this, SLOT(togglePlay()));
    mute_action = play_menu->addAction(tr("Muet"));
    mute_action->setCheckable(true);
    connect(mute_action, SIGNAL(toggled(bool)), this, SLOT(muteFromMenu(bool)));

    QMenu *help_menu = bar->addMenu(tr("&Aide"));
    QAction *help_action = help_menu->addAction(tr("Comment ça marche... / How it works..."));
    help_action->setShortcut(QKeySequence::HelpContents);
    connect(help_action, SIGNAL(triggered()), this, SLOT(showHelp()));
    help_menu->addSeparator();
    QAction *about_action = help_menu->addAction(tr("À propos"));
    connect(about_action, SIGNAL(triggered()), this, SLOT(showAbout()));

    return bar;
}

void MontageV2Window::showHelp() {
    showHelpDialog(this, HelpTopic::Montage);
}

void MontageV2Window::showAbout() {
    showAboutDialog(this);
}

void MontageV2Window::createControls() {
    QHBoxLayout *outer = new QHBoxLayout(this);
    // One-off commands go in a menu bar at the top of the window; the
    // controls used constantly (Lecture, Muet, Zoom) stay in the row under
    // the video, where they are one click away.
    outer->setMenuBar(createMenuBar());
    QVBoxLayout *root = new QVBoxLayout();
    outer->addLayout(root, 3);

    preview = new QLabel(this);
    preview->setMinimumSize(640, 360);
    preview->setAlignment(Qt::AlignCenter);
    preview->setStyleSheet("background-color: #101010; color: #888;");
    preview->setText(tr("Charge une vidéo pour commencer"));
    root->addWidget(preview, 3);

    QHBoxLayout *transport_row = new QHBoxLayout();
    play_button = new QPushButton(tr("▶ Lecture"), this);
    play_button->setEnabled(false);
    connect(play_button, SIGNAL(pressed()), this, SLOT(togglePlay()));
    transport_row->addWidget(play_button);
    mute_check = new QCheckBox(tr("Muet"), this);
    connect(mute_check, SIGNAL(toggled(bool)), this, SLOT(toggleMute(bool)));
    transport_row->addWidget(mute_check);
    media_label = new QLabel(tr("Aucune vidéo chargée"), this);
    transport_row->addWidget(media_label);
    time_label = new QLabel("0:00 / 0:00", this);
    transport_row->addWidget(time_label);
    transport_row->addStretch();
    clear_preview_button = new QPushButton(tr("✕"), this);
    clear_preview_button->setFixedWidth(28);
    clear_preview_button->setToolTip(tr("Retirer le filtre de test sélectionné dans la liste de droite"));
    connect(clear_preview_button, SIGNAL(pressed()), this, SLOT(clearPreviewFilter()));
    transport_row->addWidget(clear_preview_button);
    transport_row->addWidget(new QLabel(tr("Zoom :"), this));
    zoom_slider = new QSlider(Qt::Horizontal, this);
    zoom_slider->setRange(10, 400);
    zoom_slider->setValue(60);
    zoom_slider->setFixedWidth(160);
    connect(zoom_slider, SIGNAL(valueChanged(int)), this, SLOT(zoomSliderChanged(int)));
    transport_row->addWidget(zoom_slider);
    root->addLayout(transport_row);

    timeline = new TimelineView(this);
    connect(timeline, SIGNAL(seekRequested(double)), this, SLOT(seekFromTimeline(double)));
    connect(timeline, SIGNAL(zoomChanged(double)), this, SLOT(onZoomChanged(double)));
    connect(timeline, SIGNAL(effectDropped(QString, double, QColor)), this, SLOT(onEffectDropped(QString, double, QColor)));
    connect(timeline, SIGNAL(effectMenuRequested(int)), this, SLOT(onEffectMenuRequested(int)));
    connect(timeline, SIGNAL(effectEdited(int, double, double, int)), this, SLOT(onEffectEdited(int, double, double, int)));
    root->addWidget(timeline, 2);

    status_label = new QLabel(tr("Glisse un filtre favori depuis la droite sur la piste orange au-dessus de la vidéo. Clic droit sur un bloc pour le supprimer."), this);
    status_label->setWordWrap(true);
    root->addWidget(status_label);

    filter_browser = new FilterBrowserPanel(this);
    filter_browser->setToolTip(tr("Glisse un filtre sur la piste d'effets (au-dessus de la vidéo)."));
    connect(filter_browser, SIGNAL(filterClicked(QString)), this, SLOT(previewFilterSelected(QString)));
    connect(filter_browser, SIGNAL(statusMessage(QString)), this, SLOT(onStatusMessage(QString)));
    connect(filter_browser, SIGNAL(renamesChanged()), this, SLOT(refreshEffectLabels()));
    outer->addWidget(filter_browser, 1);
}

bool MontageV2Window::openMedia(const QString &path) {
    if(playing) togglePlay();
    if(capture.isOpened()) capture.release();
    capture.open(path.toStdString());
    if(!capture.isOpened())
        return false;
    total_frames = static_cast<long>(capture.get(cv::CAP_PROP_FRAME_COUNT));
    fps = capture.get(cv::CAP_PROP_FPS);
    if(fps <= 0.0) fps = 24.0;
    if(total_frames <= 0) {
        capture.release();
        return false;
    }
    media_path = path;
    current_frame = 0;
    playback_clock_offset = 0.0;
    effects.clear();
    undo_stack.clear();
    preview_active = false;
    preview_filter_name.clear();

    audio->open(path.toStdString());
    audio->setMuted(mute_check->isChecked());

    double duration = total_frames / fps;
    timeline->setDuration(duration);
    timeline->setEffects(&effects);
    startThumbnailWorker(timeline->desiredThumbnailCount());

    // Stale workers from a previously loaded video (if still running) are left
    // to finish on their own; onThumbnailsReady/onWaveformReady drop their
    // result via the path check below, and each worker deletes itself when done.
    waveform_worker = new WaveformWorker(path, 1000, this);
    connect(waveform_worker, SIGNAL(waveformReady(QString, QVector<float>)),
            this, SLOT(onWaveformReady(QString, QVector<float>)));
    connect(waveform_worker, SIGNAL(finished()), waveform_worker, SLOT(deleteLater()));
    waveform_worker->start();

    ac::release_all_objects();
    showFrame(0);
    return true;
}

void MontageV2Window::onThumbnailsReady(QString path, QVector<QImage> thumbs) {
    if(path != media_path) return;
    timeline->setVideoThumbnails(thumbs);
}

void MontageV2Window::onWaveformReady(QString path, QVector<float> peaks) {
    if(path != media_path) return;
    timeline->setWaveform(peaks);
}

void MontageV2Window::startThumbnailWorker(int count) {
    thumbnail_worker = new ThumbnailWorker(media_path, count, this);
    connect(thumbnail_worker, SIGNAL(thumbnailsReady(QString, QVector<QImage>)),
            this, SLOT(onThumbnailsReady(QString, QVector<QImage>)));
    connect(thumbnail_worker, SIGNAL(finished()), thumbnail_worker, SLOT(deleteLater()));
    thumbnail_worker->start();
}

void MontageV2Window::onZoomChanged(double) {
    if(media_path.isEmpty()) return;
    int desired = timeline->desiredThumbnailCount();
    // Only bother regenerating once the density is meaningfully off (avoids
    // firing on every single wheel tick for a tiny zoom nudge).
    if(std::abs(desired - timeline->currentThumbnailCount()) < 4) return;
    thumbnail_regen_timer->start(300);
}

void MontageV2Window::regenerateThumbnails() {
    if(media_path.isEmpty()) return;
    if(thumbnail_worker && thumbnail_worker->isRunning()) {
        // A previous regeneration is still crunching; try again shortly
        // rather than piling up capture handles on the same file.
        thumbnail_regen_timer->start(300);
        return;
    }
    startThumbnailWorker(timeline->desiredThumbnailCount());
}

void MontageV2Window::loadMedia() {
    QString path = QFileDialog::getOpenFileName(this, tr("Charger une vidéo"), QString(),
        tr("Vidéos (*.mp4 *.mov *.avi *.mkv *.webm);;Tous les fichiers (*)"));
    if(path.isEmpty()) return;
    if(!openMedia(path)) {
        QMessageBox::warning(this, tr("Erreur"), tr("Impossible d'ouvrir cette vidéo."));
        return;
    }
    media_label->setText(QFileInfo(path).fileName());
    play_button->setEnabled(true);
    export_action->setEnabled(true);
    save_montage_action->setEnabled(true);
    status_label->setText(tr("Vidéo chargée. Clique-glisse sur la timeline pour naviguer, molette+Ctrl ou le curseur pour zoomer."));
}

void MontageV2Window::applyEffectsAt(cv::Mat &frame, long frameIndex) const {
    double t = frameIndex / fps;
    QVector<const TimelineEffectClip *> active;
    for(const auto &e : effects)
        if(t >= e.start && t < e.end)
            active.push_back(&e);
    // Lower tracks are applied first, matching how they stack visually on
    // the timeline (composited layers, not "last one wins").
    std::sort(active.begin(), active.end(), [](const TimelineEffectClip *a, const TimelineEffectClip *b) {
        return a->track < b->track;
    });
    for(const TimelineEffectClip *clip : active) {
        if(clip->filter.filter < 0 || clip->filter.filter >= static_cast<int>(ac::draw_strings.size()))
            continue;
        if(clip->intensity <= 0)
            continue;
        // Dosing a clip means cross-fading between the frame as it reaches
        // this effect and what the effect makes of it, so the copy is only
        // taken when there is actually something to blend back in.
        cv::Mat before;
        if(clip->intensity < 100)
            before = frame.clone();
        ac::setSubFilter(clip->filter.subfilter);
        ac::CallFilter(ac::draw_strings[clip->filter.filter], frame);
        ac::setSubFilter(-1);
        if(clip->intensity < 100 && !before.empty()
           && before.size() == frame.size() && before.type() == frame.type()) {
            const double x = clip->intensity / 100.0;
            cv::addWeighted(before, 1.0 - x, frame, x, 0.0, frame);
        }
    }
}

void MontageV2Window::applyPreviewFilter(cv::Mat &frame) const {
    if(!preview_active) return;
    if(preview_filter.filter < 0 || preview_filter.filter >= static_cast<int>(ac::draw_strings.size())) return;
    ac::setSubFilter(preview_filter.subfilter);
    ac::CallFilter(ac::draw_strings[preview_filter.filter], frame);
    ac::setSubFilter(-1);
}

void MontageV2Window::showFrame(long index) {
    if(!capture.isOpened()) return;
    if(index < 0) index = 0;
    if(index >= total_frames) index = total_frames - 1;
    capture.set(cv::CAP_PROP_POS_FRAMES, static_cast<double>(index));
    cv::Mat frame;
    if(!capture.read(frame)) return;
    current_frame = index;

    // The preview filter is only ever shown live in this window - it is
    // intentionally left out of exportMontage()'s frame loop, which uses
    // applyEffectsAt() directly on its own capture/writer.
    applyEffectsAt(frame, index);
    applyPreviewFilter(frame);

    cv::Mat rgb;
    cv::cvtColor(frame, rgb, cv::COLOR_BGR2RGB);
    QImage img(reinterpret_cast<const unsigned char *>(rgb.data), rgb.cols, rgb.rows,
               static_cast<int>(rgb.step), QImage::Format_RGB888);
    preview->setPixmap(QPixmap::fromImage(img).scaled(preview->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));

    double t = index / fps;
    double total_t = total_frames / fps;
    time_label->setText(QString("%1 / %2").arg(formatTimeV2(t), formatTimeV2(total_t)));
}

void MontageV2Window::seekFromTimeline(double seconds) {
    if(playing) togglePlay();
    long frame = static_cast<long>(seconds * fps);
    showFrame(frame);
    playback_clock_offset = seconds;
    audio->seekTo(seconds);
}

void MontageV2Window::togglePlay() {
    if(!capture.isOpened()) return;
    playing = !playing;
    if(playing) {
        play_button->setText(tr("⏸ Pause"));
        playback_clock_offset = current_frame / fps;
        playback_clock.restart();
        if(audio->hasAudio()) {
            audio->seekTo(playback_clock_offset);
            audio->startPlayback();
        }
        play_timer->start(15);
    } else {
        play_button->setText(tr("▶ Lecture"));
        play_timer->stop();
        audio->pausePlayback();
    }
}

void MontageV2Window::toggleMute(bool checked) {
    audio->setMuted(checked);
    // The checkbox and the menu entry are two views of the same setting; the
    // guard keeps them from bouncing the change back at each other.
    if(mute_action && mute_action->isChecked() != checked)
        mute_action->setChecked(checked);
}

void MontageV2Window::muteFromMenu(bool checked) {
    if(mute_check->isChecked() != checked)
        mute_check->setChecked(checked);
}

void MontageV2Window::zoomSliderChanged(int value) {
    timeline->setZoom(static_cast<double>(value));
}

void MontageV2Window::playbackTick() {
    double total_seconds = total_frames / fps;
    double elapsed = audio->hasAudio()
        ? audio->position()
        : playback_clock_offset + playback_clock.elapsed() / 1000.0;

    if(elapsed >= total_seconds) {
        playback_clock_offset = 0.0;
        playback_clock.restart();
        if(audio->hasAudio()) {
            audio->seekTo(0);
            audio->startPlayback();
        }
        elapsed = 0.0;
    }

    long target = static_cast<long>(elapsed * fps);
    timeline->setPosition(elapsed);
    if(target != current_frame)
        showFrame(target);
}

void MontageV2Window::onStatusMessage(QString text) {
    status_label->setText(text);
}

void MontageV2Window::refreshEffectLabels() {
    // A filter was renamed: the clips on the timeline carry the real name, so
    // only the labels drawn on them have to be refreshed.
    for(TimelineEffectClip &clip : effects)
        clip.label = subFilterLabel(filter_browser->displayName(clip.filterName), clip.filter.subfilter);
    timeline->setEffects(&effects);
}

int MontageV2Window::resolveSubFilter(const QString &realName) {
    if(!filterNeedsSubFilter(realName))
        return -1;
    // The filter being previewed already has a choice: dropping that same
    // filter onto the timeline keeps it instead of asking twice.
    if(preview_active && preview_filter_name == realName && preview_filter.subfilter >= 0)
        return preview_filter.subfilter;
    const int chosen = chooseSubFilter(this, filter_browser->displayName(realName), last_subfilter);
    if(chosen >= 0)
        last_subfilter = chosen;
    return chosen;
}

void MontageV2Window::previewFilterSelected(QString realName) {
    if(realName.isEmpty() || !capture.isOpened()) return;
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
    preview_filter = filter_map[preview_filter_name.toStdString()];
    preview_filter.subfilter = subfilter;
    preview_active = true;
    showFrame(current_frame);
    status_label->setText(tr("Aperçu test : « %1 » — clique ✕ pour l'enlever, glisse-le sur la piste pour le garder.")
        .arg(subFilterLabel(shown, subfilter)));
}

void MontageV2Window::clearPreviewFilter() {
    if(!preview_active) return;
    preview_active = false;
    preview_filter_name.clear();
    showFrame(current_frame);
    status_label->setText(tr("Filtre de test retiré."));
}

void MontageV2Window::onEffectDropped(QString filterName, double seconds, QColor color) {
    if(!capture.isOpened()) return;
    const int subfilter = resolveSubFilter(filterName);
    if(filterNeedsSubFilter(filterName) && subfilter < 0) {
        status_label->setText(tr("« %1 » a besoin d'un sous-filtre : rien n'a été placé.")
            .arg(filter_browser->displayName(filterName)));
        return;
    }
    double total_seconds = total_frames / fps;
    const double default_duration = 2.0;
    double start = std::clamp(seconds, 0.0, std::max(0.0, total_seconds - default_duration));
    double end = std::min(start + default_duration, total_seconds);

    // Place on the first track (starting at 0) where this time range is
    // free; overlapping tracks are allowed and composited together, so this
    // never gets rejected - it just stacks upward.
    int track = 0;
    while(true) {
        bool overlap = false;
        for(const auto &e : effects) {
            if(e.track == track && start < e.end && end > e.start) {
                overlap = true;
                break;
            }
        }
        if(!overlap) break;
        ++track;
    }

    TimelineEffectClip clip;
    clip.start = start;
    clip.end = end;
    clip.filterName = filterName;
    clip.color = color.isValid() ? color : filterNeutralColor();
    clip.filter = filter_map[filterName.toStdString()];
    clip.filter.subfilter = subfilter;
    clip.label = subFilterLabel(filter_browser->displayName(filterName), subfilter);
    clip.track = track;
    pushUndoState();
    effects.push_back(clip);
    timeline->setEffects(&effects);
    showFrame(current_frame);
    status_label->setText(tr("Effet « %1 » placé sur la piste %2, de %3 à %4. Clic droit pour le supprimer, glisse-le verticalement pour changer de piste.")
        .arg(clip.label).arg(track + 1).arg(formatTimeV2(start), formatTimeV2(end)));
}

void MontageV2Window::onEffectEdited(int index, double newStart, double newEnd, int newTrack) {
    if(index < 0 || index >= effects.size()) return;
    const TimelineEffectClip &cur = effects[index];
    // A plain click without dragging still emits this with unchanged values -
    // skip recording an undo step when nothing actually moved.
    if(cur.start == newStart && cur.end == newEnd && cur.track == newTrack)
        return;
    pushUndoState();
    effects[index].start = newStart;
    effects[index].end = newEnd;
    effects[index].track = newTrack;
    timeline->setEffects(&effects);
    showFrame(current_frame);
    status_label->setText(tr("Effet « %1 » ajusté : piste %2, %3 → %4.")
        .arg(filter_browser->displayName(effects[index].filterName)).arg(newTrack + 1).arg(formatTimeV2(newStart), formatTimeV2(newEnd)));
}

void MontageV2Window::onEffectMenuRequested(int index) {
    if(index < 0 || index >= effects.size()) return;
    QMenu menu(this);
    QAction *intensity_action = menu.addAction(
        tr("Intensité : %1 %...").arg(effects[index].intensity));
    menu.addSeparator();
    QAction *delete_action = menu.addAction(tr("Supprimer l'effet"));
    QAction *chosen = menu.exec(QCursor::pos());
    if(chosen == intensity_action)
        editEffectIntensity(index);
    else if(chosen == delete_action)
        onEffectDeleteRequested(index);
}

void MontageV2Window::editEffectIntensity(int index) {
    if(index < 0 || index >= effects.size()) return;
    const int original = effects[index].intensity;

    // Move the playhead inside the clip if it is not already, otherwise the
    // live preview would show a frame the effect does not even touch.
    if(current_frame / fps < effects[index].start || current_frame / fps >= effects[index].end) {
        const double middle = (effects[index].start + effects[index].end) / 2.0;
        seekFromTimeline(middle);
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Intensité de « %1 »").arg(effects[index].label));
    QVBoxLayout *layout = new QVBoxLayout(&dialog);
    QLabel *info = new QLabel(tr("100 % = le filtre seul, 0 % = invisible.\n"
                                 "Entre les deux, l'image d'avant et l'image filtrée sont mélangées."), &dialog);
    info->setWordWrap(true);
    layout->addWidget(info);

    QHBoxLayout *row = new QHBoxLayout();
    QSlider *slider = new QSlider(Qt::Horizontal, &dialog);
    slider->setRange(0, 100);
    slider->setValue(original);
    row->addWidget(slider, 1);
    QLabel *value_label = new QLabel(tr("%1 %").arg(original), &dialog);
    value_label->setMinimumWidth(46);
    row->addWidget(value_label);
    layout->addLayout(row);

    QDialogButtonBox *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    // Live: the current frame is re-rendered as the slider moves.
    connect(slider, &QSlider::valueChanged, this, [&](int value) {
        value_label->setText(tr("%1 %").arg(value));
        effects[index].intensity = value;
        showFrame(current_frame);
    });

    if(dialog.exec() != QDialog::Accepted) {
        effects[index].intensity = original;
        showFrame(current_frame);
        return;
    }

    const int chosen = slider->value();
    if(chosen == original) return;
    // Undo has to capture the value from before the dialog, so the live
    // preview's writes are rolled back first.
    effects[index].intensity = original;
    pushUndoState();
    effects[index].intensity = chosen;
    timeline->setEffects(&effects);
    showFrame(current_frame);
    status_label->setText(tr("« %1 » réglé à %2 %.").arg(effects[index].label).arg(chosen));
}

void MontageV2Window::onEffectDeleteRequested(int index) {
    if(index < 0 || index >= effects.size()) return;
    QString name = filter_browser->displayName(effects[index].filterName);
    pushUndoState();
    effects.remove(index);
    timeline->setEffects(&effects);
    showFrame(current_frame);
    status_label->setText(tr("Effet « %1 » supprimé.").arg(name));
}

void MontageV2Window::pushUndoState() {
    undo_stack.push_back(effects);
    if(undo_stack.size() > MaxUndoSteps)
        undo_stack.removeFirst();
}

void MontageV2Window::undoLastAction() {
    if(undo_stack.isEmpty()) {
        status_label->setText(tr("Rien à annuler."));
        return;
    }
    effects = undo_stack.takeLast();
    timeline->setEffects(&effects);
    showFrame(current_frame);
    status_label->setText(tr("Dernière modification annulée."));
}

void MontageV2Window::exportMontage() {
    if(!capture.isOpened()) {
        QMessageBox::warning(this, tr("Erreur"), tr("Charge d'abord une vidéo."));
        return;
    }
    if(playing) togglePlay();

    QString outPath = QFileDialog::getSaveFileName(this, tr("Exporter le montage"), QString(), tr("Vidéo MP4 (*.mp4)"));
    if(outPath.isEmpty()) return;
    if(!outPath.endsWith(".mp4", Qt::CaseInsensitive))
        outPath += ".mp4";

    cv::VideoCapture exportCap(media_path.toStdString());
    if(!exportCap.isOpened()) {
        QMessageBox::warning(this, tr("Erreur"), tr("Impossible de relire la vidéo source."));
        return;
    }
    int w = static_cast<int>(exportCap.get(cv::CAP_PROP_FRAME_WIDTH));
    int h = static_cast<int>(exportCap.get(cv::CAP_PROP_FRAME_HEIGHT));

    // Render the silent, effects-applied video first (H.264/libx264, low
    // CRF = high quality), then mux the original audio track onto it in a
    // second pass. The video is rendered by reading the source start-to-end
    // with no frame skipping, so every output frame lines up 1:1 with the
    // source timeline - muxing the untouched original audio back on keeps
    // it perfectly in sync, no manual offset needed.
    QString tempPath = outPath + ".video_only.mp4";

    mx::EncodeOptions opts;
    opts.codec = "libx264";
    opts.crf = 16;       // high quality (README recommends 18-28; 16 is a notch above "high")
    opts.preset = "slow"; // export is offline, not realtime - trade encode time for quality/efficiency
    opts.realtime = false;
    // Without this, MXWrite's internal encode queue silently DROPS frames
    // once it fills up faster than the encoder drains it (the "slow" preset
    // is much slower than the frame-reading loop below) - the exported
    // video ends up shorter than the source and the muxed audio runs past
    // its end. block_when_full makes write() pace the export loop to the
    // encoder's real throughput instead, so every frame is kept.
    opts.block_when_full = true;

    mx::Writer video_writer;
    if(!video_writer.open(tempPath.toStdString(), w, h, static_cast<float>(fps), opts)) {
        QMessageBox::warning(this, tr("Erreur"), tr("Impossible de créer le fichier vidéo (encodeur H.264 indisponible)."));
        exportCap.release();
        return;
    }

    ac::release_all_objects();

    QProgressDialog progress(tr("Export en cours (vidéo)..."), tr("Annuler"), 0, static_cast<int>(total_frames), this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);

    cv::Mat frame, rgba;
    long idx = 0;
    bool cancelled = false;
    while(exportCap.read(frame)) {
        applyEffectsAt(frame, idx);
        cv::cvtColor(frame, rgba, cv::COLOR_BGR2RGBA);
        video_writer.write(rgba.data);
        ++idx;
        progress.setValue(static_cast<int>(idx));
        QApplication::processEvents();
        if(progress.wasCanceled()) {
            cancelled = true;
            break;
        }
    }
    video_writer.close();
    exportCap.release();
    ac::release_all_objects();

    if(cancelled) {
        QFile::remove(tempPath);
        status_label->setText(tr("Export annulé."));
        return;
    }

    status_label->setText(tr("Vidéo encodée, ajout de la piste audio..."));
    QApplication::processEvents();

    bool audioOk = ffmpeg_mux_audio(tempPath.toStdString(), media_path.toStdString(), outPath.toStdString());
    QFile::remove(tempPath);

    if(audioOk)
        status_label->setText(tr("Export terminé (H.264, son synchronisé) : %1").arg(outPath));
    else
        status_label->setText(tr("Export vidéo terminé (H.264) mais l'ajout du son a échoué — fichier silencieux : %1").arg(outPath));
}

void MontageV2Window::saveMontage() {
    if(media_path.isEmpty()) {
        QMessageBox::warning(this, tr("Erreur"), tr("Charge d'abord une vidéo."));
        return;
    }
    QString path = QFileDialog::getSaveFileName(this, tr("Enregistrer le montage"), QString(), tr("Montage Acid Cam (*.acmontage)"));
    if(path.isEmpty()) return;
    if(!path.endsWith(".acmontage", Qt::CaseInsensitive))
        path += ".acmontage";

    QJsonObject root;
    root["media_path"] = media_path;
    QJsonArray arr;
    for(const auto &e : effects) {
        QJsonObject o;
        o["start"] = e.start;
        o["end"] = e.end;
        o["filterName"] = e.filterName;
        o["color"] = e.color.isValid() ? e.color.name() : filterNeutralColor().name();
        // Sub-filter hosts are useless without it, so it has to survive a save.
        // Stored by name: the numeric index can shift between libacidcam builds.
        if(e.filter.subfilter >= 0 && e.filter.subfilter < static_cast<int>(ac::draw_strings.size()))
            o["subFilterName"] = QString::fromStdString(ac::draw_strings[e.filter.subfilter]);
        o["intensity"] = e.intensity;
        o["track"] = e.track;
        arr.append(o);
    }
    root["effects"] = arr;

    QFile file(path);
    if(!file.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, tr("Erreur"), tr("Impossible d'écrire le fichier."));
        return;
    }
    file.write(QJsonDocument(root).toJson());
    file.close();
    status_label->setText(tr("Montage enregistré : %1 (%2 effets).").arg(path).arg(effects.size()));
}

void MontageV2Window::loadMontage() {
    QString path = QFileDialog::getOpenFileName(this, tr("Charger un montage"), QString(), tr("Montage Acid Cam (*.acmontage)"));
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
    QString mpath = root["media_path"].toString();
    if(!openMedia(mpath)) {
        QMessageBox::warning(this, tr("Erreur"), tr("Impossible de recharger la vidéo source :\n%1\n\nLe fichier a peut-être été déplacé.").arg(mpath));
        return;
    }

    effects.clear();
    for(const QJsonValue &v : root["effects"].toArray()) {
        QJsonObject o = v.toObject();
        TimelineEffectClip clip;
        clip.start = o["start"].toDouble();
        clip.end = o["end"].toDouble();
        clip.filterName = o["filterName"].toString();
        // Montages saved before clips had a colour simply get the brown.
        clip.color = QColor(o["color"].toString());
        if(!clip.color.isValid())
            clip.color = filterNeutralColor();
        clip.filter = filter_map[clip.filterName.toStdString()];
        const QString sub_name = o["subFilterName"].toString();
        clip.filter.subfilter = sub_name.isEmpty()
            ? -1
            : filter_map[sub_name.toStdString()].filter;
        // Montages saved before intensities existed stay at full strength.
        clip.intensity = o.contains("intensity")
            ? std::clamp(o["intensity"].toInt(), 0, 100)
            : 100;
        clip.label = subFilterLabel(filter_browser->displayName(clip.filterName), clip.filter.subfilter);
        clip.track = o["track"].toInt();
        effects.push_back(clip);
    }
    timeline->setEffects(&effects);
    showFrame(current_frame);

    media_label->setText(QFileInfo(mpath).fileName());
    play_button->setEnabled(true);
    export_action->setEnabled(true);
    save_montage_action->setEnabled(true);
    status_label->setText(tr("Montage chargé : %1 (%2 effets).").arg(path).arg(effects.size()));
}

void MontageV2Window::closeEvent(QCloseEvent *event) {
    if(playing) togglePlay();
    audio->closeAudio();
    QDialog::closeEvent(event);
}
