// The dock surface itself: a QQuickView flagged as a layer-shell window.
// Owns the mapping from DockConfig to layer-shell properties and the
// input-mask trick used for autohide.

#pragma once

#include <QQuickView>

class DockConfig;
class DockModel;
class Theme;
class SettingsDialog;
class DesktopEntryIndex;
class BrightnessControl;
class BatteryControl;
class VolumeControl;
class ClockWidget;
class ClockWidget2;
class OverviewControl;
class DesktopControl;
class MonitorControl;
class MaxMinControl;
class ActiveWindowControl;
class WallpaperControl;
class PowerControl;
class DockManager;
class SystrayModel;
class SystrayHost;
class RelanzadoresManager;
class ScriptRunnersManager;
class ClipboardHistory;
class DisksControl;
class NetworkControl;
class AppearanceControl;
class WindowMonitor;
class TileMenuLauncher;
class VirtualDesktops;

class DockWindow : public QQuickView
{
    Q_OBJECT
public:
    DockWindow(DockConfig *config, Theme *theme, DockModel *model, DesktopEntryIndex *apps,
               VolumeControl *volume, ClockWidget *clock, ClockWidget2 *clock2,
               BrightnessControl *brightness, BatteryControl *battery,
                OverviewControl *overview,
                DesktopControl *desktopControl, MonitorControl *monitorControl,
                MaxMinControl *maxmin, ActiveWindowControl *activeWindow,
                WallpaperControl *wallpaperControl, PowerControl *power,
                SystrayModel *systrayModel, SystrayHost *systrayHost = nullptr,
               RelanzadoresManager *relanzadores = nullptr,
               ScriptRunnersManager *scriptRunners = nullptr,
               ClipboardHistory *clipboardHistory = nullptr,
               DisksControl *disks = nullptr, NetworkControl *network = nullptr,
               AppearanceControl *appearance = nullptr,
               WindowMonitor *monitor = nullptr,
               VirtualDesktops *desktops = nullptr);

    // Called from QML when the autohide animation finishes: shrinks the
    // input region to a thin strip on the screen edge so clicks pass
    // through to the windows below.
    Q_INVOKABLE void setHidden(bool hidden);

    // Map or unmap the surface because the current virtual desktop wants (or
    // no longer wants) this dock. Everything above the surface — the QML
    // engine, the model, the clocks — stays alive, so coming back is cheap;
    // see DockManager::sync().
    void setDeskVisible(bool visible);

    // The manager backing the settings dialog's per-monitor selector.
    void setManager(DockManager *manager) { m_manager = manager; }

    // Whether this is the primary dock (hosts systray; relanzadores default to
    // shown here). Exposed to QML as the `dockIsPrimary` context property.
    void setPrimary(bool primary);

    Q_INVOKABLE void openSettings();
    // Open the settings dialog on the Docks tab, selecting this dock's
    // row (right-click → Dock → Nombre).
    Q_INVOKABLE void openSettingsToDock();
    // Ask for confirmation and, if accepted, remove this dock entirely
    // (right-click → Dock → Borrar este Dock).
    Q_INVOKABLE void deleteDock();
    // Create a second dock with the default settings on this monitor and open
    // the settings dialog on it (right-click → Dock → Crear dock vacío).
    Q_INVOKABLE void createEmptyDock();
    // Move this dock to the next connected monitor (right-click → Dock →
    // Mover Sig. Monitor). The old config is renamed to .tmp; see
    // DockManager::moveDockToNextMonitor().
    Q_INVOKABLE void moveToNextMonitor();
    // Open the settings dialog directly on the Audio tab (volume widget
    // right-click).
    Q_INVOKABLE void openAudioSettings();
    // Same, on the Redes tab (network widget right-click).
    Q_INVOKABLE void openNetworkSettings();
    Q_INVOKABLE void quit();
    // Relaunch kdock with the same CLI arguments, then quit this instance.
    Q_INVOKABLE void restart();

    // Toggle keyboard focus for the layer surface (and its popups). The dock
    // is normally keyboard-inert ("none"); widgets that need text input (the
    // app menu search) switch it to "exclusive" while open.
    Q_INVOKABLE void setKeyboardInteractive(bool on);

private:
    void applyLayerProperties();
    // Recreate the layer surface on the configured output (or the primary
    // one). applyScreen() does it synchronously; scheduleApplyScreen()
    // coalesces bursts of screen hotplug/config events into a single
    // deferred call so the dock isn't torn down and rebuilt repeatedly.
    void applyScreen();
    void scheduleApplyScreen();
    // (Re)apply the autohide input mask for the current m_hidden state. Split
    // out of setHidden() because remapping the surface needs it too, and
    // setHidden() returns early when the flag didn't change.
    void applyHiddenMask();
    int thickness() const;

    DockConfig *m_config;
    Theme *m_theme;
    DockModel *m_model;
    DesktopEntryIndex *m_apps;
    SystrayHost *m_systrayHost;
    RelanzadoresManager *m_relanzadores;
    ScriptRunnersManager *m_scriptRunners = nullptr;
    ClipboardHistory *m_clipboardHistory = nullptr;
    WindowMonitor *m_monitor = nullptr;
    SettingsDialog *m_dialog = nullptr;
    DockManager *m_manager = nullptr;
    OverviewControl *m_overview = nullptr;
    DesktopControl *m_desktopControl = nullptr;
    MonitorControl *m_monitorControl = nullptr;
    MaxMinControl *m_maxmin = nullptr;
    ActiveWindowControl *m_activeWindow = nullptr;
    WallpaperControl *m_wallpaperControl = nullptr;
    PowerControl *m_power = nullptr;
    AppearanceControl *m_appearance = nullptr;
    VirtualDesktops *m_desktops = nullptr;
    // Drives the separate kdock-tilemenu process (context property
    // "tileLauncher"); owned by this window.
    TileMenuLauncher *m_tileLauncher = nullptr;
    bool m_hidden = false;
    bool m_primary = false;
    bool m_screenChangePending = false;
    // wl_output the current layer surface was bound to (as a raw pointer value).
    // Tracked here because QWindow::screen() is unreliable right after a
    // layer-surface recreation and would otherwise wedge the dock on one output.
    quintptr m_boundOutput = 0;
};
