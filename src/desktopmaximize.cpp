#include "desktopmaximize.h"

#include "dockconfig.h"
#include "virtualdesktops.h"
#include "windowmonitor.h"

#include <QCoreApplication>
#include <QDebug>
#include <QTimer>

namespace {

bool envDisabled()
{
    return qEnvironmentVariableIsSet("KDOCK_NO_WINDOW_ACTIONS");
}

} // namespace

DesktopMaximize::DesktopMaximize(VirtualDesktops *desktops, WindowMonitor *monitor,
                                 QObject *parent)
    : QObject(parent)
    , m_desktops(desktops)
    , m_monitor(monitor)
{
    if (!m_desktops || !m_monitor)
        return;

    connect(m_desktops, &VirtualDesktops::currentChanged, this,
            &DesktopMaximize::onDesktopChanged);

    // Track every window's maximized state as it changes, so we know which ones
    // were maximized *before* the desktop switch and should be restored after it.
    connect(m_monitor, &WindowMonitor::windowAdded, this, [this](AbstractWindow *w) {
        connect(w, &AbstractWindow::changed, this, &DesktopMaximize::onWindowChanged);
        if (w->maximized)
            m_maximized.insert(w);
    });
    connect(m_monitor, &WindowMonitor::windowRemoved, this, [this](AbstractWindow *w) {
        QObject::disconnect(w, &AbstractWindow::changed, this,
                            &DesktopMaximize::onWindowChanged);
        m_maximized.remove(w);
    });

    // Populate the set with windows already tracked before this object was
    // created (the window monitor may already have windows from startup).
    for (AbstractWindow *w : m_monitor->windows) {
        connect(w, &AbstractWindow::changed, this, &DesktopMaximize::onWindowChanged);
        if (w->maximized)
            m_maximized.insert(w);
    }
}

void DesktopMaximize::onDesktopChanged(int position)
{
    Q_UNUSED(position);
    if (envDisabled() || !DockConfig::maximizeWindowsOnDesktop())
        return;
    scheduleRemediation();
}

void DesktopMaximize::onWindowChanged()
{
    auto *w = qobject_cast<AbstractWindow *>(sender());
    if (!w)
        return;
    if (w->maximized && w->maximizable)
        m_maximized.insert(w);
    else
        m_maximized.remove(w);
}

void DesktopMaximize::scheduleRemediation()
{
    // The outgoing dock releases its exclusive zone asynchronously; the
    // compositor may not have re-laid out the work area yet. Retry at
    // increasing intervals to catch the moment it stabilises — reuse the
    // idiom from DockWindow (dockwindow.cpp:177).
    m_retryIndex = 0;
    m_retryTimer.disconnect();
    connect(&m_retryTimer, &QTimer::timeout, this, &DesktopMaximize::tryRemediate);
    m_retryTimer.setSingleShot(true);
    m_retryTimer.start(200);
}

void DesktopMaximize::tryRemediate()
{
    const bool force = (++m_retryIndex == 1); // first pass: always try the toggle
    const int position = m_desktops->currentPosition();
    if (position <= 0)
        return;
    const QString desktopId = m_desktops->currentDesktopId();

    bool anyAttempted = false;
    for (AbstractWindow *w : m_monitor->windows) {
        if (!shouldMaximize(w))
            continue;

        // Only windows that are on this desktop (or on all desktops) need
        // remediation. The "empty desktops = all desktops" convention is
        // documented in AbstractWindow.
        if (!w->desktops.isEmpty() && !w->desktops.contains(desktopId))
            continue;

        // The toggle cycle: KWin ignores set_state for a state the window
        // already has, so clear maximized first and re-set it. The two calls
        // are sent back-to-back; the compositor processes them as part of the
        // same frame and the window ends up maximized at its new geometry.
        if (force) {
            w->setMaximized(false);
            w->setMaximized(true);
        }
        anyAttempted = true;
    }

    // Staggered retries: the first pass unmaximizes and remaximizes; later
    // passes are for windows whose remaximized geometry may have been computed
    // against a stale work-area size. Three passes cover the worst case (two
    // docks of substantially different thickness swapping on the same output).
    if (anyAttempted || m_retryIndex < 3) {
        static const int delays[] = {200, 700, 1500};
        if (m_retryIndex < 3) {
            m_retryTimer.start(delays[m_retryIndex]);
            return;
        }
    }
}

bool DesktopMaximize::shouldMaximize(AbstractWindow *w) const
{
    if (!w)
        return false;
    if (!w->maximizable)
        return false;
    if (w->minimized)
        return false;
    if (w->skipTaskbar)
        return false;
    // A window without the ~maximized~ bit either was never maximized or lost
    // the flag to the work-area shrink — but if it was never maximized we
    // cannot tell the difference. Err on the side of restoration: any window
    // in the tracked set that is still maximizable needs the toggle, and the
    // first pass's set_state(MAXIMIZED,0) is a no-op for a window that was
    // never maximized anyway.
    return m_maximized.contains(w);
}

// --- Probe ----------------------------------------------------------------

int DesktopMaximize::runProbe(VirtualDesktops *desktops, WindowMonitor *monitor)
{
    if (!desktops || !monitor)
        return 1;

    QObject::connect(desktops, &VirtualDesktops::currentChanged, desktops,
                     [monitor, desktops] {
                         const int pos = desktops->currentPosition();
                         qDebug().noquote()
                             << "=== desktop" << pos
                             << desktops->nameOf(pos) << "===";
                         for (AbstractWindow *w : monitor->windows) {
                             qDebug().noquote()
                                 << " " << w->appId
                                 << "max:" << w->maximized
                                 << "maxbl:" << w->maximizable
                                 << "min:" << w->minimized
                                 << "skip:" << w->skipTaskbar
                                 << "transient:" << w->transient
                                 << "geom:" << w->geometry
                                 << "desktops:" << w->desktops;
                         }
                     });

    qDebug() << "DesktopMaximize probe: watching currentChanged, dump window"
             << "state on every switch. Press Ctrl+C to exit.";

    QCoreApplication::exec();
    return 0;
}
