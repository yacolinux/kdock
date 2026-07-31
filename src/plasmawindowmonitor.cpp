#include "plasmawindowmonitor.h"

PlasmaWindow::PlasmaWindow(struct ::org_kde_plasma_window *object, QObject *parent)
    : AbstractWindow(parent)
    , QtWayland::org_kde_plasma_window(object)
{
}

PlasmaWindow::~PlasmaWindow()
{
    if (QtWayland::org_kde_plasma_window::object())
        destroy();
}

void PlasmaWindow::activate()
{
    set_state(ORG_KDE_PLASMA_WINDOW_MANAGEMENT_STATE_ACTIVE,
              ORG_KDE_PLASMA_WINDOW_MANAGEMENT_STATE_ACTIVE);
}

void PlasmaWindow::minimize()
{
    set_state(ORG_KDE_PLASMA_WINDOW_MANAGEMENT_STATE_MINIMIZED,
              ORG_KDE_PLASMA_WINDOW_MANAGEMENT_STATE_MINIMIZED);
}

void PlasmaWindow::unminimize()
{
    set_state(ORG_KDE_PLASMA_WINDOW_MANAGEMENT_STATE_MINIMIZED, 0);
}

void PlasmaWindow::requestClose()
{
    close();
}

void PlasmaWindow::org_kde_plasma_window_title_changed(const QString &t)
{
    title = t;
    emit changed();
}

void PlasmaWindow::org_kde_plasma_window_app_id_changed(const QString &id)
{
    appId = id;
    emit changed();
}

void PlasmaWindow::org_kde_plasma_window_state_changed(uint32_t flags)
{
    activated = flags & ORG_KDE_PLASMA_WINDOW_MANAGEMENT_STATE_ACTIVE;
    minimized = flags & ORG_KDE_PLASMA_WINDOW_MANAGEMENT_STATE_MINIMIZED;
    skipTaskbar = flags & ORG_KDE_PLASMA_WINDOW_MANAGEMENT_STATE_SKIPTASKBAR;
    emit changed();
}

void PlasmaWindow::org_kde_plasma_window_initial_state()
{
    ready = true;
    emit initialStateDone();
}

void PlasmaWindow::org_kde_plasma_window_unmapped()
{
    emit windowClosed();
}

// ---------------------------------------------------------------------------

PlasmaWindowMonitor::PlasmaWindowMonitor(WindowMonitor *owner)
    : QWaylandClientExtensionTemplate<PlasmaWindowMonitor>(20)
    , m_owner(owner)
{
    initialize();
}

void PlasmaWindowMonitor::org_kde_plasma_window_management_window_with_uuid(uint32_t id,
                                                                            const QString &uuid)
{
    Q_UNUSED(id);
    wrapWindow(get_window_by_uuid(uuid));
}

void PlasmaWindowMonitor::org_kde_plasma_window_management_show_desktop_changed(uint32_t state)
{
    static_cast<PlasmaBackend *>(m_owner)->setShowDesktopActive(
        state == QtWayland::org_kde_plasma_window_management::show_desktop_enabled);
}

void PlasmaWindowMonitor::requestShowDesktop(bool show)
{
    show_desktop(show ? QtWayland::org_kde_plasma_window_management::show_desktop_enabled
                      : QtWayland::org_kde_plasma_window_management::show_desktop_disabled);
}

void PlasmaWindowMonitor::wrapWindow(struct ::org_kde_plasma_window *window)
{
    auto *w = new PlasmaWindow(window, m_owner);
    // Wait for the full initial burst before exposing the window
    connect(w, &PlasmaWindow::initialStateDone, m_owner,
            [this, w] { m_owner->registerWindow(w); });
}

PlasmaBackend::PlasmaBackend(QObject *parent)
    : WindowMonitor(parent)
    , m_extension(this)
{
}

void PlasmaBackend::toggleShowDesktop()
{
    // Optimistic; the compositor confirms via show_desktop_changed.
    m_extension.requestShowDesktop(!m_showDesktopActive);
}

void PlasmaBackend::setShowDesktopActive(bool active)
{
    if (m_showDesktopActive == active)
        return;
    m_showDesktopActive = active;
    emit showDesktopChanged();
}
