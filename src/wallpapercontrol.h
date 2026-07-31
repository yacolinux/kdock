// KDE Slideshow Wallpaper control via D-Bus. Advances the slideshow wallpaper
// to the next image on a *specific* monitor: the KDE global shortcut only ever
// affects the primary screen, so instead we drive Plasma's scripting API
// (org.kde.PlasmaShell.evaluateScript) to target the containment on the dock's
// screen, computing the next image ourselves from the slideshow folders.

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
    // Fire-and-forget evaluateScript on plasmashell.
    void runPlasmaScript(const QString &script);
    // Primary-only fallback: invoke the KDE global shortcut.
    void invokeGlobalShortcut();
    // Read the target screen's slideshow config, compute the next image and
    // write it back — all scoped to the containment at (x, y).
    void advanceForGeometry(int x, int y);
    // Pick the image after `currentPath` (sorted, wrapping) from `folders`.
    static QString nextImage(const QStringList &folders, const QString &currentPath);

    bool m_available = false;
};
