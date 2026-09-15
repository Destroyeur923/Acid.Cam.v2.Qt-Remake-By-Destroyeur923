/*
 * Acid Cam v2 - Qt/OpenCV Edition
 * The Aide / À propos dialogs shared by the Photo, Montage V2 and WebCam
 * windows.
 *
 * Each workspace works differently enough to need its own help page, so the
 * topic picks which one is shown. Every page exists in French and English,
 * side by side in two tabs.
 */

#ifndef __HELP_ABOUT_H__
#define __HELP_ABOUT_H__

#include "qtheaders.h"

enum class HelpTopic {
    Photo,
    Montage,
    Webcam
};

// Modeless: the help can stay open next to the window it describes.
void showHelpDialog(QWidget *parent, HelpTopic topic);

// The original Acid Cam credits, plus who added these three workspaces.
void showAboutDialog(QWidget *parent);

#endif
