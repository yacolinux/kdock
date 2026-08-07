// Rellena el hueco que DockManager::sync() deja al mostrar el dock entrante
// antes de ocultar el saliente (2026-08-07): durante ese instante los dos docks
// reservan zona exclusiva sobre el mismo output, KWin achica el work-area y las
// maximizadas no se recuperan. Esta clase re-maximiza las ventanas que ya lo
// estaban, con reintentos escalonados, después de cada cambio de escritorio.
//
// Enganchado a VirtualDesktops::currentChanged, igual que DesktopWallpapers.
// No viaja en Shared — ningún dock lo usa.

#pragma once

#include <QObject>
#include <QSet>
#include <QTimer>

class AbstractWindow;
class VirtualDesktops;
class WindowMonitor;

class DesktopMaximize : public QObject
{
    Q_OBJECT

public:
    explicit DesktopMaximize(VirtualDesktops *desktops, WindowMonitor *monitor,
                             QObject *parent = nullptr);

    // Seeded by the --probe-maximize CLI; dumps every window's state on
    // currentChanged and returns immediately (no docks, no event loop).
    static int runProbe(VirtualDesktops *desktops, WindowMonitor *monitor);

private slots:
    void onDesktopChanged(int position);
    void onWindowChanged();

private:
    void scheduleRemediation();
    void tryRemediate();
    bool shouldMaximize(AbstractWindow *w) const;

    VirtualDesktops *m_desktops;
    WindowMonitor *m_monitor;
    QSet<AbstractWindow *> m_maximized;
    QTimer m_retryTimer;
    int m_retryIndex = 0;
};
