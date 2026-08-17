#include "plasmawindowmonitor.h"

PlasmaWindow::PlasmaWindow(struct ::org_kde_plasma_window *object, const QString &id,
                           QObject *parent)
    : AbstractWindow(parent)
    , QtWayland::org_kde_plasma_window(object)
{
    uuid = id;
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

void PlasmaWindow::maximize()
{
    set_state(ORG_KDE_PLASMA_WINDOW_MANAGEMENT_STATE_MAXIMIZED,
              ORG_KDE_PLASMA_WINDOW_MANAGEMENT_STATE_MAXIMIZED);
}

void PlasmaWindow::setMaximized(bool on)
{
    set_state(ORG_KDE_PLASMA_WINDOW_MANAGEMENT_STATE_MAXIMIZED,
              on ? ORG_KDE_PLASMA_WINDOW_MANAGEMENT_STATE_MAXIMIZED : 0);
}

void PlasmaWindow::requestClose()
{
    close();
}

void PlasmaWindow::moveToDesktop(const QString &enterId, const QString &leaveId)
{
    // Enter first, leave second: a window that belongs to *no* desktop is how
    // the protocol spells "on all desktops", so dropping the old one first
    // would flash the window onto every desktop.
    if (!enterId.isEmpty())
        request_enter_virtual_desktop(enterId);
    if (!leaveId.isEmpty() && leaveId != enterId)
        request_leave_virtual_desktop(leaveId);
}

void PlasmaWindow::moveToOnlyDesktop(const QString &enterId)
{
    if (enterId.isEmpty())
        return;
    // Enter first (see moveToDesktop), then drop everything else. A window on
    // all desktops has an empty list, so it is just the enter — which is
    // exactly right: the compositor narrows it down to the one we asked for.
    request_enter_virtual_desktop(enterId);
    const QStringList previous = desktops;
    for (const QString &id : previous) {
        if (id != enterId)
            request_leave_virtual_desktop(id);
    }
}

void PlasmaWindow::org_kde_plasma_window_virtual_desktop_entered(const QString &id)
{
    if (!id.isEmpty() && !desktops.contains(id)) {
        desktops.append(id);
        emit changed();
    }
}

void PlasmaWindow::org_kde_plasma_window_virtual_desktop_left(const QString &id)
{
    if (desktops.removeAll(id) > 0)
        emit changed();
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
    maximized = flags & ORG_KDE_PLASMA_WINDOW_MANAGEMENT_STATE_MAXIMIZED;
    maximizable = flags & ORG_KDE_PLASMA_WINDOW_MANAGEMENT_STATE_MAXIMIZABLE;
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

void PlasmaWindow::org_kde_plasma_window_parent_window(struct ::org_kde_plasma_window *parent)
{
    transient = (parent != nullptr);
}

void PlasmaWindow::org_kde_plasma_window_geometry(int32_t x, int32_t y, uint32_t width, uint32_t height)
{
    geometry = QRect(x, y, static_cast<int>(width), static_cast<int>(height));
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
    // The uuid is the only handle the screenshot API accepts, and this event is
    // the only place it is ever sent: it has to be kept now or not at all.
    wrapWindow(get_window_by_uuid(uuid), uuid);
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

void PlasmaWindowMonitor::wrapWindow(struct ::org_kde_plasma_window *window, const QString &uuid)
{
    auto *w = new PlasmaWindow(window, uuid, m_owner);
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
