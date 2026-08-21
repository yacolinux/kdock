// One full-screen wallpaper surface, on one monitor.
//
// Under LXQt nobody can give kdock the wallpapers it wants: PCManFM-Qt paints
// the desktop, and its own source says so — `// FIXME: support different
// wallpapers on different screen` (pcmanfm/application.cpp), plus it forces
// per-screen wallpapers off entirely under Wayland. So kdock paints them
// itself, on wlr-layer-shell surfaces of its own, exactly the way DockWindow
// does for the dock:
//
//   - **background layer**, so this sits at the very bottom of the stack;
//   - anchored to all four edges, which is how a layer surface asks to be the
//     size of the output;
//   - exclusive zone -1, i.e. "do not shrink me for anybody's panel";
//   - **no input region at all** (Qt::WindowTransparentForInput). Every click
//     has to fall through to whatever is underneath. Trap: QWindow::setMask
//     with an empty QRegion on Qt Wayland means set_input_region(nullptr) =
//     the whole surface (see QWaylandWindow::updateInputRegion). Only the
//     WindowTransparentForInput flag produces a genuinely empty wl_region.
//
// LxqtWallpapers owns one of these per monitor and tells it which image to
// show. A window with no image hides itself, so a monitor kdock has nothing
// configured for keeps showing whatever the desktop had.

#pragma once

#include <QQuickView>
#include <QString>

class WallpaperWindow : public QQuickView
{
    Q_OBJECT

public:
    explicit WallpaperWindow(const QString &screenName, QWindow *parent = nullptr);

    QString screenName() const { return m_screenName; }

    // Show `path` with `fillMode` (Plasma's org.kde.image FillMode numbering,
    // which is what the Wallpapers tab already stores). An empty path hides the
    // surface. Setting the same path twice is a no-op, so a re-apply on a
    // desktop switch does not flash.
    void setImage(const QString &path, int fillMode);
    QString image() const { return m_path; }

    // Re-resolve the target monitor and, if it moved, recreate the surface: a
    // layer surface is bound to its wl_output when it is created, so there is
    // no way to move one. Same reason DockWindow has to do this.
    void updateScreen();

private:
    void applyLayerProperties();

    QString m_screenName;
    QString m_path;
    int m_fillMode = -1;
    quintptr m_boundOutput = 0;
};
