// Where the dock's surface ends up on screen, as pure arithmetic.
//
// Split out of DockWindow::dockRect() so it can be tested without a QQuickView,
// a compositor or an X server: the dodge mode's whole correctness lives here,
// and instantiating a dock window just to check a rectangle is both slow and
// impossible in CI (see tests/unit/tst_dockgeometry.cpp).

#pragma once

#include <QRect>
#include <QSize>

namespace kdock {

// `edge` and `alignment` take DockConfig::Edge / DockConfig::Alignment values;
// they are ints here so this header stays free of dockconfig.h.
//
// Start/End are exact: the surface sits against its corner with the margin in
// between, which is what applyLayerProperties() anchors it to. **Center is
// not**, and cannot be: the compositor centers the surface inside whatever the
// *other* exclusive zones leave free (measured ~90 px off on a session with a
// left dock), and a Wayland client is never told where its surface landed. So a
// centered dock reports the whole edge band — it dodges a window it doesn't
// quite touch, never the other way round.
inline QRect dockRectFor(const QRect &screen, int edge, int alignment, int margin,
                         const QSize &surface)
{
    if (screen.isEmpty() || surface.width() <= 0 || surface.height() <= 0)
        return {};

    // Mirrors DockConfig::Edge: 0 Bottom, 1 Top, 2 Left, 3 Right.
    const bool horizontal = (edge == 0 || edge == 1);
    const int m = margin;
    const int w = surface.width();
    const int h = surface.height();

    const int span = horizontal ? screen.width() : screen.height();
    int len = horizontal ? w : h;
    int along = horizontal ? screen.left() : screen.top();
    switch (alignment) {
    case 0: along += m; break;              // Start
    case 1: len = span; break;              // Center: the whole band, see above
    case 2: along += span - len - m; break; // End
    }
    const int bw = horizontal ? len : w; // band size, edge-relative
    const int bh = horizontal ? h : len;

    switch (edge) {
    case 0: return QRect(along, screen.bottom() + 1 - m - h, bw, bh); // Bottom
    case 1: return QRect(along, screen.top() + m, bw, bh);            // Top
    case 2: return QRect(screen.left() + m, along, bw, bh);           // Left
    case 3: return QRect(screen.right() + 1 - m - w, along, bw, bh);  // Right
    }
    return {};
}

} // namespace kdock
