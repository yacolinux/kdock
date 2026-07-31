#include "windowmonitor.h"

#include "plasmawindowmonitor.h"
#include "taskmanager.h"

WindowMonitor *WindowMonitor::create(QObject *parent)
{
    auto *wlr = new WlrBackend(parent);
    if (wlr->active())
        return wlr;
    delete wlr;

    auto *plasma = new PlasmaBackend(parent);
    if (plasma->active())
        return plasma;
    delete plasma;

    qWarning("kdock: compositor offers neither zwlr_foreign_toplevel_manager_v1 nor "
             "org_kde_plasma_window_management; running windows will not be shown.\n"
             "kdock: on KWin, make sure kdock.desktop (with X-KDE-Wayland-Interfaces) "
             "is installed in an applications/ dir.");
    return nullptr;
}

void WindowMonitor::registerWindow(AbstractWindow *window)
{
    windows.append(window);
    connect(window, &AbstractWindow::windowClosed, this, [this, window] {
        windows.removeOne(window);
        emit windowRemoved(window);
        window->deleteLater();
    });
    emit windowAdded(window);
}

void WindowMonitor::minimizeAllWindows()
{
    for (AbstractWindow *w : std::as_const(windows)) {
        if (!w->minimized && !w->skipTaskbar)
            w->minimize();
    }
}
