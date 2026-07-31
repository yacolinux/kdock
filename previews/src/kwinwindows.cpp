#include "kwinwindows.h"

KWinWindow::KWinWindow(struct ::org_kde_plasma_window *object, const QString &uuid, QObject *parent)
    : QObject(parent)
    , QtWayland::org_kde_plasma_window(object)
    , m_uuid(uuid)
{
}

KWinWindow::~KWinWindow()
{
    if (QtWayland::org_kde_plasma_window::object())
        destroy();
}

void KWinWindow::activate()
{
    // Order matters: on KWin a minimized window stays minimized if it is only
    // asked to become active.
    if (m_minimized)
        unminimize();
    set_state(ORG_KDE_PLASMA_WINDOW_MANAGEMENT_STATE_ACTIVE,
              ORG_KDE_PLASMA_WINDOW_MANAGEMENT_STATE_ACTIVE);
}

void KWinWindow::minimize()
{
    set_state(ORG_KDE_PLASMA_WINDOW_MANAGEMENT_STATE_MINIMIZED,
              ORG_KDE_PLASMA_WINDOW_MANAGEMENT_STATE_MINIMIZED);
}

void KWinWindow::unminimize()
{
    set_state(ORG_KDE_PLASMA_WINDOW_MANAGEMENT_STATE_MINIMIZED, 0);
}

void KWinWindow::requestClose()
{
    close();
}

void KWinWindow::org_kde_plasma_window_title_changed(const QString &title)
{
    if (m_title == title)
        return;
    m_title = title;
    emit metadataChanged();
}

void KWinWindow::org_kde_plasma_window_app_id_changed(const QString &appId)
{
    if (m_appId == appId)
        return;
    m_appId = appId;
    emit metadataChanged();
}

void KWinWindow::org_kde_plasma_window_themed_icon_name_changed(const QString &name)
{
    if (m_themedIconName == name)
        return;
    m_themedIconName = name;
    emit metadataChanged();
}

void KWinWindow::org_kde_plasma_window_state_changed(uint32_t flags)
{
    const bool active = flags & ORG_KDE_PLASMA_WINDOW_MANAGEMENT_STATE_ACTIVE;
    const bool minimized = flags & ORG_KDE_PLASMA_WINDOW_MANAGEMENT_STATE_MINIMIZED;
    const bool skipTaskbar = flags & ORG_KDE_PLASMA_WINDOW_MANAGEMENT_STATE_SKIPTASKBAR;
    if (active == m_active && minimized == m_minimized && skipTaskbar == m_skipTaskbar)
        return;
    m_active = active;
    m_minimized = minimized;
    m_skipTaskbar = skipTaskbar;
    emit stateChanged();
}

void KWinWindow::org_kde_plasma_window_geometry(int32_t x, int32_t y, uint32_t width,
                                                uint32_t height)
{
    const QRect r(x, y, int(width), int(height));
    if (m_geometry == r)
        return;
    m_geometry = r;
    emit geometryChanged();
}

void KWinWindow::org_kde_plasma_window_virtual_desktop_entered(const QString &id)
{
    if (m_desktops.contains(id))
        return;
    m_desktops.append(id);
    emit desktopsChanged();
}

void KWinWindow::org_kde_plasma_window_virtual_desktop_left(const QString &id)
{
    if (m_desktops.removeAll(id) > 0)
        emit desktopsChanged();
}

void KWinWindow::org_kde_plasma_window_initial_state()
{
    m_ready = true;
    emit initialStateDone();
}

void KWinWindow::org_kde_plasma_window_unmapped()
{
    emit closed();
}

// ---------------------------------------------------------------------------

KWinWindows::KWinWindows(QObject *parent)
    // Version 20 matches what kdock binds; get_window_by_uuid needs >= 12 and
    // window_with_uuid >= 13, both of which KWin 6 provides.
    : QWaylandClientExtensionTemplate<KWinWindows>(20)
{
    setParent(parent);
    initialize();
    if (!isActive()) {
        qWarning("kdock-previews: the compositor does not offer "
                 "org_kde_plasma_window_management; no windows will be listed.\n"
                 "kdock-previews: on KWin, the installed kdock-previews.desktop must carry "
                 "X-KDE-Wayland-Interfaces=org_kde_plasma_window_management and an Exec= with "
                 "this binary's absolute path.");
    }
}

void KWinWindows::org_kde_plasma_window_management_window_with_uuid(uint32_t id,
                                                                    const QString &uuid)
{
    Q_UNUSED(id);
    auto *w = new KWinWindow(get_window_by_uuid(uuid), uuid, this);

    // Only expose the window once its whole initial state burst has arrived:
    // before that its app id, geometry and desktops are all still empty, and a
    // card built from that would flicker into place.
    connect(w, &KWinWindow::initialStateDone, this, [this, w] {
        if (m_windows.contains(w))
            return;
        m_windows.append(w);
        emit windowAdded(w);
    });
    connect(w, &KWinWindow::closed, this, [this, w] {
        if (m_windows.removeOne(w))
            emit windowRemoved(w);
        w->deleteLater();
    });
}
