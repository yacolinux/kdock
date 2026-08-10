// KDE Slideshow Wallpaper control via D-Bus. Advances the slideshow wallpaper
// to the next image on a *specific* monitor: the KDE global shortcut only ever
// affects the primary screen, so instead we drive Plasma's scripting API
// (org.kde.PlasmaShell.evaluateScript) to target the containment on the dock's
// screen.
//
// How the advance is made matters more than it looks: on this Plasma 6, writing
// a concrete `Image` into the slideshow config and calling reloadConfig() DOES
// change the stored key and does NOT repaint (measured 2026-08-10 — three
// advances in a row moved the key and left the same wallpaper on screen). What
// repaints is flipping the containment's plugin to org.kde.image and, in a
// SEPARATE call, back to org.kde.slideshow + reloadConfig(): KDE then advances
// the slideshow itself, exactly like the desktop's own "Next Wallpaper". That
// is what this class does, and what next-wall.sh has been doing since 2026-07-16.
//
// A monitor showing a STATIC image (org.kde.image — which is what *Wallpapers
// per virtual desktop* leaves on desktops 2..N) has no slideshow to advance, so
// there "next" means the next file of the folder the current image lives in.
// That plugin, unlike the slideshow one, does honour writeConfig("Image") +
// reloadConfig() and repaints — measured, and the reason the two paths differ.

#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

class WallpaperControl : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available CONSTANT)

public:
    explicit WallpaperControl(QObject *parent = nullptr);

    bool available() const { return m_available; }

    // Advance the wallpaper on the given monitor (by connector name, e.g.
    // "DP-1"). Empty name falls back to the primary-only global shortcut.
    Q_INVOKABLE void nextWallpaper(const QString &screenName = QString());

private:
    void checkAvailability();
    // Primary-only fallback: invoke the KDE global shortcut.
    void invokeGlobalShortcut();
    // Advance the wallpaper of the containment at (x, y): cycle the plugin for
    // a slideshow, write the next file of the folder for a static image. No-op
    // for any other wallpaper plugin.
    void advanceForGeometry(int x, int y);
    // Pick the image after `currentPath` (sorted, wrapping) from `folders`.
    static QString nextImage(const QStringList &folders, const QString &currentPath);

    bool m_available = false;
};
