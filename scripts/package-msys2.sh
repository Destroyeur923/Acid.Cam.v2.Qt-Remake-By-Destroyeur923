#!/bin/bash
# package-msys2.sh - Produit un dossier Windows autonome (exe + DLL + plugins Qt)
#
# À lancer depuis un terminal MSYS2 MINGW64, après avoir compilé le projet.
# Le dossier produit se lance sur une machine SANS MSYS2 ni Qt installés :
# c'est ce dossier qu'on zippe pour une Release GitHub.
#
# Usage :
#   ./scripts/package-msys2.sh [dossier_de_sortie]
#
# Par défaut : ./AcidCam-Remake-win64/

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${PROJECT_DIR}/src/build"
OUT_DIR="${1:-${PROJECT_DIR}/AcidCam-Remake-win64}"
EXE="acidcam-qt.exe"

if [ ! -f "${BUILD_DIR}/${EXE}" ]; then
    echo "ERREUR : ${BUILD_DIR}/${EXE} est introuvable."
    echo "Compile d'abord :  cd src && cmake -B build -G Ninja && cmake --build build"
    exit 1
fi

echo "==> Dossier de sortie : ${OUT_DIR}"
rm -rf "${OUT_DIR}"
mkdir -p "${OUT_DIR}"

cp "${BUILD_DIR}/${EXE}" "${OUT_DIR}/"

# Le plugin de base est chargé au démarrage s'il est présent.
if [ -f "${BUILD_DIR}/basic.dll" ]; then
    mkdir -p "${OUT_DIR}/plugins/basic"
    cp "${BUILD_DIR}/basic.dll" "${OUT_DIR}/plugins/basic/"
fi

# windeployqt s'occupe des DLL Qt et des plugins Qt (platforms/, imageformats/,
# styles/...). Sans le plugin "platforms/qwindows.dll" l'application refuse de
# démarrer avec "could not find or load the Qt platform plugin windows".
echo "==> windeployqt (DLL et plugins Qt)"
windeployqt --release --no-translations --no-opengl-sw "${OUT_DIR}/${EXE}"

# windeployqt ne connaît que Qt. OpenCV, SDL2, FFmpeg, libacidcam et les
# runtimes GCC doivent être copiés à la main : on suit récursivement les
# dépendances que ldd rapporte, en ne gardant que celles de /mingw64.
echo "==> Copie des DLL non-Qt (OpenCV, SDL2, FFmpeg, libacidcam...)"
copy_deps() {
    local target="$1"
    local dep
    while read -r dep; do
        [ -z "${dep}" ] && continue
        local base
        base="$(basename "${dep}")"
        if [ ! -f "${OUT_DIR}/${base}" ]; then
            cp "${dep}" "${OUT_DIR}/"
            copy_deps "${OUT_DIR}/${base}"
        fi
    done < <(ldd "${target}" 2>/dev/null \
             | grep -i '/mingw64/bin/' \
             | awk '{print $3}' \
             | sort -u)
}
copy_deps "${OUT_DIR}/${EXE}"

echo
echo "==> Terminé."
echo "    ${OUT_DIR}"
echo "    $(find "${OUT_DIR}" -type f | wc -l) fichiers, $(du -sh "${OUT_DIR}" | cut -f1)"
echo
echo "Teste-le sur une machine sans MSYS2, puis zippe ce dossier et"
echo "attache-le à une Release GitHub."
