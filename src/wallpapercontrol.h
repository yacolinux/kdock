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
    // Ask KDE for the next image of the slideshow on the containment at
    // (x, y), by cycling its wallpaper plugin. No-op when that containment is
    // not running a slideshow.
    void advanceForGeometry(int x, int y);

    bool m_available = false;
};
