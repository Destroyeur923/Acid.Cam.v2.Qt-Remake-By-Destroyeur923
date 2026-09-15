# Acid Cam v2 Qt — Remake By Destroyeur923

> Documentation des fonctionnalités ajoutées. Page d'accueil du projet : [README.md](README.md).

Ce document décrit les ajouts faits à [Acid Cam v2 Qt](https://github.com/lostjared/Acid.Cam.v2.Qt) dans
ce fork : trois nouvelles fonctionnalités (**Photo**, **Montage V2**, **WebCam**), un système
d'organisation des filtres partagé par tous, et plusieurs corrections de fond sur la manière dont les
filtres de libacidcam sont appliqués.

Le code d'origine d'Acid Cam n'est pas documenté ici — seulement ce qui a été ajouté.

---

## Sommaire

- [Vue d'ensemble](#vue-densemble)
- [Le navigateur de filtres (commun aux trois)](#le-navigateur-de-filtres-commun-aux-trois)
  - [Favoris](#favoris)
  - [Collections](#collections)
  - [Renommer un filtre](#renommer-un-filtre)
  - [Glisser-déposer](#glisser-déposer)
- [Les sous-filtres](#les-sous-filtres)
- [L'intensité (dosage d'un filtre)](#lintensité-dosage-dun-filtre)
- [Fonctionnalité Photo](#fonctionnalité-photo)
- [Fonctionnalité Montage V2](#fonctionnalité-montage-v2)
- [Fonctionnalité WebCam](#fonctionnalité-webcam)
- [Aide intégrée](#aide-intégrée)
- [Raccourcis clavier](#raccourcis-clavier)
- [Formats de fichiers](#formats-de-fichiers)
- [Notes techniques](#notes-techniques)
- [Crédits](#crédits)
- [Compilation](#compilation)

---

## Vue d'ensemble

Acid Cam applique des centaines de filtres de glitch-art à une image ou une vidéo. La version d'origine
travaille essentiellement sur un flux unique avec un filtre actif à la fois. Ce fork ajoute trois façons
de travailler :

| Fonctionnalité | Sert à | Sortie |
|---|---|---|
| **Photo** | empiler des filtres sur une image fixe, avec aperçu isolé | PNG / JPG |
| **Montage V2** | placer des effets sur une timeline vidéo, avec pistes superposées | MP4 (H.264 + audio) |
| **WebCam** | appliquer des filtres en direct sur une caméra | MP4 (H.264) |

Les trois partagent le même navigateur de filtres : les favoris, les collections et les renommages sont
communs et immédiatement visibles partout.

---

## Le navigateur de filtres (commun aux trois)

Le panneau de droite est le même widget (`FilterBrowserPanel`) dans Photo, Montage V2 et WebCam.
Il présente des onglets : **Tous**, **Favoris**, puis un onglet par collection.

Un **clic** sur un filtre le met en aperçu. Un **glisser-déposer** l'applique pour de vrai.
Cette distinction est la même dans les trois fonctionnalités.

### Favoris

Chaque ligne a un cœur à gauche. Cliquer dessus met le filtre en favori (ou l'enlève). L'onglet
**Favoris** liste tout ce qui est coché. Les favoris sont enregistrés et rechargés au démarrage.

### Collections

Une collection est un dossier de filtres. Un même filtre peut appartenir à plusieurs collections.

**Clic droit sur un filtre** ouvre un menu :
- *Mettre en favoris* / *Retirer des favoris*
- *Ajouter à une collection* → sous-menu avec *Nouvelle collection...* et la liste des collections
  existantes, chacune précédée d'une **pastille de sa couleur**
- *Retirer de « X »* (uniquement quand on est dans l'onglet d'une collection)
- *Renommer...*

À la création d'une collection, un sélecteur de couleur s'ouvre. Cette couleur sert :
- de couleur pour le texte et la pastille de l'onglet de la collection ;
- de pastille dans le sous-menu « Ajouter à une collection » ;
- de **couleur du bloc** sur la timeline de Montage V2 (voir plus bas).

**Clic droit sur l'onglet d'une collection** permet de *changer la couleur*, *renommer la collection*
ou *supprimer la collection* (les filtres eux-mêmes ne sont jamais supprimés).

**Ranger un filtre par glisser-déposer** : on peut aussi attraper un filtre et le lâcher directement sur
l'onglet d'une collection. L'onglet visé s'entoure d'un liseré bleu au survol. Seuls les onglets de
collection acceptent le dépôt — « Tous » contient déjà tout et « Favoris » a le cœur pour ça.

### Renommer un filtre

*Clic droit → Renommer...* remplace le nom affiché. Le champ est pré-rempli avec le nom actuel.
Laisser le champ vide rétablit le nom d'origine.

Le renommage est **purement cosmétique** : c'est toujours le vrai nom du filtre qui est utilisé en
interne et enregistré dans les fichiers de montage. Un montage reste donc lisible même si quelqu'un
d'autre a renommé ses filtres autrement. Les listes sont triées sur le nom affiché, et la recherche
trouve aussi bien l'un que l'autre.

### Glisser-déposer

Le glisser transporte :
- le **vrai nom** du filtre (jamais le renommage) ;
- la **couleur de l'onglet source**.

Ce qui donne, sur un bloc de la timeline Montage V2 :

| Onglet d'origine | Couleur du bloc |
|---|---|
| **Tous** | marron |
| **Favoris** | le rouge du cœur |
| une **collection** | la couleur de cette collection |

Un filtre présent dans deux collections prend la couleur de celle **depuis laquelle on l'a glissé** —
c'est l'onglet source qui décide, pas le filtre.

---

## Les sous-filtres

Plusieurs centaines de filtres de libacidcam combinent l'image avec la sortie d'un **second filtre**.
Leur nom contient toujours `SubFilter` (`BlendSubFilter`, `BlurFrameSubFilter`,
`AlphaBlendWithSubFilter`…).

Sans ce second filtre, ils ne produisent rien d'utilisable. Dans les trois fonctionnalités, cliquer
ou déposer un filtre de ce type ouvre donc une fenêtre de choix, avec recherche. Seuls les filtres qui
ne sont pas eux-mêmes des « SubFilter » sont proposés.

- Le choix est affiché partout sous la forme **`Filtre → Sous-filtre`** (liste des calques, blocs de la
  timeline, messages d'état).
- Le dernier sous-filtre choisi est **présélectionné** la fois suivante.
- Annuler n'applique rien et le dit explicitement.
- Le choix est **enregistré** dans les fichiers de montage, par **nom** et non par numéro, pour rester
  valable si l'ordre interne de libacidcam change entre deux versions.
- Déposer le filtre déjà en aperçu réutilise son sous-filtre au lieu de redemander.

---

## L'intensité (dosage d'un filtre)

Par défaut un filtre est tout ou rien. L'intensité, réglable de **0 à 100 %**, mélange l'image telle
qu'elle était **avant** le filtre avec ce que le filtre en a fait :

```
résultat = (1 − x) × image_avant  +  x × image_après
```

- **100 %** = le filtre seul (comportement d'origine)
- **50 %** = à mi-chemin
- **0 %** = le filtre est invisible

Ce n'est pas « le filtre exécuté à moitié », c'est un **fondu entre deux images**. Selon le filtre,
le rendu diffère :
- filtre de **couleur** → l'effet paraît réellement plus doux ;
- filtre de **déformation** → on obtient plutôt une **image fantôme** superposée à l'originale.

Un filtre qui **change les dimensions** de l'image ne peut pas être fondu : dans ce cas le dosage est
ignoré et le filtre s'applique à fond.

Où le régler :
- **Photo** — un curseur sous la liste des calques, agissant sur le calque sélectionné ;
- **Montage V2** — clic droit sur un bloc → *Intensité...*, avec aperçu en direct ;
- **WebCam** — un curseur par filtre, directement dans la liste (pensé pour régler vite en direct).

---

## Fonctionnalité Photo

Empiler des filtres sur une image fixe.

**Fenêtre** : l'image à gauche, une seconde vue optionnelle à droite, la liste des calques en dessous,
le navigateur de filtres à droite.

### Appliquer des filtres

- **Clic** sur un filtre → aperçu sur l'image, rien n'est modifié.
- **Glisser-déposer** sur l'image → le filtre devient un **calque** permanent, empilé sur les précédents.
- **Glisser dans la liste des calques** → réordonner la pile.
- **Clic droit sur un calque** → le retirer.
- **Curseur Intensité** → doser le calque sélectionné.

### « Filtre IMG basic »

Cette case affiche une **seconde copie de l'image** avec uniquement le filtre en cours d'aperçu, sur une
copie vierge. Jamais empilé, jamais appliqué. Sert à voir la vraie nature d'un filtre avant de l'ajouter
à une pile déjà chargée.

### Avant / après

**Maintenir Espace** affiche l'image d'origine ; relâcher revient au résultat. C'est un simple échange
d'affichage : aucun filtre n'est relancé, donc aucun filtre aléatoire ne se re-tire au passage.

### Formats

PNG, JPG, JPEG, BMP en entrée comme en sortie. L'image se redimensionne avec la fenêtre.

---

## Fonctionnalité Montage V2

Un montage vidéo à la manière d'un logiciel de montage.

### La timeline

- **piste vidéo** avec vignettes (générées en tâche de fond, recalculées selon le zoom) ;
- **piste audio** avec forme d'onde ;
- **tête de lecture** déplaçable ;
- **zoom** à la molette ou au curseur ;
- **pistes d'effets** empilées au-dessus de la vidéo.

### Les effets

- **Glisser-déposer** un filtre sur la timeline crée un bloc de 2 secondes, placé sur la première piste
  libre à cet endroit.
- Les blocs se **déplacent** (glisser) et se **redimensionnent** (glisser un bord).
- Les blocs de pistes différentes qui se chevauchent sont **composités** : la piste du bas est appliquée
  en premier, puis celle du dessus, etc. Ce n'est pas « le dernier gagne ».
- **Clic droit sur un bloc** → *Intensité...* ou *Supprimer l'effet*.
- Chaque bloc porte la **couleur** de l'onglet d'où le filtre a été glissé, et affiche son nom (tronqué
  avec « … » s'il dépasse, le nom complet restant en infobulle) ainsi que son pourcentage quand il est
  dosé.

### Aperçu

Un **clic** sur un filtre l'applique en aperçu sur l'image courante sans le placer sur la timeline.
Le bouton **✕** retire cet aperçu.

### Lecture

Lecture / pause (bouton ou **Espace**), audio synchronisé, case **Muet**.

### Export

Export en **H.264 (libx264)**, CRF 16, preset `slow`, avec l'**audio d'origine remuxé**.

La vidéo est rendue en lisant la source du début à la fin sans sauter une seule image, donc chaque image
de sortie correspond 1:1 à la timeline source — l'audio remuxé reste parfaitement synchrone, sans
décalage à corriger.

---

## Fonctionnalité WebCam

Appliquer des filtres en direct sur une caméra, avec aperçu et enregistrement.

### Choix de la caméra

À l'ouverture, la fenêtre liste les caméras détectées sur la machine **par leur vrai nom**
(énumération DirectShow sous Windows). On en sélectionne une et on se connecte.

### Les deux vues

- **À gauche** : la caméra en direct avec les filtres déjà appliqués. C'est cette vue qui est
  enregistrée.
- **À droite** : la même image en direct, **plus le filtre actuellement sélectionné**. C'est un aperçu
  live : on voit ce que le filtre donnerait avant de décider de l'ajouter.

### Appliquer des filtres

- **Clic** sur un filtre → il apparaît dans la vue d'aperçu (à droite) uniquement.
- **Glisser-déposer** sur la vue de gauche → le filtre est ajouté pour de vrai à la pile.
- Chaque filtre ajouté apparaît dans une liste, les uns sous les autres, avec **son propre curseur
  d'intensité** et un bouton pour le retirer. Pas de curseur partagé : on règle chaque filtre
  indépendamment, ce qui permet de doser vite pendant une session en direct.

### Miroir

**Affichage → Miroir** (`Ctrl+M`). L'image brute d'une caméra n'est pas inversée : lever la main droite
la fait apparaître du côté gauche de l'image. Le miroir corrige cela.

Le retournement est appliqué **avant** les filtres, donc les deux vues et l'enregistrement montrent la
même chose — un miroir purement visuel aurait produit un fichier inversé par rapport à ce que la
personne regardait pendant le tournage. Le réglage est conservé d'une session à l'autre.

### Enregistrement

Un bouton **⏺ Enregistrer / ⏹ Arrêter** (`Ctrl+R`). La sortie est un MP4 H.264 encodé en temps réel,
déposé dans `Vidéos/AcidCamWebCam/`. Seule la vue de gauche (celle avec les filtres appliqués) est
enregistrée.

La durée du fichier correspond au **temps réel**, même quand les filtres ralentissent l'affichage ;
voir [Le débit d'un enregistrement live](#le-débit-dun-enregistrement-live) pour le détail.

> **Pas d'audio.** L'enregistrement WebCam est vidéo seule : la capture du micro n'est pas branchée.

### Si l'affichage rame

La vue d'aperçu fait tourner un filtre de plus à chaque image. La case **Vue d'aperçu** la coupe pour
n'afficher que la vue de gauche. Retirer des filtres de la pile, ou descendre leur intensité à 0,
allège également la charge.

---

## Aide intégrée

Chacune des trois fonctionnalités a son propre menu **Aide** :

- **Comment ça marche...** (`F1`) ouvre une page d'aide propre à cette fonctionnalité, avec deux
  onglets : **Français** et **English**. La fenêtre est non modale, elle peut rester ouverte à côté
  pendant le travail.
- **À propos** affiche les crédits d'origine d'Acid Cam et ceux de ces ajouts.

Les trois pages d'aide se terminent par une section commune décrivant le navigateur de filtres, les
sous-filtres et l'intensité — ces trois mécanismes se comportent de la même façon partout.

---

## Raccourcis clavier

| Raccourci | Où | Action |
|---|---|---|
| `Ctrl+O` | Photo, Montage V2, WebCam | charger (image / vidéo / caméra) |
| `Ctrl+S` | Photo, Montage V2 | enregistrer le montage |
| `Ctrl+E` | Photo, Montage V2 | exporter |
| `Ctrl+Z` | Photo, Montage V2, WebCam | annuler |
| `Ctrl+W` | Photo, Montage V2, WebCam | fermer la fenêtre |
| `F1` | Photo, Montage V2, WebCam | ouvrir l'aide |
| `Espace` | Montage V2 | lecture / pause |
| `Espace` (maintenu) | Photo | voir l'image d'origine |
| `Ctrl+R` | WebCam | démarrer / arrêter l'enregistrement |
| `Ctrl+M` | WebCam | miroir |

---

## Formats de fichiers

### `.acphoto` — montage photo

JSON. Contient le chemin de l'image source et la liste des calques. Chaque calque enregistre :

| Champ | Rôle |
|---|---|
| `filterName` | vrai nom du filtre |
| `seed` | graine aléatoire, pour que le rendu soit reproductible |
| `intensity` | dosage 0–100 |
| `subFilterName` | sous-filtre, par nom (absent si le filtre n'en a pas) |

### Montage vidéo

JSON également. Chaque bloc enregistre ses bornes (`start`, `end`), sa piste, son `filterName`, sa
`color`, son `intensity` et son `subFilterName`.

**Compatibilité ascendante** : les champs ajoutés au fil du temps sont optionnels. Un fichier
enregistré avant l'arrivée de l'intensité se recharge à 100 %, un fichier sans couleur se recharge en
marron. Rien n'est cassé.

### Réglages

Stockés via `QSettings` sous `LostSideDead / Acid Cam Qt` :

| Clé | Contenu |
|---|---|
| `LabFavorites` | liste des filtres favoris |
| `FilterCollections` | collection → liste de filtres |
| `FilterCollectionColors` | collection → couleur `#rrggbb` |
| `FilterRenames` | vrai nom → nom affiché |

> `LabFavorites` est un nom hérité du mode « Lab » retiré depuis. C'est bien le stockage commun utilisé
> par tous les onglets ; il a été conservé tel quel pour ne pas perdre les favoris existants.

---

## Notes techniques

Ces points ont motivé une bonne partie du code et méritent d'être connus avant de toucher aux filtres.

### Les trois niveaux d'état d'un filtre

Beaucoup de filtres libacidcam ne sont pas des fonctions pures : le même filtre sur la même image peut
donner deux résultats différents. Il y a trois sources à cela, et elles ne se contrôlent pas de la même
façon :

1. **Les collections globales `ac::`** — des images précédentes gardées en mémoire, mélangées avec
   l'image courante. Remises à zéro par `ac::release_all_objects()`.
2. **Le `rand()` du C** — utilisé par la grande majorité des filtres. Maîtrisable en appelant
   `srand(seed)` juste avant.
3. **Les variables `static` locales aux fonctions** — compteurs, tampons internes. **Impossibles à
   remettre à zéro depuis l'extérieur.** La seule parade est de ne pas relancer le filtre et de
   réutiliser les pixels déjà calculés.

Sur une **image fixe**, on veut de la reproductibilité : l'onglet Photo remet donc l'état à zéro et
re-seede avant chaque appel, garde un **instantané par calque**, et réutilise les pixels de l'aperçu au
moment de le valider. Conséquences concrètes :
- ce qu'on voit en aperçu est exactement ce qu'on obtient en l'appliquant ;
- supprimer ou déplacer un calque ne change pas les calques du dessous ;
- une action d'interface sans rapport ne re-tire pas les filtres aléatoires.

Sur une **vidéo ou un flux live**, c'est l'inverse : les effets de traînée ont **besoin** de l'état
accumulé d'une image à l'autre. Montage V2 et WebCam ne remettent donc rien à zéro entre deux images.
Y appliquer la logique de l'onglet Photo casserait ces effets.

### Chemins de fichiers accentués

`cv::imread` / `cv::imwrite` passent la chaîne à `fopen()`, que Windows interprète dans la page de codes
ANSI du système — pas en UTF-8. Le moindre caractère accentué dans le chemin fait échouer OpenCV
silencieusement, ce qui ressemble exactement à « ce format n'est pas supporté », même pour un PNG banal.

La parade : lire/écrire les octets avec `QFile` (correct en Unicode) et laisser OpenCV décoder ou encoder
un **tampon en mémoire** via `cv::imdecode` / `cv::imencode`, sans jamais lui donner le chemin.

### Frames perdues à l'export

`mx::Writer` encode sur un thread de fond. Si la file d'encodage se remplit plus vite qu'elle ne se vide
(le preset `slow` est bien plus lent que la boucle de lecture), les images en trop sont **jetées
silencieusement** : la vidéo exportée est plus courte que la source et l'audio remuxé dépasse la fin.

`opts.block_when_full = true` fait attendre la boucle d'export au rythme réel de l'encodeur, et plus
aucune image n'est perdue. À l'inverse, en **enregistrement webcam** on veut le comportement opposé :
mieux vaut perdre une image que bloquer la caméra, donc `block_when_full = false` et `realtime = true`.

### Raccourcis en double

Un `QShortcut` et une action de menu portant la même combinaison dans la même fenêtre sont considérés
**ambigus** par Qt, qui n'en déclenche alors **aucun** — le raccourci paraît simplement mort. Chaque
combinaison ne doit avoir qu'un seul propriétaire par fenêtre.

### Glisser-déposer sous Qt

Un widget qui accepte un dépôt doit implémenter **les trois** gestionnaires : `dragEnterEvent`,
`dragMoveEvent` **et** `dropEvent`. L'implémentation par défaut de `dragMoveEvent` refuse le dépôt, ce
qui donne un glisser-déposer qui « ne marche pas » sans le moindre message.

### Le débit d'un enregistrement live

Un MP4 rejoue ses images au débit fixe inscrit dans son en-tête. Écrire une image par image capturée
n'est correct que si la boucle de capture tourne réellement à ce débit — et ce n'est pas le cas :
chaque filtre ajouté la ralentit, et le débit change encore dès qu'on en retire un. Le fichier obtenu
contient alors moins d'images que sa durée annoncée et se lit **en avance rapide**.

La parade : caler les écritures sur une horloge réelle. À chaque image capturée, l'image courante est
répétée jusqu'à ce que le fichier contienne autant d'images que le débit déclaré l'exige à cet instant.
La durée du fichier correspond alors toujours au temps réel, quel que soit le rythme de la boucle. Les
images répétées ne coûtent presque rien en taille (x264 les encode comme « rien n'a changé »), et le
rattrapage est plafonné pour qu'une pause longue ne provoque pas un sursaut.

### Le thème vient d'un QSS, pas de la palette

L'apparence sombre est une feuille de style appliquée à toute l'application
([`stylesheet.qss`](src/stylesheet.qss)), pas une palette Qt. Deux conséquences qui ont coûté du temps :

- **`setBackgroundRole()` / `setPalette()` n'ont aucun effet** sur un widget que le QSS peint. Un
  `QScrollArea` que la feuille ne stylait pas retombait sur la palette claire de Fusion et peignait son
  viewport en **blanc**, rendant son contenu illisible. Il faut ajouter la règle manquante à la feuille
  plutôt que jouer sur la palette.
- **Une feuille de style l'emporte sur `setFont()`.** Le QSS fixe `QLabel { font-size: 10px }` et
  `QTextEdit { font-family: "Courier New" }` — dont hérite `QTextBrowser`. Agrandir un texte demande
  donc un `setStyleSheet()` sur le widget lui-même : une feuille posée sur un widget a priorité sur
  celle de l'application.

---

## Crédits

Acid Cam est développé par **Jared Bruni** ([lostjared](https://github.com/lostjared/Acid.Cam.v2.Qt)).

Les fonctionnalités **Photo**, **Montage V2** et **WebCam** décrites ici, ainsi que le système de
collections, les sous-filtres, l'intensité et l'aide intégrée, ont été ajoutées par **Destroyeur923** —
[github.com/Destroyeur923](https://github.com/Destroyeur923).

---

## Compilation

Voir la section **Installation** du [README](README.md) : version prête à l'emploi en
[Releases](../../releases), ou compilation depuis les sources sous MSYS2.

Dépendances : Qt 6 (Core, Gui, Widgets, OpenGL, Network), OpenCV, SDL2, FFmpeg (via MXWrite) et
**libacidcam**, qui est un projet séparé à compiler et installer en premier. Sous Windows,
l'énumération des caméras utilise DirectShow (`strmiids`, `ole32`, `oleaut32`).

`scripts/package-msys2.sh` produit un dossier Windows autonome (exe + DLL + plugins Qt) : c'est
l'archive distribuée en Release.
