// Owns the dock instances. Up to DockConfig::kMaxDocksPerScreen docks may run
// on one monitor, each identified by a "dockId" (see DockConfig): slot 0 is the
// bare screen name, extra slots append "#<slot>". Docks are opt-in: the user
// enables a (monitor, slot) in SettingsDialog and its dockId is persisted in the
// shared config's "enabledScreens" list. On monitor hotplug and on
// enable/disable, sync() creates or destroys the corresponding DockWindow.
//
// Shared services (volume, clock, brightness, overview, window monitor, ...)
// are singletons passed to every dock's QML context. The relanzadores manager
// is attached only to the primary dock: the lowest-slot enabled dock on the
// primary monitor (see primaryDockId()). The systray host goes to every dock —
// which one draws the tray is its own "showSystray" flag, kept exclusive among
// docks that can be on screen together by systrayDockIdFor()/
// normalizeSystrayOwner().
//
// Docks are also filtered by KWin's current **virtual desktop**: each dock
// carries a (possibly empty) list of desktops in DockConfig::dockDesktops(),
// and wantedDocks() turns that into the set to show. Unlike a monitor going
// away, a dock the current desktop does not want is only **hidden**, never
// destroyed, and it is not built at all until the first time a desktop asks
// for it — that laziness is the point (a DockWindow carries a whole QML
// engine).

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
class ScreenBrightness;
class BatteryControl;
class OverviewControl;
class DesktopControl;
class MonitorControl;
class MaxMinControl;
class ActiveWindowControl;
class WallpaperControl;
class PowerControl;
class SystrayHost;
class RelanzadoresManager;
class ScriptRunnersManager;
class ClipboardHistory;
class DisksControl;
class NetworkControl;
class WeatherControl;
class AppearanceControl;
class VirtualDesktops;
class DesktopWallpapers;
class AutoColorScheme;

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
        // Per-monitor brightness (PowerDevil). Only the settings dialog uses it
        // directly; the dock widget goes through BrightnessControl, which picks
        // one display out of it.
        ScreenBrightness *screens = nullptr;
        BatteryControl *battery = nullptr;
        OverviewControl *overview = nullptr;
        DesktopControl *desktopControl = nullptr;
        MonitorControl *monitorControl = nullptr;
        MaxMinControl *maxmin = nullptr;
        ActiveWindowControl *activeWindow = nullptr;
        WallpaperControl *wallpaperControl = nullptr;
        PowerControl *power = nullptr;
        SystrayHost *systrayHost = nullptr;
        RelanzadoresManager *relanzadores = nullptr;
        ScriptRunnersManager *scriptRunners = nullptr;
        ClipboardHistory *clipboardHistory = nullptr;
        DisksControl *disks = nullptr;
        NetworkControl *network = nullptr;
        WeatherControl *weather = nullptr;
        AppearanceControl *appearance = nullptr;
        VirtualDesktops *desktops = nullptr;
        DesktopWallpapers *desktopWallpapers = nullptr;
        // ColorAuto. Injected after construction (setAutoColorScheme): it needs
        // the manager itself to reach the docks, so the two cannot both be
        // built with the other already in hand.
        AutoColorScheme *autoColors = nullptr;
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

    // --- Virtual desktops ---------------------------------------------------

    // Which enabled docks belong on a given 1-based virtual desktop. The rule,
    // and the only place it lives: **per monitor**, a desktop that has docks of
    // its own gets exactly those; a monitor with none falls back to its "base"
    // docks (the ones with an empty DockConfig::dockDesktops()). Desktop 0
    // ("KWin didn't answer" — X11, wlroots, the Xvfb harness) means every
    // desktop-bound dock is ignored and only the base set is wanted, i.e. what
    // kdock did before this existed. Disconnected monitors are excluded.
    // Not const: it reads each dock's desktops through configFor().
    QStringList wantedDocks(int desktop);

    // The virtual desktops the settings UI should offer, as display names
    // indexed by position-1. Empty when KWin is unreachable.
    QStringList desktopNamesForUi() const;
    // Current desktop (1-based), 0 when unknown.
    int currentDesktop() const;

    // Whether two docks can be on screen at the same time, i.e. whether their
    // desktop sets overlap (an empty set means "every desktop"). Used to scope
    // the systray's exclusivity: two docks that are never visible together may
    // both host a tray.
    bool canCoexist(const QString &dockIdA, const QString &dockIdB);

    // Create a dock with the factory defaults in the first free slot of a
    // monitor and enable it (right-click → Dock → Crear dock vacío). Unlike
    // duplicateDockForDesktop() nothing is copied: any config file left over
    // from a removed dock at that slot is deleted first. Returns the new
    // dockId, or an empty string (with *error set) when the monitor is full.
    QString createEmptyDock(const QString &screenName, QString *error = nullptr);

    // Clone srcDockId into a free slot on its own monitor, bound to the given
    // 1-based desktop, and enable it. Returns the new dockId, or an empty
    // string (with *error set) when the monitor is out of slots.
    QString duplicateDockForDesktop(const QString &srcDockId, int desktop,
                                    QString *error = nullptr);

    // Move a dock to the next connected monitor (wrapping around at the end of
    // the list). Reuses the Docks-tab machinery: copySettingsTo() clones the
    // config onto a free slot of the target monitor, then the old dock is
    // disabled, removed from knownDocks and its config file renamed to
    // "*.conf.tmp" (a leftover that can be reused by hand). The next call from
    // any dock first sweeps those .tmp files. Returns the new dockId, or an
    // empty string when there is only one monitor or the target has no free
    // slot.
    QString moveDockToNextMonitor(const QString &dockId);

    // Same, but the original stays where it is and enabled: the next monitor
    // gets a copy (right-click → Dock → Copiar a Sig. Monitor). The two share
    // every setting except the tray, which the copy gives up when it would be
    // on screen next to its source (see canCoexist). Returns the new dockId,
    // or empty on the same conditions as the move.
    QString copyDockToNextMonitor(const QString &dockId);

    // The dockId that defaults relanzadores to shown: the lowest-slot enabled
    // dock on the primary monitor.
    QString primaryDockId() const;

    // The dock that currently claims the system tray, or empty when no dock
    // does. Any dock can host it, but only one at a time *among docks that can
    // be on screen together* (the settings dialog disables the checkbox on the
    // others). Not const: it reads the other docks' configs through configFor().
    QString systrayDockId();
    // Same, restricted to the docks that would collide with `dockId` — i.e. the
    // owner the settings dialog has to point at when it greys out the checkbox.
    // Empty when `dockId` may claim the tray itself.
    QString systrayDockIdFor(const QString &dockId);

    // The item model of a shown dock, or nullptr when that dock isn't running.
    // The settings dialog reads the app icon names out of it to tint its tabs.
    DockModel *modelFor(const QString &dockId) const;

    // The window of a shown dock, or nullptr. An empty dockId asks for the
    // primary one, and falls back to any instance that exists — which is what
    // "open the settings dialog" means when the primary dock is currently
    // hidden by the virtual-desktop rule. Used by DockService.
    DockWindow *windowFor(const QString &dockId) const;

    // The DockConfig for a dock, created (and cached) on demand so the settings
    // dialog can edit a dock even before it is shown.
    DockConfig *configFor(const QString &dockId);

    // Global shared services. The settings dialog uses these regardless of
    // which dock opened it.
    RelanzadoresManager *relanzadores() const { return m_shared.relanzadores; }
    ScriptRunnersManager *scriptRunners() const { return m_shared.scriptRunners; }
    SystrayHost *systrayHost() const { return m_shared.systrayHost; }
    AudioControl *audio() const { return m_shared.audio; }
    // The three backends of the VideoEnergía tab (see SettingsDialog).
    BrightnessControl *brightness() const { return m_shared.brightness; }
    ScreenBrightness *screens() const { return m_shared.screens; }
    BatteryControl *battery() const { return m_shared.battery; }
    DesktopWallpapers *desktopWallpapers() const { return m_shared.desktopWallpapers; }
    AutoColorScheme *autoColorScheme() const { return m_shared.autoColors; }
    void setAutoColorScheme(AutoColorScheme *autoColors) { m_shared.autoColors = autoColors; }

signals:
    // Emitted whenever the set of enabled/known docks changes in a way that
    // affects what the settings dialog lists (enabledDocks/knownDocks).
    void dockListChanged();

private:
    struct Instance {
        DockModel *model = nullptr;
        SystrayModel *systrayModel = nullptr;
        // Per-dock clocks: the time format is a per-monitor config setting.
        ClockWidget *clock = nullptr;
        ClockWidget2 *clock2 = nullptr;
        DockWindow *window = nullptr;
        bool primary = false;
        // False while the current virtual desktop doesn't want this dock: the
        // instance stays built (and keeps its QML engine) but its surface is
        // unmapped. See sync().
        bool onScreen = true;
    };

    void migrateFirstRun();
    // Body of moveDockToNextMonitor()/copyDockToNextMonitor(): the two differ
    // only in what happens to the source once the copy is in place.
    QString cloneToNextMonitor(const QString &dockId, bool keepSource);
    // Drops every systray claim but one (see the .cpp): configs written while
    // the tray was primary-only may flag several docks.
    void normalizeSystrayOwner();
    void sync();
    // Map/unmap an existing dock's surface without destroying the instance.
    void setInstanceOnScreen(const QString &dockId, bool onScreen);
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
