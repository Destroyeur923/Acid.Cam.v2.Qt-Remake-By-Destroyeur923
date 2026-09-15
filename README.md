# Acid Cam v2 Qt — Remake By Destroyeur923


https://github.com/user-attachments/assets/02d3e376-932c-40c5-99bd-73de698689d7


> 🎬 **[Voir la vidéo de démonstration](docs/acidcam_demo_destroyeur923.mp4)**
> *(en attendant qu'elle soit intégrée en lecteur — voir le commentaire ci-dessus)*

---

Une version retravaillée d'[Acid Cam v2 Qt](https://github.com/lostjared/Acid.Cam.v2.Qt), l'application
de glitch-art temps réel de **Jared Bruni**.

Acid Cam fait passer une image ou une vidéo à travers plus de **2 200 filtres** de distorsion visuelle
(moteur [libacidcam](https://github.com/lostjared/libacidcam)). Ce remake ajoute trois façons complètes
de travailler avec ces filtres, ainsi qu'un système pour les organiser.

## Ce que ce remake ajoute

| Fonctionnalité | Ce qu'elle permet |
|---|---|
| 🖼️ **Photo** | empiler des filtres sur une image fixe, avec aperçu isolé avant de valider |
| 🎬 **Montage V2** | poser des effets sur une timeline vidéo, sur des pistes qui se superposent |
| 📹 **WebCam** | appliquer des filtres **en direct** sur une caméra, avec aperçu et enregistrement |

Et, partagé par les trois :

- **Collections** — ranger les filtres dans des dossiers colorés, avec favoris et renommage
- **Sous-filtres** — plusieurs centaines de filtres inutilisables auparavant sont désormais accessibles
- **Intensité** — doser chaque filtre de 0 à 100 % au lieu du tout ou rien
- **Glisser-déposer** partout : sur l'image, sur la timeline, sur la caméra, sur une collection
- **Aide intégrée** en français et en anglais dans chaque fonctionnalité (`F1`)

📖 **[Documentation complète des fonctionnalités →](FONCTIONNALITES.md)**

## Aperçu

### Photo
Un clic essaie un filtre, un glisser-déposer l'applique pour de vrai. Une seconde vue montre le filtre
seul sur une copie vierge de l'image. Chaque calque a son intensité, et se réordonne à la souris.

### Montage V2
Timeline zoomable avec vignettes et forme d'onde. Les blocs d'effets se déplacent, se redimensionnent
et se **compositent** entre pistes. Export H.264 avec l'audio d'origine, parfaitement synchrone.

### WebCam
Deux vues côte à côte : le direct avec les filtres appliqués, et le même direct **plus le filtre
sélectionné** — un aperçu live avant de décider. Un curseur d'intensité par filtre pour régler vite
pendant une session. Miroir et enregistrement H.264.

## Installation

### Option 1 — Windows, version prête à l'emploi (recommandé)

Télécharger l'archive `AcidCam-Remake-win64.zip` depuis la page
**[Releases](../../releases)**, la décompresser n'importe où, et double-cliquer sur
**`acidcam-qt.exe`**.

Rien d'autre à installer : toutes les bibliothèques nécessaires sont dans le dossier.

> ⚠️ Décompresse l'archive **entièrement** avant de lancer l'exécutable. Ouvrir le `.exe`
> directement depuis le zip ne fonctionne pas : il ne trouvera pas ses DLL.

### Option 2 — Compiler depuis les sources (Windows / MSYS2)

À faire uniquement pour modifier le code ou si aucune Release ne correspond à ta machine.

**1. Installer MSYS2** depuis [msys2.org](https://www.msys2.org), puis ouvrir le terminal
**MSYS2 MINGW64** (pas « MSYS », pas « UCRT64 ») et installer les dépendances :

```sh
pacman -S --needed \
  mingw-w64-x86_64-gcc \
  mingw-w64-x86_64-cmake \
  mingw-w64-x86_64-ninja \
  mingw-w64-x86_64-pkgconf \
  mingw-w64-x86_64-qt6-base \
  mingw-w64-x86_64-opencv \
  mingw-w64-x86_64-SDL2 \
  mingw-w64-x86_64-ffmpeg
```

**2. Compiler et installer libacidcam**, le moteur de filtres. C'est un projet **séparé** et
c'est le prérequis le plus facile à oublier : sans lui, `cmake` s'arrête sur
`Package 'acidcam' not found`.

```sh
git clone https://github.com/lostjared/libacidcam.git
cd libacidcam
cmake -B build -G Ninja -DBUILD_SHARED_LIBS=ON -DCMAKE_INSTALL_PREFIX=/mingw64
cmake --build build
cmake --install build
cd ..
```

**3. Compiler Acid Cam** :

```sh
git clone https://github.com/Destroyeur923/Acid.Cam.v2.Qt.git
cd Acid.Cam.v2.Qt/src
cmake -B build -G Ninja
cmake --build build
```

L'exécutable est `src/build/acidcam-qt.exe`. Lancé depuis le terminal MINGW64 il trouve ses
DLL tout seul.

**4. (facultatif) Produire une version autonome** — un dossier qui fonctionne sur une machine
sans MSYS2 ni Qt, c'est-à-dire ce qui est distribué en Release :

```sh
cd ..
./scripts/package-msys2.sh
```

Le dossier `AcidCam-Remake-win64/` obtenu (~280 Mo) contient l'exécutable, toutes les DLL et
les plugins Qt. C'est lui qu'on zippe.

### Linux / macOS

Mêmes dépendances (Qt 6, OpenCV, SDL2, FFmpeg, libacidcam) via le gestionnaire de paquets de
la distribution ou Homebrew, puis l'étape 3 à l'identique. L'énumération des caméras utilise
DirectShow sous Windows et retombe ailleurs sur un balayage des index — les caméras y sont
listées « Caméra 0, 1, 2… » au lieu de leur vrai nom.

## Crédits

Acid Cam est créé et développé par **Jared Bruni** —
[lostjared/Acid.Cam.v2.Qt](https://github.com/lostjared/Acid.Cam.v2.Qt).
Le README d'origine du projet est conservé ici : [README_UPSTREAM.md](README_UPSTREAM.md).

Les fonctionnalités **Photo**, **Montage V2** et **WebCam**, le système de collections, les
sous-filtres, l'intensité et l'aide intégrée ont été ajoutés par **Destroyeur923** —
[github.com/Destroyeur923](https://github.com/Destroyeur923).

## Licence

Même licence que le projet d'origine — voir [LICENSE](LICENSE).
