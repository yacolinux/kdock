#include "activewindowcontrol.h"

#include "virtualdesktops.h"
#include "windowmonitor.h"

ActiveWindowControl::ActiveWindowControl(WindowMonitor *monitor, VirtualDesktops *desktops,
                                         QObject *parent)
    : QObject(parent)
    , m_monitor(monitor)
    , m_desktops(desktops)
{
}

AbstractWindow *ActiveWindowControl::activeWindow() const
{
    if (!m_monitor)
        return nullptr;
    for (AbstractWindow *w : m_monitor->windows) {
        if (w && w->activated)
            return w;
    }
    return nullptr;
}

void ActiveWindowControl::closeActive()
{
    if (AbstractWindow *w = activeWindow())
        w->requestClose();
}

void ActiveWindowControl::sendActiveToNextDesktop()
{
    AbstractWindow *w = activeWindow();
    if (!w || !w->canChangeDesktop() || !m_desktops)
        return;

    const QStringList ids = m_desktops->desktopIds();
    if (ids.size() < 2) // Nowhere to send it.
        return;

    // The window we are moving is the active one, so it is on the current
    // desktop by definition: that is the one to leave. (A window pinned to all
    // desktops only loses the current one, which is the same thing KDE's task
    // manager does.)
    const QString current = m_desktops->currentDesktopId();
    int index = ids.indexOf(current);
    if (index < 0)
        index = 0;
    const QString next = ids.at((index + 1) % ids.size());

    w->moveToDesktop(next, current);
}
