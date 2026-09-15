/*
 * Acid Cam v2 - Qt/OpenCV Edition
 * Listing the machine's cameras.
 *
 * OpenCV has no enumeration API - cv::VideoCapture only takes an index - and
 * Qt Multimedia is not among this build's Qt modules, so on Windows the list
 * comes from DirectShow, which also gives the real device names. Everywhere
 * else (and if DirectShow fails) the indices are probed one by one and named
 * generically.
 */

#ifndef __CAMERA_ENUM_H__
#define __CAMERA_ENUM_H__

#include "qtheaders.h"

struct CameraDevice {
    int index;      // what cv::VideoCapture takes
    QString name;   // what the user reads
};

QVector<CameraDevice> enumerateCameras();

#endif
