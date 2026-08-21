#include "wallpaperwindow.h"

#include <QGuiApplication>
#include <QQmlContext>
#include <QQuickItem>
#include <QRegion>
#include <QScreen>
#include <QUrl>

#include <QtWaylandClient/private/qwaylandscreen_p.h>

namespace {

// zwlr_layer_surface_v1 anchor bits, as the in-tree integration takes them.
constexpr uint AnchorTop = 1;
constexpr uint AnchorBottom = 2;
constexpr uint AnchorLeft = 4;
constexpr uint AnchorRight = 8;

} // namespace

WallpaperWindow::WallpaperWindow(const QString &screenName, QWindow *parent)
    : QQuickView(parent)
    , m_screenName(screenName)
{
    setColor(Qt::transparent);
    setFlags(Qt::FramelessWindowHint | Qt::WindowTransparentForInput);
    setTitle(QStringLiteral("kdock-wallpaper"));

    // Layer-shell properties must be in place before the platform window
    // exists, i.e. before show().
    setProperty("kdock.layershell", true);
    setProperty("kdock.keyboardInteractivity", 0u); // none
    // **This is a desktop, not a panel**, and saying so is what makes KWin's
    // Overview work: it looks for a window with `isDesktop()` to use as its
    // background, and it draws every `Dock` window as an overlay on top of
    // itself. Inheriting kdock's default "dock" scope gave a black Overview
    // background *and* a full-screen wallpaper flickering over the effect
    // (reported 2026-08-20, overview-error.jpg). See layershell.h.
    setProperty("kdock.scope", QStringLiteral("desktop"));
    applyLayerProperties();

    // The compositor decides the size here (we are anchored to all four edges),
    // which is the opposite of the dock, where the content decides.
    setResizeMode(QQuickView::SizeRootObjectToView);
    setSource(QUrl(QStringLiteral("qrc:/qml/Wallpaper.qml")));

    // Scenery, not UI: clicks must fall through. The flag above is the
    // mechanism (see header): QWaylandWindow::updateInputRegion maps it to an
    // empty wl_region. An empty QRegion via setMask would mean the opposite
    // (nullptr → whole surface) and the re-apply-after-show workaround could
    // never fix that.
}

void WallpaperWindow::applyLayerProperties()
{
    setProperty("kdock.anchors", AnchorTop | AnchorBottom | AnchorLeft | AnchorRight);
    setProperty("kdock.layer", 0u); // background
    // -1, not 0: "do not move me to accommodate anybody's exclusive zone". With
    // 0 the compositor would shrink the wallpaper by the height of every panel
    // on that output — including kdock's own dock.
    setProperty("kdock.exclusiveZone", -1);
    setProperty("kdock.margins", QVariant::fromValue(QMargins()));
}

void WallpaperWindow::updateScreen()
{
    QScreen *target = nullptr;
    const auto screens = QGuiApplication::screens();
    for (QScreen *s : screens) {
        if (s->name() == m_screenName) {
            target = s;
            break;
        }
    }
    if (!target)
        return;

    // Resolve the wl_output here and stash it, so the integration binds this
    // surface to exactly this output rather than guessing from
    // QWindow::screen() (which lags a destroy()+show() cycle). Same reasoning,
    // and same code, as DockWindow::updateScreen().
    quintptr outputPtr = 0;
    if (auto *qws = dynamic_cast<QtWaylandClient::QWaylandScreen *>(target->handle()))
        outputPtr = reinterpret_cast<quintptr>(qws->output());
    setProperty("kdock.output", QVariant::fromValue(outputPtr));

    const bool needsRecreate = !handle()
        || (outputPtr ? outputPtr != m_boundOutput : screen() != target);
    m_boundOutput = outputPtr;
    if (!needsRecreate)
        return;

    const bool wasVisible = isVisible();
    if (handle()) {
        destroy();
    }
    setScreen(target);
    setGeometry(target->geometry());
    if (wasVisible)
        show();
}

void WallpaperWindow::setImage(const QString &path, int fillMode)
{
    if (path == m_path && fillMode == m_fillMode)
        return;
    m_path = path;
    m_fillMode = fillMode;

    if (path.isEmpty()) {
        // Nothing configured for this monitor on this desktop: get out of the
        // way entirely instead of painting a black rectangle over whatever the
        // desktop had.
        hide();
        return;
    }

    if (QQuickItem *root = rootObject()) {
        root->setProperty("fillMode", fillMode);
        // The QML crossfades to it; setting the source is the whole API.
        root->setProperty("source", QUrl::fromLocalFile(path));
    }
    if (!isVisible()) {
        updateScreen();
        show();
    }
}
