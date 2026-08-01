// Owns the dock instances. Up to DockConfig::kMaxDocksPerScreen docks may run
// on one monitor, each identified by a "dockId" (see DockConfig): slot 0 is the
// bare screen name, extra slots append "#<slot>". Docks are opt-in: the user
// enables a (monitor, slot) in SettingsDialog and its dockId is persisted in the
// shared config's "enabledScreens" list. On monitor hotplug and on
// enable/disable, sync() creates or destroys the corresponding DockWindow.
//
// Shared services (volume, clock, brightness, overview, window monitor, ...)
// are singletons passed to every dock's QML context. The systray host and the
// relanzadores manager are attached only to the primary dock: the lowest-slot
// enabled dock on the primary monitor (see primaryDockId()).

#pragma once

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>

class DockConfig;
class DockModel;
class DockWindow;
class SystrayModel;
class Theme;
class DesktopEntryIndex;
class WindowMonitor;
class VolumeControl;
class AudioControl;
class ClockWidget;
class ClockWidget2;
class BrightnessControl;
class BatteryControl;
class OverviewControl;
class DesktopControl;
class MonitorControl;
class MaxMinControl;
class WallpaperControl;
class PowerControl;
class SystrayHost;
class RelanzadoresManager;
class ScriptRunnersManager;
class ClipboardHistory;
class DisksControl;
class NetworkControl;
class AppearanceControl;

class DockManager : public QObject
{
    Q_OBJECT
public:
    struct Shared {
        Theme *theme = nullptr;
        DesktopEntryIndex *apps = nullptr;
        WindowMonitor *monitor = nullptr;
        VolumeControl *volume = nullptr;
        AudioControl *audio = nullptr;
        BrightnessControl *brightness = nullptr;
        BatteryControl *battery = nullptr;
        OverviewControl *overview = nullptr;
        DesktopControl *desktopControl = nullptr;
        MonitorControl *monitorControl = nullptr;
        MaxMinControl *maxmin = nullptr;
        WallpaperControl *wallpaperControl = nullptr;
        PowerControl *power = nullptr;
        SystrayHost *systrayHost = nullptr;
        RelanzadoresManager *relanzadores = nullptr;
        ScriptRunnersManager *scriptRunners = nullptr;
        ClipboardHistory *clipboardHistory = nullptr;
        DisksControl *disks = nullptr;
        NetworkControl *network = nullptr;
        AppearanceControl *appearance = nullptr;
    };

    explicit DockManager(const Shared &shared, QObject *parent = nullptr);

    // Names of all currently connected monitors.
    QStringList connectedScreens() const;
    bool isDockEnabled(const QString &dockId) const;
    // Enable/disable a specific dock (persists + syncs live). When enabling a
    // brand-new slot, its config is seeded with a free screen edge.
    void setDockEnabled(const QString &dockId, bool enabled);

    // Enabled dockIds on a given screen, ordered by slot.
    QStringList enabledDocksForScreen(const QString &screenName) const;

    // All docks the settings UI should list: enabled plus every dock ever
    // configured (DockConfig::knownDocks), ordered by screen then slot.
    QStringList configuredDocks() const;

    // Monitors the settings UI should offer: persisted known screens, currently
    // connected screens, and screens referenced by configured docks.
    QStringList knownScreensForUi() const;

    // First unused slot (0..kMaxDocksPerScreen-1) on a screen, or -1 if the
    // screen already has the maximum number of configured docks.
    int firstFreeSlot(const QString &screenName) const;

    // Live preview: show a temporary, non-persisted copy of srcDockId on
    // targetScreen (a real dock window if the screen is connected). Nothing is
    // written to the enabled/known lists until applyPreviews() is called; a
    // preview is torn down by unpreviewScreen()/clearPreviews() leaving no
    // trace. Returns false (with *error) if the target screen is full.
    bool previewDockOnScreen(const QString &srcDockId, const QString &targetScreen,
                             QString *error = nullptr);
    // Remove a single pending preview of srcDockId on targetScreen.
    void unpreviewScreen(const QString &srcDockId, const QString &targetScreen);
    // Discard every pending preview (called when the dialog closes or the
    // selected dock changes without applying).
    void clearPreviews();
    // Commit the pending previews of srcDockId: copy config becomes permanent
    // and each target dock is enabled. The new docks appear in configuredDocks.
    void applyPreviews(const QString &srcDockId);
    // Whether a pending preview of srcDockId exists on a given screen / at all.
    bool hasPreview(const QString &srcDockId, const QString &screen) const;
    bool hasPreviewsFor(const QString &srcDockId) const;

    // Stop showing a dock and drop it from the settings list, keeping its config
    // file on disk so it can be recreated later.
    void removeDock(const QString &dockId);

    // The dockId that hosts systray and defaults relanzadores to shown: the
    // lowest-slot enabled dock on the primary monitor.
    QString primaryDockId() const;

    // The item model of a shown dock, or nullptr when that dock isn't running.
    // The settings dialog reads the app icon names out of it to tint its tabs.
    DockModel *modelFor(const QString &dockId) const;

    // The DockConfig for a dock, created (and cached) on demand so the settings
    // dialog can edit a dock even before it is shown.
    DockConfig *configFor(const QString &dockId);

    // Global shared services. The settings dialog uses these regardless of
    // which dock opened it.
    RelanzadoresManager *relanzadores() const { return m_shared.relanzadores; }
    ScriptRunnersManager *scriptRunners() const { return m_shared.scriptRunners; }
    SystrayHost *systrayHost() const { return m_shared.systrayHost; }
    AudioControl *audio() const { return m_shared.audio; }

private:
    struct Instance {
        DockModel *model = nullptr;
        SystrayModel *systrayModel = nullptr;
        // Per-dock clocks: the time format is a per-monitor config setting.
        ClockWidget *clock = nullptr;
        ClockWidget2 *clock2 = nullptr;
        DockWindow *window = nullptr;
        bool primary = false;
    };

    void migrateFirstRun();
    void sync();
    Instance buildInstance(const QString &dockId, bool primary);
    void teardownInstance(Instance &inst);
    void createInstance(const QString &dockId, bool primary);
    void destroyInstance(const QString &dockId);
    // First free slot (0..kMaxDocksPerScreen-1) on a screen, also excluding
    // slots taken by pending previews; -1 if none.
    int firstFreeSlotWithPreviews(const QString &screenName) const;
    void dropPreview(const QString &dstDockId, bool deleteFileIfCreated);
    QString primaryScreenName() const;

    Shared m_shared;
    QHash<QString, DockConfig *> m_configs;   // cached, keyed by dockId
    QHash<QString, Instance> m_instances;     // only for shown docks, by dockId
    // Pending previews, keyed by the prospective dockId on the target screen.
    QHash<QString, QString> m_previewSource;  // dstDockId -> srcDockId
    QHash<QString, Instance> m_previews;      // live preview windows (connected)
    QSet<QString> m_previewCreatedFile;       // dstDockIds whose file we created
};
