#include "help_about.h"
#include <QTabWidget>
#include <QTextBrowser>
#include <QDialogButtonBox>
#include <QDesktopServices>

namespace {

// ------------------------------------------------------------------- Photo

const char *PHOTO_FR = R"HTML(
<h2>Fonctionnalité Photo</h2>
<p>Empiler des filtres sur une image fixe, avec un aperçu avant de valider.</p>

<h3>Pour commencer</h3>
<p><b>Fichier &rarr; Charger une image...</b> (Ctrl+O). Formats acceptés : PNG, JPG, JPEG, BMP.
L'image se redimensionne automatiquement avec la fenêtre.</p>

<h3>Essayer un filtre, puis l'appliquer</h3>
<ul>
<li><b>Clic</b> sur un filtre dans la liste de droite &rarr; il s'affiche sur l'image,
mais <b>rien n'est modifié</b>. C'est un simple essai.</li>
<li><b>Glisser-déposer</b> le filtre <b>sur l'image</b> &rarr; il devient un <b>calque</b>
permanent, empilé par-dessus les précédents.</li>
</ul>
<p>Ce que tu vois en aperçu est exactement ce que tu obtiens en le déposant : les pixels de
l'aperçu sont réutilisés tels quels, ils ne sont pas recalculés.</p>

<h3>La liste des calques</h3>
<ul>
<li><b>Glisser</b> un calque dans la liste &rarr; changer l'ordre d'empilement.</li>
<li><b>Clic droit</b> sur un calque &rarr; le retirer.</li>
<li><b>Curseur Intensité</b> &rarr; doser le calque sélectionné, de 0 à 100 %.</li>
</ul>
<p>Modifier ou supprimer un calque ne change jamais l'apparence des calques situés
<b>en dessous</b> : chacun garde ses pixels exacts, même les filtres aléatoires.</p>

<h3>« Filtre IMG basic »</h3>
<p>Cette case affiche une <b>seconde copie</b> de l'image avec <b>uniquement</b> le filtre en
cours d'essai, sur une copie vierge. Jamais empilé, jamais appliqué. Utile pour voir la vraie
nature d'un filtre quand la pile est déjà chargée.</p>

<h3>Avant / après</h3>
<p><b>Maintiens la barre Espace</b> pour voir l'image d'origine ; relâche pour revenir au
résultat.</p>

<h3>Enregistrer</h3>
<ul>
<li><b>Exporter la photo...</b> (Ctrl+E) &rarr; l'image finale en PNG ou JPG.</li>
<li><b>Enregistrer le montage...</b> (Ctrl+S) &rarr; un fichier <code>.acphoto</code> qui
retient la liste des calques et leurs réglages, pour reprendre le travail plus tard.
Il ne contient pas l'image, seulement son chemin.</li>
</ul>

<h3>Raccourcis</h3>
<p><code>Ctrl+O</code> charger &middot; <code>Ctrl+S</code> enregistrer le montage &middot;
<code>Ctrl+E</code> exporter &middot; <code>Ctrl+Z</code> annuler &middot;
<code>Espace</code> (maintenu) voir l'original &middot; <code>Ctrl+W</code> fermer</p>
)HTML";

const char *PHOTO_EN = R"HTML(
<h2>Photo feature</h2>
<p>Stack filters on a still image, with a preview before you commit.</p>

<h3>Getting started</h3>
<p><b>Fichier &rarr; Charger une image...</b> (Ctrl+O). Supported formats: PNG, JPG, JPEG, BMP.
The image rescales with the window.</p>

<h3>Try a filter, then apply it</h3>
<ul>
<li><b>Click</b> a filter in the list on the right &rarr; it appears on the image, but
<b>nothing is changed</b>. It is only a trial.</li>
<li><b>Drag and drop</b> the filter <b>onto the image</b> &rarr; it becomes a permanent
<b>layer</b>, stacked on top of the previous ones.</li>
</ul>
<p>What you see in the preview is exactly what you get when you drop it: the preview's pixels
are reused as they are, not recomputed.</p>

<h3>The layer list</h3>
<ul>
<li><b>Drag</b> a layer within the list &rarr; change the stacking order.</li>
<li><b>Right-click</b> a layer &rarr; remove it.</li>
<li><b>Intensity slider</b> &rarr; dose the selected layer, from 0 to 100%.</li>
</ul>
<p>Changing or deleting a layer never alters how the layers <b>below</b> it look: each one
keeps its exact pixels, random filters included.</p>

<h3>"Filtre IMG basic"</h3>
<p>This checkbox shows a <b>second copy</b> of the image with <b>only</b> the filter being
tried, on a pristine copy. Never stacked, never applied. Useful to see what a filter really
does when the stack is already busy.</p>

<h3>Before / after</h3>
<p><b>Hold the Space bar</b> to see the original image; release to go back to the result.</p>

<h3>Saving</h3>
<ul>
<li><b>Exporter la photo...</b> (Ctrl+E) &rarr; the final image as PNG or JPG.</li>
<li><b>Enregistrer le montage...</b> (Ctrl+S) &rarr; an <code>.acphoto</code> file holding the
layer list and its settings, to pick the work up later. It stores the image's path, not the
image itself.</li>
</ul>

<h3>Shortcuts</h3>
<p><code>Ctrl+O</code> open &middot; <code>Ctrl+S</code> save project &middot;
<code>Ctrl+E</code> export &middot; <code>Ctrl+Z</code> undo &middot;
<code>Space</code> (held) show original &middot; <code>Ctrl+W</code> close</p>
)HTML";

// ---------------------------------------------------------------- Montage

const char *MONTAGE_FR = R"HTML(
<h2>Fonctionnalité Montage V2</h2>
<p>Placer des effets sur une timeline vidéo, comme dans un logiciel de montage.</p>

<h3>Pour commencer</h3>
<p><b>Fichier &rarr; Charger une vidéo...</b> (Ctrl+O). Les vignettes de la piste vidéo et la
forme d'onde audio se construisent en tâche de fond.</p>

<h3>La timeline</h3>
<ul>
<li><b>Piste vidéo</b> avec vignettes, <b>piste audio</b> avec forme d'onde.</li>
<li><b>Tête de lecture</b> : clique ou glisse pour te déplacer.</li>
<li><b>Zoom</b> : la molette de la souris, ou le curseur en bas à droite.</li>
<li><b>Pistes d'effets</b> empilées au-dessus de la vidéo.</li>
</ul>

<h3>Poser des effets</h3>
<ul>
<li><b>Clic</b> sur un filtre &rarr; aperçu sur l'image courante, sans rien poser.
Le bouton <b>✕</b> retire cet aperçu.</li>
<li><b>Glisser-déposer</b> un filtre sur la timeline &rarr; un bloc de 2 secondes est créé,
sur la première piste libre à cet endroit.</li>
<li><b>Glisser</b> un bloc pour le déplacer (horizontalement dans le temps, verticalement pour
changer de piste). <b>Glisser un bord</b> pour le redimensionner.</li>
<li><b>Clic droit</b> sur un bloc &rarr; <i>Intensité...</i> ou <i>Supprimer l'effet</i>.</li>
</ul>

<h3>Les effets se superposent</h3>
<p>Quand plusieurs blocs se chevauchent dans le temps sur des pistes différentes, ils sont
<b>composités</b> : la piste du bas est appliquée en premier, puis celle du dessus, etc.
Ce n'est pas « le dernier gagne » — les effets se combinent.</p>

<h3>La couleur des blocs</h3>
<p>Chaque bloc prend la couleur de l'onglet <b>d'où tu as glissé le filtre</b> : marron depuis
<i>Tous</i>, rouge depuis <i>Favoris</i>, ou la couleur de la collection. Un filtre présent dans
deux collections prend donc la couleur de celle par laquelle tu es passé.</p>

<h3>Lecture</h3>
<p>Bouton <b>Lecture</b> ou la <b>barre Espace</b>. L'audio est synchronisé ; la case
<b>Muet</b> le coupe.</p>

<h3>Exporter</h3>
<p><b>Fichier &rarr; Exporter la vidéo...</b> (Ctrl+E). Sortie en H.264 haute qualité, avec
<b>l'audio d'origine</b> remis en place et parfaitement synchrone.
L'export lit la source du début à la fin sans sauter d'image, donc il prend un moment.</p>

<h3>Raccourcis</h3>
<p><code>Ctrl+O</code> charger &middot; <code>Ctrl+S</code> enregistrer le montage &middot;
<code>Ctrl+E</code> exporter &middot; <code>Ctrl+Z</code> annuler &middot;
<code>Espace</code> lecture/pause &middot; <code>Ctrl+W</code> fermer</p>
)HTML";

const char *MONTAGE_EN = R"HTML(
<h2>Montage V2 feature</h2>
<p>Place effects on a video timeline, the way a video editor works.</p>

<h3>Getting started</h3>
<p><b>Fichier &rarr; Charger une vidéo...</b> (Ctrl+O). The video track's thumbnails and the
audio waveform are built in the background.</p>

<h3>The timeline</h3>
<ul>
<li><b>Video track</b> with thumbnails, <b>audio track</b> with a waveform.</li>
<li><b>Playhead</b>: click or drag to move through the video.</li>
<li><b>Zoom</b>: the mouse wheel, or the slider at the bottom right.</li>
<li><b>Effect tracks</b> stacked above the video.</li>
</ul>

<h3>Placing effects</h3>
<ul>
<li><b>Click</b> a filter &rarr; preview on the current frame, nothing is placed.
The <b>✕</b> button removes that preview.</li>
<li><b>Drag and drop</b> a filter onto the timeline &rarr; a 2-second block is created, on the
first track that is free at that point.</li>
<li><b>Drag</b> a block to move it (horizontally in time, vertically to change track).
<b>Drag an edge</b> to resize it.</li>
<li><b>Right-click</b> a block &rarr; <i>Intensité...</i> (intensity) or
<i>Supprimer l'effet</i> (delete).</li>
</ul>

<h3>Effects composite</h3>
<p>When several blocks overlap in time on different tracks they are <b>composited</b>: the
lowest track is applied first, then the one above it, and so on. It is not "last one wins" —
the effects combine.</p>

<h3>Block colours</h3>
<p>Each block takes the colour of the tab <b>you dragged the filter from</b>: brown from
<i>Tous</i> (All), red from <i>Favoris</i> (Favourites), or the collection's own colour. A filter
that belongs to two collections therefore takes the colour of the one you came through.</p>

<h3>Playback</h3>
<p>The <b>Lecture</b> button or the <b>Space bar</b>. Audio is kept in sync; the <b>Muet</b>
checkbox mutes it.</p>

<h3>Exporting</h3>
<p><b>Fichier &rarr; Exporter la vidéo...</b> (Ctrl+E). High-quality H.264 output with the
<b>original audio</b> remuxed back in, perfectly in sync. The export reads the source from start
to finish without skipping a frame, so it takes a while.</p>

<h3>Shortcuts</h3>
<p><code>Ctrl+O</code> open &middot; <code>Ctrl+S</code> save project &middot;
<code>Ctrl+E</code> export &middot; <code>Ctrl+Z</code> undo &middot;
<code>Space</code> play/pause &middot; <code>Ctrl+W</code> close</p>
)HTML";

// ----------------------------------------------------------------- WebCam

const char *WEBCAM_FR = R"HTML(
<h2>Fonctionnalité WebCam</h2>
<p>Appliquer des filtres <b>en direct</b> sur une caméra, avec aperçu et enregistrement.</p>

<h3>Choisir la caméra</h3>
<p><b>Fichier &rarr; Choisir une caméra...</b> (Ctrl+O). La liste affiche les caméras détectées
par leur vrai nom. Si tu en branches une après coup, clique sur <b>Rechercher à nouveau</b>.</p>
<p>Si une caméra refuse de s'ouvrir, c'est presque toujours qu'une autre application l'utilise
déjà.</p>

<h3>Les deux vues</h3>
<ul>
<li><b>DIRECT</b> (à gauche) : la caméra avec les filtres réellement appliqués.
<b>C'est cette vue qui est enregistrée.</b></li>
<li><b>APERÇU</b> (à droite) : la même image, <b>plus le filtre sélectionné</b>.
Rien n'y est appliqué pour de vrai — c'est là pour voir en direct ce que donnerait un filtre
avant de l'ajouter.</li>
</ul>

<h3>Appliquer des filtres</h3>
<ul>
<li><b>Clic</b> sur un filtre &rarr; il n'apparaît que dans la vue d'aperçu, à droite.</li>
<li><b>Glisser-déposer</b> sur la <b>vue de gauche</b> &rarr; il est ajouté pour de vrai.</li>
</ul>
<p>Chaque filtre ajouté apparaît dans la liste du bas avec <b>son propre curseur d'intensité</b>
et un bouton ✕. Chaque filtre se règle indépendamment, pour pouvoir doser vite pendant une
session en direct.</p>

<h3>Miroir</h3>
<p><b>Affichage &rarr; Miroir</b> (Ctrl+M). L'image brute d'une caméra n'est pas inversée : ta
main droite apparaît du côté gauche. Le miroir corrige cela. Il agit aussi sur l'enregistrement,
pour que le fichier corresponde à ce que tu voyais. Le réglage est mémorisé.</p>

<h3>Enregistrer</h3>
<p>Bouton <b>⏺ Enregistrer</b> ou Ctrl+R, puis <b>⏹ Arrêter</b>. Le fichier est un MP4 H.264
déposé dans <code>Vidéos/AcidCamWebCam/</code>.</p>
<p><b>L'enregistrement est sans son.</b> La durée du fichier correspond au temps réel, même si
les filtres ralentissent l'affichage.</p>

<h3>Si ça rame</h3>
<p>La vue d'aperçu fait tourner un filtre de plus à chaque image. Décoche la case
<b>Vue d'aperçu</b> pour n'afficher que la vue de gauche. Retirer des filtres de la pile, ou
baisser leur intensité à 0, allège aussi la charge.</p>

<h3>Raccourcis</h3>
<p><code>Ctrl+O</code> choisir une caméra &middot; <code>Ctrl+R</code> enregistrer / arrêter
&middot; <code>Ctrl+M</code> miroir &middot; <code>Ctrl+Z</code> annuler &middot;
<code>Ctrl+W</code> fermer</p>
)HTML";

const char *WEBCAM_EN = R"HTML(
<h2>WebCam feature</h2>
<p>Apply filters to a camera <b>live</b>, with a preview and recording.</p>

<h3>Choosing the camera</h3>
<p><b>Fichier &rarr; Choisir une caméra...</b> (Ctrl+O). The list shows the detected cameras by
their real names. If you plug one in afterwards, click <b>Rechercher à nouveau</b> (search
again).</p>
<p>When a camera refuses to open, it is almost always because another application is already
using it.</p>

<h3>The two views</h3>
<ul>
<li><b>DIRECT</b> (left): the camera with the filters actually applied.
<b>This is the view that gets recorded.</b></li>
<li><b>APERÇU</b> (right): the same image, <b>plus the selected filter</b>. Nothing is really
applied there — it is a live look at what a filter would do before you add it.</li>
</ul>

<h3>Applying filters</h3>
<ul>
<li><b>Click</b> a filter &rarr; it only appears in the preview view, on the right.</li>
<li><b>Drag and drop</b> onto the <b>left view</b> &rarr; it is added for real.</li>
</ul>
<p>Every added filter appears in the list below with <b>its own intensity slider</b> and a ✕
button. Each filter is dosed independently, so they can be adjusted quickly during a live
session.</p>

<h3>Mirror</h3>
<p><b>Affichage &rarr; Miroir</b> (Ctrl+M). A raw camera feed is not mirrored: your right hand
shows up on the left side. The mirror fixes that. It also applies to the recording, so the file
matches what you were looking at. The setting is remembered.</p>

<h3>Recording</h3>
<p>The <b>⏺ Enregistrer</b> button or Ctrl+R, then <b>⏹ Arrêter</b> to stop. The file is an
H.264 MP4 written to <code>Videos/AcidCamWebCam/</code>.</p>
<p><b>Recording has no audio.</b> The file's duration matches real time even when filters slow
the display down.</p>

<h3>If it runs slowly</h3>
<p>The preview view runs one extra filter on every frame. Uncheck <b>Vue d'aperçu</b> to show
only the left view. Removing filters from the stack, or dropping their intensity to 0, also
lightens the load.</p>

<h3>Shortcuts</h3>
<p><code>Ctrl+O</code> choose a camera &middot; <code>Ctrl+R</code> record / stop &middot;
<code>Ctrl+M</code> mirror &middot; <code>Ctrl+Z</code> undo &middot;
<code>Ctrl+W</code> close</p>
)HTML";

// Shared block explaining the filter browser: it behaves identically in all
// three workspaces, so each page ends with it rather than repeating a
// slightly different version of it three times.
const char *BROWSER_FR = R"HTML(
<hr>
<h3>Le panneau des filtres (identique dans les trois fonctionnalités)</h3>
<ul>
<li>Onglets <b>Tous</b>, <b>Favoris</b>, puis un onglet par <b>collection</b>.</li>
<li>Le <b>cœur</b> à gauche de chaque ligne met le filtre en favori.</li>
<li><b>Clic droit sur un filtre</b> : mettre en favoris, <i>Ajouter à une collection</i>
(avec <i>Nouvelle collection...</i>), ou <i>Renommer...</i>.</li>
<li><b>Clic droit sur l'onglet d'une collection</b> : changer sa couleur, la renommer,
la supprimer. Supprimer une collection ne supprime aucun filtre.</li>
<li>On peut aussi <b>glisser un filtre sur l'onglet d'une collection</b> pour l'y ranger.</li>
<li>Renommer un filtre ne change que l'affichage : les fichiers enregistrés gardent le vrai nom,
donc un montage reste lisible par quelqu'un d'autre.</li>
</ul>
<p>Favoris, collections, couleurs et renommages sont <b>partagés</b> par Photo, Montage V2 et
WebCam, et conservés d'une session à l'autre.</p>

<h3>Les filtres « SubFilter »</h3>
<p>Plusieurs centaines de filtres combinent l'image avec la sortie d'un <b>second filtre</b> —
leur nom contient toujours <code>SubFilter</code>. Quand tu en choisis un, une fenêtre te demande
lequel utiliser. Sans ce choix, ces filtres ne produisent rien d'utilisable.</p>

<h3>L'intensité</h3>
<p>Le curseur d'intensité mélange l'image <b>avant</b> le filtre avec le résultat <b>après</b> :
100 % = le filtre seul, 0 % = invisible. Sur un filtre de couleur, l'effet paraît simplement
plus doux ; sur un filtre de déformation, on obtient plutôt une image fantôme superposée.</p>
)HTML";

const char *BROWSER_EN = R"HTML(
<hr>
<h3>The filter panel (identical in all three features)</h3>
<ul>
<li>Tabs: <b>Tous</b> (All), <b>Favoris</b> (Favourites), then one tab per
<b>collection</b>.</li>
<li>The <b>heart</b> on the left of each row marks the filter as a favourite.</li>
<li><b>Right-click a filter</b>: add to favourites, <i>Ajouter à une collection</i> (add to a
collection, including <i>Nouvelle collection...</i>), or <i>Renommer...</i> (rename).</li>
<li><b>Right-click a collection's tab</b>: change its colour, rename it, delete it. Deleting a
collection deletes no filter.</li>
<li>You can also <b>drag a filter onto a collection's tab</b> to file it there.</li>
<li>Renaming a filter only changes what is displayed: saved files keep the real name, so a
project stays readable by someone else.</li>
</ul>
<p>Favourites, collections, colours and renames are <b>shared</b> between Photo, Montage V2 and
WebCam, and kept between sessions.</p>

<h3>"SubFilter" filters</h3>
<p>Several hundred filters combine the image with the output of a <b>second filter</b> — their
name always contains <code>SubFilter</code>. When you pick one, a window asks which filter to
combine it with. Without that choice, these filters produce nothing usable.</p>

<h3>Intensity</h3>
<p>The intensity slider blends the image <b>before</b> the filter with the result <b>after</b>
it: 100% = the filter alone, 0% = invisible. On a colour filter the effect simply looks gentler;
on a distortion filter you get a ghost image layered over the original instead.</p>
)HTML";

struct HelpPage {
    QString title;
    QString french;
    QString english;
};

HelpPage pageFor(HelpTopic topic) {
    switch(topic) {
    case HelpTopic::Photo:
        return {QObject::tr("Aide - Photo"),
                QString::fromUtf8(PHOTO_FR) + QString::fromUtf8(BROWSER_FR),
                QString::fromUtf8(PHOTO_EN) + QString::fromUtf8(BROWSER_EN)};
    case HelpTopic::Montage:
        return {QObject::tr("Aide - Montage V2"),
                QString::fromUtf8(MONTAGE_FR) + QString::fromUtf8(BROWSER_FR),
                QString::fromUtf8(MONTAGE_EN) + QString::fromUtf8(BROWSER_EN)};
    case HelpTopic::Webcam:
    default:
        return {QObject::tr("Aide - WebCam"),
                QString::fromUtf8(WEBCAM_FR) + QString::fromUtf8(BROWSER_FR),
                QString::fromUtf8(WEBCAM_EN) + QString::fromUtf8(BROWSER_EN)};
    }
}

QTextBrowser *makePane(const QString &html, QWidget *parent) {
    QTextBrowser *browser = new QTextBrowser(parent);
    browser->setOpenExternalLinks(true);
    // The app-wide stylesheet sets "QTextEdit { font-family: Courier New;
    // font-size: 10px }", and QTextBrowser inherits from QTextEdit - which
    // left these pages in tiny monospace. A stylesheet set on the widget
    // itself takes priority over the application one.
    browser->setStyleSheet(
        "QTextBrowser {"
        "  font-family: 'Segoe UI', 'Noto Sans', 'DejaVu Sans', Arial, sans-serif;"
        "  font-size: 14px;"
        "  background-color: #121212;"
        "  padding: 14px;"
        "}");
    browser->document()->setDefaultStyleSheet(
        "h2 { font-size: 20px; color: #ff6b6b; }"
        "h3 { font-size: 16px; color: #f2f2f2; margin-top: 18px; }"
        "p, li { font-size: 14px; line-height: 160%; }"
        "li { margin-bottom: 6px; }"
        "code { color: #ffb86c; font-family: 'Consolas', monospace; }");
    browser->setHtml(html);
    return browser;
}

}  // namespace

void showHelpDialog(QWidget *parent, HelpTopic topic) {
    const HelpPage page = pageFor(topic);

    QDialog *dialog = new QDialog(parent);
    // Modeless, so the help can stay open next to the window it describes.
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(page.title);
    dialog->resize(680, 720);

    QVBoxLayout *layout = new QVBoxLayout(dialog);
    QTabWidget *tabs = new QTabWidget(dialog);
    tabs->addTab(makePane(page.french, dialog), QObject::tr("Français"));
    tabs->addTab(makePane(page.english, dialog), QObject::tr("English"));
    layout->addWidget(tabs);

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
    layout->addWidget(buttons);

    dialog->show();
}

void showAboutDialog(QWidget *parent) {
    QString html;
    QTextStream stream(&html);
    // The original Acid Cam credits, kept as they are.
    stream << "<p><b>Acid Cam Qt version: " << ac_version
           << " filters: " << ac::version.c_str() << "</b></p>";
    stream << QObject::tr(
        "<p>Engineering by <b>Jared Bruni</b><br>"
        "Testing by <b>Boris D. S</b></p>"
        "<p><b>This software is dedicated to all the people that experience mental illness.</b></p>"
        "<p><a href=\"https://lostsidedead.biz/wish\">My Wish List</a></p>");
    stream << "<hr>";
    stream << QObject::tr(
        "<p>Les fonctionnalités <b>Photo</b>, <b>Montage V2</b> et <b>WebCam</b> ont "
        "été ajoutées par <b>Destroyeur923</b>.<br>"
        "<i>The <b>Photo</b>, <b>Montage V2</b> and <b>WebCam</b> features were added by "
        "<b>Destroyeur923</b>.</i></p>"
        "<p><a href=\"https://github.com/Destroyeur923\">github.com/Destroyeur923</a></p>");

    // A plain QMessageBox renders the links but will not open them; a label we
    // own can be told to, so both credits stay clickable.
    QDialog dialog(parent);
    dialog.setWindowTitle(QObject::tr("À propos d'Acid Cam"));

    QVBoxLayout *layout = new QVBoxLayout(&dialog);
    QLabel *text = new QLabel(html, &dialog);
    text->setTextFormat(Qt::RichText);
    text->setOpenExternalLinks(true);
    text->setTextInteractionFlags(Qt::TextBrowserInteraction);
    text->setWordWrap(true);
    // The app-wide stylesheet pins QLabel to 10px, which is unreadable here.
    text->setStyleSheet("QLabel { font-family: 'Segoe UI', 'Noto Sans', 'DejaVu Sans', Arial,"
                        " sans-serif; font-size: 13px; }");
    text->setMinimumWidth(430);
    layout->addWidget(text);

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    layout->addWidget(buttons);

    dialog.exec();
}
