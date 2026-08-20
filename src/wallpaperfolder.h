// "The images of a folder", and the step from one to the next.
//
// Two unrelated-looking features need exactly this and would otherwise each
// grow their own copy — with their own idea of which extensions count and of
// what "next" means at the end of the list: WallpaperControl advancing a
// static Plasma wallpaper, and LxqtWallpapers running its own slideshow.

#pragma once

#include <QString>
#include <QStringList>

namespace WallpaperFolder {

// Every image of `folders`, sorted case-insensitively. Not recursive: "the
// images of this folder" is what someone looking at a wallpaper folder expects,
// and a folder of folders would make the order impossible to predict.
QStringList images(const QStringList &folders);

// The image after `currentPath` in that set, wrapping around. Empty when there
// are no images at all; the first image when `currentPath` is not in the set
// (which is also the "nothing shown yet" case).
QString next(const QStringList &folders, const QString &currentPath);

// A random image of `folders` that is not in `recent`, which is the caller's
// most-recently-shown list, oldest first.
//
// Random rather than sequential because the alternative is what the slideshow
// used to do and it reads as broken: a folder walked in name order shows the
// same handful of pictures every session. And "not in `recent`" rather than
// "not the last one" because with a two-picture memory the eye still sees a
// ping-pong.
//
// Degrades instead of failing when the folder is too small to honour that:
// drops the oldest constraints one at a time, and a one-image folder returns
// that image. Empty only when there are no images at all.
QString randomOther(const QStringList &folders, const QStringList &recent);

} // namespace WallpaperFolder
