// In-tree wlr-layer-shell-v1 client integration for Qt Wayland.
// Pure Qt: uses QtWaylandClient private API + our own protocol bindings,
// no KDE Frameworks / layer-shell-qt involved.
//
// Windows opt in by setting the dynamic property "kdock.layershell" = true
// before the platform window is created. Everything else (dialogs, menus,
// popups) is delegated to the stock xdg-shell integration.
//
// Supported window properties (read at creation, updated live on change):
//   kdock.layershell            bool   use layer-shell for this window
//   kdock.anchors               uint   zwlr anchor bitmask (top1 bottom2 left4 right8)
//   kdock.exclusiveZone         int    reserved screen space (like X11 struts)
//   kdock.layer                 uint   0 background, 1 bottom, 2 top, 3 overlay
//   kdock.margins               QMargins
//   kdock.keyboardInteractivity uint   0 none, 1 exclusive, 2 on-demand
//   kdock.scope                 QString layer-shell namespace; "" = "dock"
//
// **kdock.scope is not cosmetic and cannot be changed after creation.** The
// namespace is how a compositor decides what *kind* of surface this is: KWin
// maps it straight onto a window type (`scopeToType` in layershellv1window.cpp)
// — "desktop" → Desktop, "dock" → Dock, anything else → Normal — and that
// decides whether the Overview effect uses the surface as the desktop
// background or draws it as a panel overlay on top of itself. See
// WallpaperWindow, which is the one surface here that is a desktop and not a
// panel; everything else leaves it at the default.

#pragma once

#include <QtWaylandClient/private/qwaylandshellintegration_p.h>
#include <QtWaylandClient/private/qwaylandshellsurface_p.h>
#include <QtWaylandClient/private/qwaylandwindow_p.h>

#include "qwayland-wlr-layer-shell-unstable-v1.h"

#include <memory>

class LayerShellIntegration
    : public QtWaylandClient::QWaylandShellIntegrationTemplate<LayerShellIntegration>,
      public QtWayland::zwlr_layer_shell_v1
{
public:
    LayerShellIntegration();
    ~LayerShellIntegration() override;

    bool initialize(QtWaylandClient::QWaylandDisplay *display) override;
    QtWaylandClient::QWaylandShellSurface *createShellSurface(QtWaylandClient::QWaylandWindow *window) override;

private:
    std::unique_ptr<QtWaylandClient::QWaylandShellIntegration> m_xdgFallback;
};

class LayerSurface : public QtWaylandClient::QWaylandShellSurface,
                     public QtWayland::zwlr_layer_surface_v1
{
    Q_OBJECT
public:
    LayerSurface(LayerShellIntegration *shell, QtWaylandClient::QWaylandWindow *window);
    ~LayerSurface() override;

    bool isExposed() const override { return m_configured; }
    void applyConfigure() override;
    void setWindowGeometry(const QRect &rect) override;
    void attachPopup(QtWaylandClient::QWaylandShellSurface *popup) override;
    std::any surfaceRole() const override;

    bool eventFilter(QObject *watched, QEvent *event) override;

protected:
    void zwlr_layer_surface_v1_configure(uint32_t serial, uint32_t width, uint32_t height) override;
    void zwlr_layer_surface_v1_closed() override;

private:
    void applyProperty(const QByteArray &name);
    void sendSize(const QSize &size);
    QSize m_pendingSize;
    uint m_layer = 2; // top
    bool m_configured = false;
};
