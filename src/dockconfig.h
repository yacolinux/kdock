// Persistent dock settings (QSettings-backed), exposed to QML and to the
// widgets settings dialog.

#pragma once

#include <QColor>
#include <QHash>
#include <QObject>
#include <QSettings>
#include <QStringList>
#include <QUrl>

// Process-wide "the dark mode changed" ping. DockConfig::darkModeChanged() is
// per instance (one per dock); this one fires once, which is what the system
// appearance side effects need — applying a KDE color scheme fifteen times is
// fifteen plasma-apply-colorscheme processes.
class DarkModeNotifier : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    void ping() { emit changed(); }
signals:
    void changed();
};

class DockConfig : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int edge READ edge WRITE setEdge NOTIFY edgeChanged)
    Q_PROPERTY(int iconSize READ iconSize WRITE setIconSize NOTIFY iconSizeChanged)
    Q_PROPERTY(int widgetIconScale READ widgetIconScale WRITE setWidgetIconScale NOTIFY widgetIconScaleChanged)
    Q_PROPERTY(int widgetIconSize READ widgetIconSize NOTIFY widgetIconSizeChanged)
    Q_PROPERTY(int spacing READ spacing WRITE setSpacing NOTIFY spacingChanged)
    Q_PROPERTY(int screenMargin READ screenMargin WRITE setScreenMargin NOTIFY screenMarginChanged)
    Q_PROPERTY(bool autohide READ autohide WRITE setAutohide NOTIFY autohideChanged)
    Q_PROPERTY(int hideMode READ hideMode WRITE setHideMode NOTIFY hideModeChanged)
    Q_PROPERTY(qreal opacity READ opacity WRITE setOpacity NOTIFY opacityChanged)
    Q_PROPERTY(QColor panelColor READ panelColor WRITE setPanelColor NOTIFY panelColorChanged)
    Q_PROPERTY(bool panelColorSet READ panelColorSet NOTIFY panelColorChanged)
    Q_PROPERTY(QStringList panelPresetColors READ panelPresetColors WRITE setPanelPresetColors NOTIFY panelPresetColorsChanged)
    Q_PROPERTY(QString panelImage READ panelImage WRITE setPanelImage NOTIFY panelImageChanged)
    Q_PROPERTY(QUrl panelImageUrl READ panelImageUrl NOTIFY panelImageChanged)
    Q_PROPERTY(QStringList pinned READ pinned WRITE setPinned NOTIFY pinnedChanged)
    Q_PROPERTY(QString screenName READ screenName WRITE setScreenName NOTIFY screenNameChanged)
    Q_PROPERTY(bool panelMode READ panelMode WRITE setPanelMode NOTIFY panelModeChanged)
    Q_PROPERTY(bool compact READ compact WRITE setCompact NOTIFY compactChanged)
    Q_PROPERTY(int alignment READ alignment WRITE setAlignment NOTIFY alignmentChanged)
    Q_PROPERTY(bool showAppIcons READ showAppIcons WRITE setShowAppIcons NOTIFY showAppIconsChanged)
    Q_PROPERTY(bool showVolume READ showVolume WRITE setShowVolume NOTIFY showVolumeChanged)
    Q_PROPERTY(bool showSystray READ showSystray WRITE setShowSystray NOTIFY showSystrayChanged)
    Q_PROPERTY(int systrayIconScale READ systrayIconScale WRITE setSystrayIconScale NOTIFY systrayIconScaleChanged)
    Q_PROPERTY(int systrayIconSize READ systrayIconSize NOTIFY systrayIconSizeChanged)
    Q_PROPERTY(QStringList systrayHiddenItems READ systrayHiddenItems WRITE setSystrayHiddenItems NOTIFY systrayHiddenItemsChanged)
    Q_PROPERTY(QStringList relanzadoresHidden READ relanzadoresHidden WRITE setRelanzadoresHidden NOTIFY relanzadoresHiddenChanged)
    Q_PROPERTY(QStringList relanzadoresShown READ relanzadoresShown WRITE setRelanzadoresShown NOTIFY relanzadoresShownChanged)
    Q_PROPERTY(QStringList scriptRunnersHidden READ scriptRunnersHidden WRITE setScriptRunnersHidden NOTIFY scriptRunnersHiddenChanged)
    Q_PROPERTY(QStringList scriptRunnersShown READ scriptRunnersShown WRITE setScriptRunnersShown NOTIFY scriptRunnersShownChanged)
    Q_PROPERTY(int effectiveMargin READ effectiveMargin NOTIFY effectiveMarginChanged)
    Q_PROPERTY(bool showBrightness READ showBrightness WRITE setShowBrightness NOTIFY showBrightnessChanged)
    Q_PROPERTY(bool showBattery READ showBattery WRITE setShowBattery NOTIFY showBatteryChanged)
    Q_PROPERTY(bool showAutohideToggle READ showAutohideToggle WRITE setShowAutohideToggle NOTIFY showAutohideToggleChanged)
    Q_PROPERTY(int separatorSize READ separatorSize WRITE setSeparatorSize NOTIFY separatorSizeChanged)
    Q_PROPERTY(int separator1 READ separator1 WRITE setSeparator1 NOTIFY separator1Changed)
    Q_PROPERTY(int separator2 READ separator2 WRITE setSeparator2 NOTIFY separator2Changed)
    // A transparent separator keeps its separatorSize px of room between two app
    // icons but draws no line. Unlike the "gap" section token, it does not punch
    // a hole in the panel: the background stays painted behind it.
    Q_PROPERTY(bool separator1Transparent READ separator1Transparent
               WRITE setSeparator1Transparent NOTIFY separator1TransparentChanged)
    Q_PROPERTY(bool separator2Transparent READ separator2Transparent
               WRITE setSeparator2Transparent NOTIFY separator2TransparentChanged)
    Q_PROPERTY(bool showClock READ showClock WRITE setShowClock NOTIFY showClockChanged)
    Q_PROPERTY(bool clockFormat24h READ clockFormat24h WRITE setClockFormat24h NOTIFY clockFormat24hChanged)
    Q_PROPERTY(bool clockShowDate READ clockShowDate WRITE setClockShowDate NOTIFY clockShowDateChanged)
    Q_PROPERTY(QString clock2Command READ clock2Command WRITE setClock2Command NOTIFY clock2CommandChanged)
    Q_PROPERTY(bool clockShowSeconds READ clockShowSeconds WRITE setClockShowSeconds NOTIFY clockShowSecondsChanged)
    Q_PROPERTY(int clockFontSize READ clockFontSize WRITE setClockFontSize NOTIFY clockFontSizeChanged)
    Q_PROPERTY(bool showDesktopButton READ showDesktopButton WRITE setShowDesktopButton NOTIFY showDesktopButtonChanged)
    Q_PROPERTY(bool iconRunningBackground READ iconRunningBackground WRITE setIconRunningBackground NOTIFY iconRunningBackgroundChanged)
    Q_PROPERTY(bool iconRunningDots READ iconRunningDots WRITE setIconRunningDots NOTIFY iconRunningDotsChanged)
    Q_PROPERTY(bool iconRunningLine READ iconRunningLine WRITE setIconRunningLine NOTIFY iconRunningLineChanged)
    Q_PROPERTY(bool showMenuButton READ showMenuButton WRITE setShowMenuButton NOTIFY showMenuButtonChanged)
    Q_PROPERTY(bool showTileMenu READ showTileMenu WRITE setShowTileMenu NOTIFY showTileMenuChanged)
    Q_PROPERTY(QString tileMenuIcon READ tileMenuIcon WRITE setTileMenuIcon NOTIFY tileMenuIconChanged)
    Q_PROPERTY(bool showControlManager READ showControlManager WRITE setShowControlManager NOTIFY showControlManagerChanged)
    Q_PROPERTY(QString controlManagerIcon READ controlManagerIcon WRITE setControlManagerIcon NOTIFY controlManagerIconChanged)
    // 0 icon only, 1 icon + text, 2 text only.
    Q_PROPERTY(int controlManagerDisplay READ controlManagerDisplay WRITE setControlManagerDisplay NOTIFY controlManagerDisplayChanged)
    Q_PROPERTY(QString controlManagerText READ controlManagerText WRITE setControlManagerText NOTIFY controlManagerTextChanged)
    Q_PROPERTY(QString controlManagerFormat READ controlManagerFormat WRITE setControlManagerFormat NOTIFY controlManagerFormatChanged)
    // Font of the Control Manager widget's own text, in px. 0 = automatic,
    // which follows the clock font when that is set and the icon size
    // otherwise. Independent from the app/widget names (whose font is
    // iconLabelFontSize and only applies while some name is shown): the CM text
    // is the one piece of dock text that exists without any label mode on.
    Q_PROPERTY(int controlManagerFontSize READ controlManagerFontSize WRITE setControlManagerFontSize NOTIFY controlManagerFontSizeChanged)
    Q_PROPERTY(bool showSessionButton READ showSessionButton WRITE setShowSessionButton NOTIFY showSessionButtonChanged)
    Q_PROPERTY(bool showSettingsButton READ showSettingsButton WRITE setShowSettingsButton NOTIFY showSettingsButtonChanged)
    Q_PROPERTY(bool showMenuPower READ showMenuPower WRITE setShowMenuPower NOTIFY showMenuPowerChanged)
    Q_PROPERTY(QString menuIcon READ menuIcon WRITE setMenuIcon NOTIFY menuIconChanged)
    Q_PROPERTY(int menuPopupWidth READ menuPopupWidth WRITE setMenuPopupWidth NOTIFY menuPopupWidthChanged)
    Q_PROPERTY(int menuPopupHeight READ menuPopupHeight WRITE setMenuPopupHeight NOTIFY menuPopupHeightChanged)
    Q_PROPERTY(int menuColumns READ menuColumns WRITE setMenuColumns NOTIFY menuColumnsChanged)
    Q_PROPERTY(int menuAppIconSize READ menuAppIconSize WRITE setMenuAppIconSize NOTIFY menuAppIconSizeChanged)
    Q_PROPERTY(int menuGridSpacing READ menuGridSpacing WRITE setMenuGridSpacing NOTIFY menuGridSpacingChanged)
    Q_PROPERTY(QString menuEditorApp READ menuEditorApp WRITE setMenuEditorApp NOTIFY menuEditorAppChanged)
    Q_PROPERTY(bool showClipboard READ showClipboard WRITE setShowClipboard NOTIFY showClipboardChanged)
    Q_PROPERTY(int clipboardPopupWidth READ clipboardPopupWidth WRITE setClipboardPopupWidth NOTIFY clipboardPopupWidthChanged)
    Q_PROPERTY(int clipboardPopupHeight READ clipboardPopupHeight WRITE setClipboardPopupHeight NOTIFY clipboardPopupHeightChanged)
    Q_PROPERTY(bool showDisks READ showDisks WRITE setShowDisks NOTIFY showDisksChanged)
    Q_PROPERTY(bool showNetwork READ showNetwork WRITE setShowNetwork NOTIFY showNetworkChanged)
    Q_PROPERTY(bool showWeather READ showWeather WRITE setShowWeather NOTIFY showWeatherChanged)
    // KDE appearance pickers (icon theme / color scheme), see AppearanceControl.
    Q_PROPERTY(bool showIconThemes READ showIconThemes WRITE setShowIconThemes NOTIFY showIconThemesChanged)
    Q_PROPERTY(bool showColorSchemes READ showColorSchemes WRITE setShowColorSchemes NOTIFY showColorSchemesChanged)
    Q_PROPERTY(bool showOverview READ showOverview WRITE setShowOverview NOTIFY showOverviewChanged)
    Q_PROPERTY(bool showClock2 READ showClock2 WRITE setShowClock2 NOTIFY showClock2Changed)
    Q_PROPERTY(bool showMoveToDesktop READ showMoveToDesktop WRITE setShowMoveToDesktop NOTIFY showMoveToDesktopChanged)
    Q_PROPERTY(bool showMoveToScreen READ showMoveToScreen WRITE setShowMoveToScreen NOTIFY showMoveToScreenChanged)
    Q_PROPERTY(bool showMaxMin READ showMaxMin WRITE setShowMaxMin NOTIFY showMaxMinChanged)
    Q_PROPERTY(bool showCloseWindow READ showCloseWindow WRITE setShowCloseWindow NOTIFY showCloseWindowChanged)
    Q_PROPERTY(bool showNextWallpaper READ showNextWallpaper WRITE setShowNextWallpaper NOTIFY showNextWallpaperChanged)
    Q_PROPERTY(bool showDarkMode READ showDarkMode WRITE setShowDarkMode NOTIFY showDarkModeChanged)
    Q_PROPERTY(bool showPager READ showPager WRITE setShowPager NOTIFY showPagerChanged)
    // Dark mode: an *override* of the normal color scheme, never a rewrite of
    // it. See darkModeActive() — the "Normal" colors stay in the .conf exactly
    // where the user left them, so turning dark mode off restores them by
    // construction (no snapshot, no restore).
    Q_PROPERTY(bool darkMode READ darkMode WRITE setDarkMode NOTIFY darkModeChanged)
    Q_PROPERTY(bool darkModeActive READ darkModeActive NOTIFY darkModeChanged)
    Q_PROPERTY(QColor darkAccent READ darkAccent NOTIFY darkModeChanged)
    Q_PROPERTY(QColor darkBackground READ darkBackground NOTIFY darkModeChanged)
    Q_PROPERTY(bool groupWindows READ groupWindows WRITE setGroupWindows NOTIFY groupWindowsChanged)
    Q_PROPERTY(bool showTooltips READ showTooltipsProp NOTIFY showTooltipsChanged)
    Q_PROPERTY(QStringList menuFavorites READ menuFavorites WRITE setMenuFavorites NOTIFY menuFavoritesChanged)
    Q_PROPERTY(QStringList widgetOrder READ widgetOrder WRITE setWidgetOrder NOTIFY widgetOrderChanged)
    Q_PROPERTY(int dockLength READ dockLength WRITE setDockLength NOTIFY dockLengthChanged)
    Q_PROPERTY(int widgetIconThemeMode READ widgetIconThemeMode WRITE setWidgetIconThemeMode NOTIFY widgetIconThemeChanged)
    Q_PROPERTY(QString widgetIconThemeLightBg READ widgetIconThemeLightBg WRITE setWidgetIconThemeLightBg NOTIFY widgetIconThemeChanged)
    Q_PROPERTY(QString widgetIconThemeDarkBg READ widgetIconThemeDarkBg WRITE setWidgetIconThemeDarkBg NOTIFY widgetIconThemeChanged)
    // App-icon labels (see IconLabelMode) and the geometry derived from them.
    Q_PROPERTY(int iconLabelMode READ iconLabelMode WRITE setIconLabelMode NOTIFY iconLabelModeChanged)
    Q_PROPERTY(int iconLabelWidth READ iconLabelWidth WRITE setIconLabelWidth NOTIFY iconLabelWidthChanged)
    Q_PROPERTY(int effectiveLabelWidth READ effectiveLabelWidth NOTIFY dockThicknessChanged)
    Q_PROPERTY(int iconLabelFontSize READ iconLabelFontSize WRITE setIconLabelFontSize NOTIFY iconLabelFontSizeChanged)
    Q_PROPERTY(int iconLabelFontPx READ iconLabelFontPx NOTIFY dockThicknessChanged)
    Q_PROPERTY(int iconLabelLineHeight READ iconLabelLineHeight NOTIFY dockThicknessChanged)
    // How many lines a name may use, 1 or 2. With 2 a name longer than the box
    // wraps instead of being elided, so long titles can be read whole; the box
    // is that many line heights tall, which is why it moves the thickness.
    // Shared by the app and the widget names, like the width and the font size.
    Q_PROPERTY(int labelLines READ labelLines WRITE setLabelLines NOTIFY labelLinesChanged)
    Q_PROPERTY(int iconLabelBoxHeight READ iconLabelBoxHeight NOTIFY dockThicknessChanged)
    Q_PROPERTY(int iconLabelGap READ iconLabelGap NOTIFY dockThicknessChanged)
    // Same idea for the non-app sections (widgets and blocks), configured apart
    // from the apps: a dock can name its widgets and not its apps, or vice versa.
    Q_PROPERTY(int widgetLabelMode READ widgetLabelMode WRITE setWidgetLabelMode NOTIFY widgetLabelModeChanged)
    // Draws every name the dock shows (apps and sections) in bold. The clocks
    // are bold already, so this only ever adds weight.
    Q_PROPERTY(bool labelBold READ labelBold WRITE setLabelBold NOTIFY labelBoldChanged)
    // Bumped on every rename; QML reads it inside the bindings that call
    // widgetName() so a custom name repaints (same trick as theme.revision).
    Q_PROPERTY(int widgetNamesRevision READ widgetNamesRevision NOTIFY widgetNamesChanged)
    // Shrink every icon (apps, widgets, systray) when the sections no longer
    // fit along the dock; see the "auto-shrink" block in Dock.qml.
    Q_PROPERTY(bool autoShrinkIcons READ autoShrinkIcons WRITE setAutoShrinkIcons NOTIFY autoShrinkIconsChanged)
    Q_PROPERTY(int autoShrinkMinIconSize READ autoShrinkMinIconSize WRITE setAutoShrinkMinIconSize NOTIFY autoShrinkMinIconSizeChanged)
    Q_PROPERTY(int dockThickness READ dockThickness NOTIFY dockThicknessChanged)
    // User-chosen friendly name for this dock (empty = the default
    // "<screen> — Dock <n>" derived from the dockId). Persisted per-dock so a
    // copy onto another monitor starts unnamed (see DockManager::previewDockOnScreen).
    Q_PROPERTY(QString alias READ alias WRITE setAlias NOTIFY aliasChanged)

public:
    enum Edge { Bottom = 0, Top = 1, Left = 2, Right = 3 };
    Q_ENUM(Edge)
    enum Alignment { Start = 0, Center = 1, End = 2 };
    Q_ENUM(Alignment)

    // How the dock shares the screen edge with the windows below it. The old
    // `autohide` boolean is the AutoHide value of this enum and is still
    // exposed (widget toggle, tooltips) as a shortcut for it.
    //   AlwaysVisible  reserves the strut, windows never cover the dock.
    //   AutoHide       slides away unless the pointer is over it; no strut.
    //   DodgeWindows   visible while nothing overlaps it; hides when a window
    //                  of the current desktop reaches its rectangle.
    //   WindowsBelow   always visible, but no strut: maximized windows extend
    //                  under the dock.
    enum HideMode { AlwaysVisible = 0, AutoHide = 1, DodgeWindows = 2, WindowsBelow = 3 };
    Q_ENUM(HideMode)

    // Which icon set the standard widget icons (volume, network, session…) are
    // taken from. Monochrome themed icons are drawn in a color meant for a
    // given background, so a dark icon set on a light panel is unreadable.
    enum WidgetIconTheme {
        FollowIconTheme = 0, // no override: whatever Theme set globally
        MatchDockColor  = 1, // pick by the dock background's luminance
        AlwaysLightBg   = 2, // always the "light background" set (dark icons)
        AlwaysDarkBg    = 3, // always the "dark background" set (light icons)
    };
    Q_ENUM(WidgetIconTheme)

    // Whether (and where) a name is drawn next to a dock icon. Used by two
    // independent settings: iconLabelMode for the "apps" block (launchers +
    // running windows) and widgetLabelMode for every other section. The icon
    // keeps its size in every mode: the label is laid out around a fixed icon
    // box. Values are persisted, so the four original ones keep their numbers.
    // LabelOnly is meaningful for apps only; widgets clamp it back to IconOnly.
    enum IconLabelMode {
        IconOnly   = 0, // default: exactly the pre-label appearance
        LabelBelow = 1, // name under the icon
        LabelRight = 2, // name at the right of the icon
        LabelOnly  = 3, // name only, no icon
        LabelAbove = 4, // name over the icon
        LabelLeft  = 5, // name at the left of the icon
    };
    Q_ENUM(IconLabelMode)

    // Human-readable name of a section token ("volume" -> "Volume"), used by
    // the Layout tab and as the default label drawn under/next to a widget.
    // See widgetName() for the user-renamed variant.
    static QString defaultWidgetLabel(const QString &token);

    // Ordered dock sections. Widget tokens plus zero or more of the repeatable
    // tokens below. See reconcileWidgetOrder().
    static QStringList knownWidgetTokens(); // apps + every widget, no separators

    // Tokens that may appear any number of times in widgetOrder (every other
    // token is unique and comes from knownWidgetTokens()): the dynamic
    // separator, which expands; the static one, a separatorSize px gap; and the
    // transparent one, which expands *and* cuts the panel background so the
    // desktop shows through (see gapRects()).
    static bool isRepeatableToken(const QString &token)
    {
        return token == QLatin1String("spring") || token == QLatin1String("sep")
               || token == QLatin1String("gap");
    }
    // A separator that expands to fill the leftover room. The transparent one
    // does it too, which is why several places have to test for both.
    static bool isSpringToken(const QString &token)
    {
        return token == QLatin1String("spring") || token == QLatin1String("gap");
    }

    // Up to this many docks can be configured on a single monitor. Six rather
    // than the original three because a monitor may now hold its base dock(s)
    // plus one set per virtual desktop (see dockDesktops()).
    static constexpr int kMaxDocksPerScreen = 6;

    // How many virtual desktops the dock offers to bind docks to. Fixed on
    // purpose: the UI lists at most this many, whatever KWin reports.
    static constexpr int kMaxDesktops = 5;

    // A dock is identified by a "dockId": the first dock on a screen uses the
    // bare screen name (slot 0, backward compatible); extra docks append
    // "#<slot>" (e.g. "HDMI-1#1"). "#" is used because output names use "-".
    static QString makeDockId(const QString &screenName, int slot);
    static QString screenOfDockId(const QString &dockId);
    static int slotOfDockId(const QString &dockId);

    // Absolute path of the shared INI settings file, under the XDG data dir
    // (~/.local/share/kdock/kdock.conf). Both DockConfig and the relanzadores
    // manager persist through this same file. On first call it migrates a
    // legacy ~/.config/kdock/kdock.conf if the new file does not exist yet.
    static QString settingsFilePath();

    // Per-dock settings file. Slot 0 keeps the legacy per-monitor name
    // (~/.local/share/kdock/kdock-DP-1.conf); extra slots append the slot
    // (kdock-DP-1-1.conf). Each dock persists independently. Global data
    // (relanzadores) stays in the shared file above.
    static QString instanceSettingsFilePath(const QString &dockId);

    // Which docks the user has enabled (opt-in). Persisted as a string list of
    // dockIds under "enabledScreens" in the shared settings file (key kept for
    // backward compatibility; slot-0 ids are bare screen names).
    static QStringList enabledDocks();
    static void setDockEnabled(const QString &dockId, bool enabled);

    // Monitors ever seen by the config, persisted under "knownScreens" so the
    // settings UI can offer previously-connected (but currently unplugged)
    // outputs. addKnownScreen() is idempotent.
    static QStringList knownScreens();
    static void addKnownScreen(const QString &screenName);

    // Docks ever configured (enabled at least once, or duplicated onto a
    // monitor), persisted under "knownDocks" so a disabled dock still shows up
    // in the settings list. addKnownDock() is idempotent.
    static QStringList knownDocks();
    static void addKnownDock(const QString &dockId);
    static void removeKnownDock(const QString &dockId);

    // Whether the menu favorites are shared across every dock/instance. When on,
    // favorites live in the shared settings file (under "sharedMenuFavorites")
    // instead of each dock's own "menuFavorites". Stored under "shareFavorites".
    static bool favoritesShared();
    // Toggle the shared-favorites mode for the whole app. On enable, the shared
    // list is seeded from the first live config if it's still empty. Re-syncs
    // every live DockConfig in the process and emits menuFavoritesChanged().
    static void setFavoritesShared(bool shared);

    // Whether the dock should re-maximize windows that were maximized before a
    // virtual-desktop switch (work-around for the work-area shrink when the
    // incoming and outgoing docks briefly coexist on the same output). Default
    // ON. Disable with KDOCK_NO_WINDOW_ACTIONS=1 for test harnesses.
    static bool maximizeWindowsOnDesktop();
    static void setMaximizeWindowsOnDesktop(bool on);

    // Master switch for the dock's tooltips (General tab): when off, no
    // ToolTip shows on any element of any dock. Shared setting, not per dock.
    static bool showTooltips();
    static void setShowTooltips(bool on);

    // ---- Dark mode (app-wide part) ----------------------------------------
    // Breeze Dark's two colors, copied into the app on purpose: the dark scheme
    // must not shift under the dock when the user edits their KDE color scheme,
    // so nothing here is read from kdeglobals at runtime.
    static constexpr const char *kDarkAccentDefault     = "#3daee9";
    static constexpr const char *kDarkBackgroundDefault = "#232629";

    // Scope of the dark-mode switch: whether turning it on/off from anywhere
    // acts on every dock or only on the one it was clicked from. A persistent
    // *preference*, deliberately independent of the on/off state below — it
    // stays put when the mode is turned off, so turning it back on still
    // reaches every dock. Stored in the shared settings file, like
    // shareFavorites.
    static bool darkModeAllDocks();
    static void setDarkModeAllDocks(bool on);
    // On/off state while the scope is app-wide (the per-dock flag is darkMode()).
    static bool darkModeGlobal();
    static void setDarkModeGlobal(bool on);
    // Is any dock rendering dark right now? What the system-wide appearance
    // switches below key off, since a KDE color scheme is not per monitor.
    static bool anyDarkModeActive();

    // Optional side effects of the mode: things outside the dock's own drawing
    // that dark mode can also flip. Unlike the dock's colors these cannot be
    // overridden at read time — they are global state — so each one carries the
    // value for *both* modes and switching applies the matching one.
    enum DarkAppearanceItem {
        SystemColorScheme = 0, // KDE color scheme (plasma-apply-colorscheme)
        SystemIconTheme   = 1, // KDE icon theme (plasma-changeicons)
        DockIconTheme     = 2, // kdock's own icon-theme override (Theme)
    };
    static bool darkAppearanceEnabled(int item);
    static void setDarkAppearanceEnabled(int item, bool on);
    // dark == true: the value applied while the mode is on; false: the one
    // restored when it goes off.
    static QString darkAppearanceValue(int item, bool dark);
    static void setDarkAppearanceValue(int item, bool dark, const QString &value);
    // What the system had right before dark mode went on, captured by
    // DarkModeAppearance. An empty "normal" value means "put this back" — that
    // is what the dialog's "(volver al anterior)" entry stands for. Persisted,
    // for the same reason darkAppearanceApplied() is: the restore has to work
    // even if the dock restarts while the mode is on. Not defined for
    // DockIconTheme, where an empty id already means something else.
    static QString darkAppearancePrevious(int item);
    static void setDarkAppearancePrevious(int item, const QString &value);
    // The id kdock itself last pushed to the system, in either direction. It is
    // what makes the snapshot above trustworthy: the tools that apply a scheme
    // or an icon theme are startDetached, so re-reading the system right after
    // a switch can still return the *previous* value — and storing that as "what
    // was there before" poisons the normal mode for good. A live value equal to
    // this one is ours, not the user's choice, so it is never snapshotted.
    static QString darkAppearanceSelfApplied(int item);
    static void setDarkAppearanceSelfApplied(int item, const QString &value);
    // Whether the side effects above are currently applied. Persisted, because
    // it is the state of the *system*, not of this process: without it a
    // restart would re-apply them and overwrite nothing, or skip the restore.
    static bool darkAppearanceApplied();
    static void setDarkAppearanceApplied(bool applied);

    // Fires once per dark-mode change, for the things that must react a single
    // time instead of once per dock (the appearance side effects). The
    // per-instance darkModeChanged() is what QML and the dialog use.
    static DarkModeNotifier *darkModeNotifier();
    // dockIds left in normal mode while darkModeAllDocks() is on.
    static QStringList darkModeExceptions();
    static void setDarkModeExceptions(const QStringList &dockIds);
    // The one accent color (label text + running-app highlight) and the dock
    // background used while dark mode is on. App-wide, not per dock.
    static QColor darkAccentColor();
    static void setDarkAccentColor(const QColor &color);
    static QColor darkBackgroundColor();
    static void setDarkBackgroundColor(const QColor &color);

    // Like favoritesShared(), but for the menu *appearance/behavior* group
    // (menu icon, popup width/height, power row, columns). Stored under
    // "shareMenuConfig"; when on, those keys live in the shared settings file.
    static bool menuConfigShared();
    // Toggle shared-menu-config for the whole app. On enable, seeds the shared
    // group from the first live config for keys not yet present. Re-syncs every
    // live DockConfig and emits each affected *Changed() signal.
    static void setMenuConfigShared(bool shared);

    explicit DockConfig(QObject *parent = nullptr);
    // Bind this config to a specific dock's settings file. The bound output is
    // derived from the dockId's screen part.
    explicit DockConfig(const QString &dockId, QObject *parent = nullptr);
    ~DockConfig() override;

    // The dockId this config belongs to (screen name for slot 0; empty for the
    // legacy single-instance ctor). Distinct from screenName(), which is the
    // bound output shared by all slots on a monitor.
    QString dockId() const { return m_dockId; }

    // Friendly display name: the alias when set, else the default
    // "<screen> — Dock <slot+1>".
    QString alias() const { return m_alias; }
    void setAlias(const QString &alias);

    // Virtual desktops this dock belongs to, as 1-based positions ("Escritorio
    // 1" == 1), persisted as "desktops=" in the dock's own file. An **empty**
    // list is the default and means "base dock": shown on every desktop that
    // has no dock of its own on this monitor (see DockManager::wantedDocks,
    // which is the only place that rule lives). Not exposed to QML: the dock
    // draws nothing out of it, only DockManager and the settings dialog care.
    QList<int> dockDesktops() const { return m_dockDesktops; }
    void setDockDesktops(const QList<int> &desktops);

    // Copy this dock's persisted settings into dstDockId's settings file, so a
    // new dock can start from an existing one's configuration. Copies the live
    // in-memory state (a freshly enabled dock may not have flushed every key to
    // disk yet), binds the copy to its own output, and deliberately skips the
    // alias — the name belongs to the original, a copy starts unnamed. Returns
    // false if the destination cannot be written.
    bool copySettingsTo(const QString &dstDockId) const;

    int edge() const { return m_edge; }
    int iconSize() const { return m_iconSize; }
    int widgetIconScale() const { return m_widgetIconScale; }
    // Effective icon size for widget sections (not launchers/relanzadores):
    // iconSize scaled by widgetIconScale%, clamped to a legible minimum.
    int widgetIconSize() const { return qMax(16, qRound(m_iconSize * m_widgetIconScale / 100.0)); }
    // See WidgetIconTheme. The two theme ids name real icon sets designed for a
    // light and a dark background respectively (Breeze / Breeze Dark by
    // default); QML picks one and passes it to the image://icon provider.
    int widgetIconThemeMode() const { return m_widgetIconThemeMode; }
    QString widgetIconThemeLightBg() const { return m_widgetIconThemeLightBg; }
    QString widgetIconThemeDarkBg() const { return m_widgetIconThemeDarkBg; }
    // See IconLabelMode. Width is the label box: a cap on a horizontal dock
    // (the cell shrinks to the name) and a fixed width on a vertical one (so
    // the dock does not change thickness from one app to another). Font size
    // 0 = derived from the icon size.
    int iconLabelMode() const { return m_iconLabelMode; }
    int iconLabelWidth() const { return m_iconLabelWidth; }
    // Width the name box actually gets. iconLabelWidth is a *cap*, not a size:
    // on a vertical dock every cell shares one box width, and using the cap
    // there left a band of dead dock to the right of the longest name (bug
    // 2026-07-30, error-texto-horiz.jpeg). So QML measures the widest name it
    // draws (off-screen, at the unscaled font) and reports it here; the cap
    // still wins when a name is longer than it, and the name is then elided as
    // before.
    int effectiveLabelWidth() const
    {
        // < 0: nothing measured yet (the very first frame) -> the cap, i.e. the
        // historic behaviour, which errs wide and so never clips a name.
        // == 0: no name is being drawn at all (labels off, or dropped by the
        // auto-shrink) -> no box, so the dock shrinks to its icons instead of
        // reserving room for names nobody can see.
        if (m_measuredLabelWidth < 0)
            return m_iconLabelWidth;
        return qMin(m_measuredLabelWidth, m_iconLabelWidth);
    }
    // Reported by Dock.qml (0 = no names drawn). Not persisted: it is a
    // measurement of what is on screen right now, not a setting.
    Q_INVOKABLE void setMeasuredLabelWidth(int px);
    int iconLabelFontSize() const { return m_iconLabelFontSize; }
    // Resolved label metrics. QML gives its label Text this exact pixel size
    // and line height, so the geometry below is not an estimate.
    int iconLabelFontPx() const;
    int iconLabelLineHeight() const;
    int labelLines() const { return m_labelLines; }
    // Height the name box gets: one line, or two when a name is allowed to wrap.
    int iconLabelBoxHeight() const { return iconLabelLineHeight() * m_labelLines; }
    int iconLabelGap() const { return m_compact ? 2 : 4; } // icon <-> label
    // See IconLabelMode. Width and font size are shared with the app labels.
    int widgetLabelMode() const { return m_widgetLabelMode; }
    bool labelBold() const { return m_labelBold; }
    // Icons shrink (down to autoShrinkMinIconSize) instead of overflowing the
    // dock when the sections do not fit. Without it the layout piles the
    // sections that do not fit on top of each other.
    bool autoShrinkIcons() const { return m_autoShrinkIcons; }
    int autoShrinkMinIconSize() const { return m_autoShrinkMinIconSize; }
    int widgetNamesRevision() const { return m_widgetNamesRevision; }
    // Name drawn for a section: the user's rename if there is one, otherwise the
    // active translation, otherwise defaultWidgetLabel(). Q_INVOKABLE because
    // QML asks per token.
    Q_INVOKABLE QString widgetName(const QString &token) const;
    // Empty (or equal to the default) clears the rename.
    Q_INVOKABLE void setWidgetName(const QString &token, const QString &name);
    // The name a section would get without any rename: translation, else
    // capabase. Used by the dialog to show what "empty = default" means.
    static QString translatedWidgetLabel(const QString &token);
    // Language changed: every live config re-emits widgetNamesChanged so the
    // QML bindings that read widgetName() are evaluated again.
    static void retranslate();
    // Cross-axis extent (px) of one app cell, label included; == iconSize in
    // IconOnly mode.
    int appCellThickness() const;
    // Geometry shared by both label settings; see the .cpp.
    int cellThicknessFor(int mode, int iconPx) const;
    // Same for a widget section. Its icon is smaller than an app's, but blocks
    // (systray, relanzadores…) can draw taller content, so the icon size stays
    // the floor.
    int widgetCellThickness() const;
    // Thickness of the whole dock: the single source of truth shared by
    // Dock.qml (root.thickness) and DockWindow::thickness() (exclusive zone).
    int dockThickness() const;
    int spacing() const { return m_spacing; }
    int screenMargin() const { return m_screenMargin; }
    // Kept as the shortcut it always was: "the dock hides on its own when the
    // pointer leaves it". The dodge mode hides too, but on a different trigger,
    // so it is not folded in here.
    bool autohide() const { return m_hideMode == AutoHide; }
    int hideMode() const { return m_hideMode; }
    // Whether the dock asks the compositor for an exclusive zone. Only the
    // plain always-visible mode does; the other three let windows through.
    bool reservesSpace() const { return m_hideMode == AlwaysVisible; }
    qreal opacity() const { return m_opacity; }
    // Custom panel background color; invalid = inherit the KDE theme color.
    QColor panelColor() const { return m_panelColor; }
    bool panelColorSet() const { return m_panelColor.isValid(); }
    // Eight user-configurable quick background colors (hex strings), offered in
    // the right-click "background color" submenu. Process-global: they live in
    // the shared settings file, so every dock offers the same palette.
    QStringList panelPresetColors() const { return m_panelPresetColors; }
    static constexpr int kPresetColorCount = 8;
    // Pure helpers, public so anything that offers the same palette reads it
    // from here instead of keeping its own copy of the eight defaults —
    // kdock-tilemenu's tile color submenu does exactly that.
    static QStringList normalizedPresetColors(const QStringList &colors);
    static QStringList defaultPresetColors();
    // Tiled panel background image (absolute path; empty = none).
    QString panelImage() const { return m_panelImage; }
    QUrl panelImageUrl() const { return m_panelImage.isEmpty() ? QUrl() : QUrl::fromLocalFile(m_panelImage); }
    QStringList pinned() const { return m_pinned; }
    QString screenName() const { return m_screenName; } // empty = compositor default
    bool panelMode() const { return m_panelMode; }      // stretch edge to edge
    bool compact() const { return m_compact; }          // no empty borders
    int alignment() const { return m_alignment; }       // icons along the edge
    // Off turns the dock into a widgets-only bar: neither the pinned launchers
    // nor the window buttons are drawn, and the apps block stops counting
    // toward the thickness (see dockThickness()). The "apps" token stays in
    // widgetOrder, so its place comes back untouched.
    bool showAppIcons() const { return m_showAppIcons; }
    bool showVolume() const { return m_showVolume; }
    bool showSystray() const { return m_showSystray; }
    int systrayIconScale() const { return m_systrayIconScale; }
    // Effective icon size for the systray, independent of widgetIconSize:
    // iconSize scaled by systrayIconScale%, clamped to a legible minimum.
    int systrayIconSize() const { return qMax(16, qRound(m_iconSize * m_systrayIconScale / 100.0)); }
    QStringList systrayHiddenItems() const { return m_systrayHiddenItems; }
    // Per-dock relanzador visibility. The primary dock uses a *hidden* list
    // (default: all shown); every other dock uses a *shown* list (default: none).
    QStringList relanzadoresHidden() const { return m_relanzadoresHidden; }
    QStringList relanzadoresShown() const { return m_relanzadoresShown; }
    // Per-dock script-runner visibility (same scheme as relanzadores).
    QStringList scriptRunnersHidden() const { return m_scriptRunnersHidden; }
    QStringList scriptRunnersShown() const { return m_scriptRunnersShown; }
    bool showClock() const { return m_showClock; }
    bool clockFormat24h() const { return m_clockFormat24h; }
    bool clockShowDate() const { return m_clockShowDate; }
    bool clockShowSeconds() const { return m_clockShowSeconds; }
    // Font size (px) of the clock time text; 0 = derived from widgetIconSize.
    int clockFontSize() const { return m_clockFontSize; }
    QString clock2Command() const { return m_clock2Command; }
    bool showBrightness() const { return m_showBrightness; }
    bool showBattery() const { return m_showBattery; }
    bool showAutohideToggle() const { return m_showAutohideToggle; }
    bool showDesktopButton() const { return m_showDesktopButton; }
    bool iconRunningBackground() const { return m_iconRunningBackground; }
    bool iconRunningDots() const { return m_iconRunningDots; }
    bool iconRunningLine() const { return m_iconRunningLine; }
    bool showMenuButton() const { return m_showMenuButton; }
    // The full-screen tile menu lives in its own process (kdock-tilemenu);
    // these two are the only things about it the dock has to know.
    bool showTileMenu() const { return m_showTileMenu; }
    QString tileMenuIcon() const { return m_tileMenuIcon; }
    // Same deal for the control panel (kdock-controlmanager). The three text
    // keys are the widget's own label: an empty controlManagerText means "show
    // the clock, formatted with controlManagerFormat".
    bool showControlManager() const { return m_showControlManager; }
    QString controlManagerIcon() const { return m_controlManagerIcon; }
    int controlManagerDisplay() const { return m_controlManagerDisplay; }
    QString controlManagerText() const { return m_controlManagerText; }
    QString controlManagerFormat() const { return m_controlManagerFormat; }
    int controlManagerFontSize() const { return m_controlManagerFontSize; }
    bool showSessionButton() const { return m_showSessionButton; }
    bool showSettingsButton() const { return m_showSettingsButton; }
    bool showMenuPower() const { return m_showMenuPower; }
    QString menuIcon() const { return m_menuIcon; }
    int menuPopupWidth() const { return m_menuPopupWidth; }
    int menuPopupHeight() const { return m_menuPopupHeight; }
    int menuColumns() const { return m_menuColumns; }
    // Icon size for the app list inside the menu popup. Drives both of its
    // layouts: the single-column rows (icon, then text) and the multi-column
    // cells (icon over text), whose cell size is derived from it.
    int menuAppIconSize() const { return m_menuAppIconSize; }
    // Padding around a multi-column cell. Lowering it packs the grid so a
    // handful of apps stop occupying the whole popup.
    int menuGridSpacing() const { return m_menuGridSpacing; }
    // Application that edits the XDG menus, opened from the menu widget's
    // right-click. A .desktop id, or a plain command as a fallback.
    QString menuEditorApp() const { return m_menuEditorApp; }
    bool showClipboard() const { return m_showClipboard; }
    int clipboardPopupWidth() const { return m_clipboardPopupWidth; }
    int clipboardPopupHeight() const { return m_clipboardPopupHeight; }
    bool showDisks() const { return m_showDisks; }
    bool showNetwork() const { return m_showNetwork; }
    bool showWeather() const { return m_showWeather; }
    bool showIconThemes() const { return m_showIconThemes; }
    bool showColorSchemes() const { return m_showColorSchemes; }
    bool showOverview() const { return m_showOverview; }
    bool showClock2() const { return m_showClock2; }
    bool showMoveToDesktop() const { return m_showMoveToDesktop; }
    bool showMoveToScreen() const { return m_showMoveToScreen; }
    bool showMaxMin() const { return m_showMaxMin; }
    bool showCloseWindow() const { return m_showCloseWindow; }
    bool showNextWallpaper() const { return m_showNextWallpaper; }
    bool showDarkMode() const { return m_showDarkMode; }
    // Virtual-desktop pager (token "pager"): the numbers of KWin's desktops,
    // click to switch.
    bool showPager() const { return m_showPager; }
    // This dock's own dark-mode flag. Only consulted when the app-wide switch
    // is off; see darkModeActive().
    bool darkMode() const { return m_darkMode; }
    // The value QML actually renders from. Resolved (never stored): the
    // app-wide switch wins, and then the exception list decides. Doing it this
    // way means an exception needs no write at all to the excepted dock's file
    // — including the docks that are not even running.
    bool darkModeActive() const;
    QColor darkAccent() const { return darkAccentColor(); }
    QColor darkBackground() const { return darkBackgroundColor(); }
    bool groupWindows() const { return m_groupWindows; }
    // QML-facing getter: the static setting lives in the shared config file,
    // but QML reads it as a per-instance property so the ToolTip bindings
    // (`config.showTooltips`) re-evaluate when the dialog flips the checkbox.
    bool showTooltipsProp() const { return showTooltips(); }
    QStringList menuFavorites() const { return m_menuFavorites; }
    int separator1() const { return m_separator1; }
    int separator2() const { return m_separator2; }
    bool separator1Transparent() const { return m_separator1Transparent; }
    bool separator2Transparent() const { return m_separator2Transparent; }
    int separatorSize() const { return m_separatorSize; }
    QStringList widgetOrder() const { return m_widgetOrder; }
    int dockLength() const { return m_dockLength; }   // 0 = auto, 1-100 = % of edge
    // Compact means truly flush: no gap to the screen edge
    int effectiveMargin() const { return m_compact ? 0 : m_screenMargin; }

    void setEdge(int edge);
    void setIconSize(int size);
    void setWidgetIconScale(int percent);
    void setWidgetIconThemeMode(int mode);
    void setWidgetIconThemeLightBg(const QString &themeId);
    void setWidgetIconThemeDarkBg(const QString &themeId);
    void setIconLabelMode(int mode);
    void setIconLabelWidth(int px);
    void setIconLabelFontSize(int px); // 0 = automatic
    void setWidgetLabelMode(int mode);
    void setLabelBold(bool on);
    // 1 or 2; anything else is clamped.
    void setLabelLines(int lines);
    void setAutoShrinkIcons(bool on);
    void setAutoShrinkMinIconSize(int px);
    void setSpacing(int spacing);
    void setScreenMargin(int margin);
    void setAutohide(bool autohide);
    void setHideMode(int mode);
    void setOpacity(qreal opacity);
    // Pass an invalid QColor() to clear (revert to the theme color).
    void setPanelColor(const QColor &color);
    void setPanelPresetColors(const QStringList &colors);
    // Revert the panel color to the inherited theme color (invalid QColor).
    // Exposed so QML can reset without constructing an invalid QColor.
    Q_INVOKABLE void resetPanelColor();
    void setPanelImage(const QString &path); // empty = none
    void setPinned(const QStringList &pinned);
    void setScreenName(const QString &name);
    void setPanelMode(bool panelMode);
    void setCompact(bool compact);
    void setAlignment(int alignment);
    void setShowAppIcons(bool show);
    void setShowVolume(bool show);
    void setShowSystray(bool show);
    void setSystrayIconScale(int percent);
    void setSystrayHiddenItems(const QStringList &items);
    void setRelanzadoresHidden(const QStringList &ids);
    void setRelanzadoresShown(const QStringList &ids);
    void setScriptRunnersHidden(const QStringList &ids);
    void setScriptRunnersShown(const QStringList &ids);
    void setShowClock(bool show);
    void setClockFormat24h(bool v);
    void setClockShowDate(bool v);
    void setClockShowSeconds(bool v);
    void setClockFontSize(int px);
    void setClock2Command(const QString &v);
    void setShowBrightness(bool show);
    void setShowBattery(bool show);
    void setShowAutohideToggle(bool show);
    void setShowDesktopButton(bool show);
    void setIconRunningBackground(bool on);
    void setIconRunningDots(bool on);
    void setIconRunningLine(bool on);
    void setShowMenuButton(bool show);
    void setShowTileMenu(bool show);
    void setTileMenuIcon(const QString &icon);
    void setShowControlManager(bool show);
    void setControlManagerIcon(const QString &icon);
    void setControlManagerDisplay(int mode);
    void setControlManagerText(const QString &text);
    void setControlManagerFormat(const QString &format);
    void setControlManagerFontSize(int px);
    void setShowSessionButton(bool show);
    void setShowSettingsButton(bool show);
    void setShowMenuPower(bool show);
    void setMenuIcon(const QString &name);
    void setMenuPopupWidth(int w);
    void setMenuPopupHeight(int h);
    void setMenuColumns(int columns);
    void setMenuAppIconSize(int px);
    void setMenuGridSpacing(int px);
    void setMenuEditorApp(const QString &app);
    void setShowClipboard(bool show);
    void setShowDisks(bool show);
    void setShowNetwork(bool show);
    Q_INVOKABLE void setShowWeather(bool show);
    void setShowIconThemes(bool show);
    void setShowColorSchemes(bool show);
    void setClipboardPopupWidth(int w);
    void setClipboardPopupHeight(int h);
    void setShowOverview(bool show);
    void setShowClock2(bool show);
    void setShowMoveToDesktop(bool show);
    void setShowMoveToScreen(bool show);
    void setShowMaxMin(bool show);
    void setShowCloseWindow(bool show);
    void setShowNextWallpaper(bool show);
    void setShowDarkMode(bool show);
    void setShowPager(bool show);
    void setDarkMode(bool on);
    // What the "Modo" submenu and the darkmode widget call: writes wherever the
    // effective value lives (the app-wide switch when it is on, this dock's own
    // flag otherwise), so the click does what it looks like it does.
    Q_INVOKABLE void setDarkModeActive(bool on);
    void setGroupWindows(bool group);
    void setMenuFavorites(const QStringList &favorites);
    void setSeparator1(int pos);
    void setSeparator2(int pos);
    void setSeparator1Transparent(bool on);
    void setSeparator2Transparent(bool on);
    void setSeparatorSize(int size);
    void setWidgetOrder(const QStringList &order);
    void setDockLength(int percent);

    // Drag & drop / context-menu editing of the section order (from QML).
    Q_INVOKABLE void moveSection(int from, int to);
    Q_INVOKABLE void insertSpring(int at);
    // Same as insertSpring, but the section also punches a hole in the
    // panel background and in the surface's input region.
    Q_INVOKABLE void insertGap(int at);
    // Fixed-size gap of separatorSize px between two sections.
    Q_INVOKABLE void insertSeparator(int at);
    Q_INVOKABLE void removeSectionAt(int at);

signals:
    void edgeChanged();
    void iconSizeChanged();
    void widgetIconScaleChanged();
    void widgetIconSizeChanged();
    void widgetIconThemeChanged();
    void iconLabelModeChanged();
    void iconLabelWidthChanged();
    void iconLabelFontSizeChanged();
    void widgetLabelModeChanged();
    void labelBoldChanged();
    void labelLinesChanged();
    void autoShrinkIconsChanged();
    void autoShrinkMinIconSizeChanged();
    void widgetNamesChanged();
    // Emitted by every setter the thickness depends on (icon size, compact,
    // edge, label mode/width/font).
    void dockThicknessChanged();
    void spacingChanged();
    void screenMarginChanged();
    void autohideChanged();
    void hideModeChanged();
    void opacityChanged();
    void panelColorChanged();
    void panelPresetColorsChanged();
    void panelImageChanged();
    void pinnedChanged();
    void screenNameChanged();
    void aliasChanged();
    void dockDesktopsChanged();
    void showTooltipsChanged();
    void panelModeChanged();
    void compactChanged();
    void alignmentChanged();
    void showAppIconsChanged();
    void showVolumeChanged();
    void showSystrayChanged();
    void systrayIconScaleChanged();
    void systrayIconSizeChanged();
    void systrayHiddenItemsChanged();
    void relanzadoresHiddenChanged();
    void relanzadoresShownChanged();
    void scriptRunnersHiddenChanged();
    void scriptRunnersShownChanged();
    void effectiveMarginChanged();
    void showClockChanged();
    void clockFormat24hChanged();
    void clockShowDateChanged();
    void clockShowSecondsChanged();
    void clockFontSizeChanged();
    void clock2CommandChanged();
    void showBrightnessChanged();
    void showBatteryChanged();
    void showAutohideToggleChanged();
    void showDesktopButtonChanged();
    void iconRunningBackgroundChanged();
    void iconRunningDotsChanged();
    void iconRunningLineChanged();
    void showMenuButtonChanged();
    void showTileMenuChanged();
    void tileMenuIconChanged();
    void showControlManagerChanged();
    void controlManagerIconChanged();
    void controlManagerDisplayChanged();
    void controlManagerTextChanged();
    void controlManagerFormatChanged();
    void controlManagerFontSizeChanged();
    void showSessionButtonChanged();
    void showSettingsButtonChanged();
    void showMenuPowerChanged();
    void menuIconChanged();
    void menuPopupWidthChanged();
    void menuPopupHeightChanged();
    void menuColumnsChanged();
    void menuAppIconSizeChanged();
    void menuGridSpacingChanged();
    void menuEditorAppChanged();
    void showClipboardChanged();
    void showDisksChanged();
    void showNetworkChanged();
    void showWeatherChanged();
    void showIconThemesChanged();
    void showColorSchemesChanged();
    void clipboardPopupWidthChanged();
    void clipboardPopupHeightChanged();
    void showOverviewChanged();
    void showClock2Changed();
    void showMoveToDesktopChanged();
    void showMoveToScreenChanged();
    void showMaxMinChanged();
    void showCloseWindowChanged();
    void showNextWallpaperChanged();
    void showDarkModeChanged();
    void showPagerChanged();
    // One signal for the whole dark-mode group (own flag, app-wide switch,
    // exceptions, both colors): every QML binding that cares reads more than
    // one of them anyway.
    void darkModeChanged();
    void groupWindowsChanged();
    void menuFavoritesChanged();
    void separator1Changed();
    void separator2Changed();
    void separator1TransparentChanged();
    void separator2TransparentChanged();
    void separatorSizeChanged();
    void widgetOrderChanged();
    void dockLengthChanged();

private:
    // Ensure apps + every known widget token appears exactly once (append
    // any missing at the end); keep spring tokens; drop unknown tokens.
    void reconcileWidgetOrder();
    // Read every value from m_settings into the members; shared by both ctors.
    void load();
    // Refresh m_menuFavorites from the effective source (shared file if sharing
    // is on, otherwise this dock's own settings) and emit menuFavoritesChanged.
    void reloadFavorites();
    // Refresh the menu-config group from its effective source and emit the
    // *Changed() signal for each member that actually changed.
    void reloadMenuConfig();
    // Persist a menu-config key to the effective store (shared file when
    // menuConfigShared(), otherwise this dock's own settings).
    void writeMenuConfigValue(const QString &key, const QVariant &value);

    // Emit darkModeChanged() on every live dock. The app-wide switch, the
    // exceptions and the two colors are process-global, so a change to any of
    // them has to repaint all the docks, not just the one being edited.
    static void notifyDarkModeChanged();

    // Quick colors are shared by every dock: read them from the shared file
    // (seeding it from this dock's legacy per-screen key on first run) and
    // normalize the list to kPresetColorCount entries.
    void reloadPresetColors();

    // Shared-favorites storage in the shared settings file.
    static QStringList sharedFavorites();
    static void setSharedFavorites(const QStringList &favorites);
    // Every live DockConfig, so shared favorites propagate across instances
    // (all docks run in one process).
    static QList<DockConfig *> s_instances;

    QSettings m_settings;
    QString m_dockId;
    QString m_alias; // user-chosen friendly name; empty = default "<screen> — Dock <n>"
    QList<int> m_dockDesktops; // 1-based virtual desktops; empty = base dock
    int m_edge = Bottom;
    int m_iconSize = 48;
    int m_widgetIconScale = 100; // % of iconSize applied to widget sections
    int m_spacing = 6;
    int m_screenMargin = 4;
    int m_hideMode = AlwaysVisible;
    qreal m_opacity = 0.85;
    QColor m_panelColor; // invalid = inherit theme background
    QStringList m_panelPresetColors;
    QString m_panelImage; // absolute path; empty = no image
    QStringList m_pinned;
    QString m_screenName;
    bool m_panelMode = false;
    bool m_compact = false;
    int m_alignment = Center;
    bool m_showVolume = true;
    int m_widgetIconThemeMode = MatchDockColor;
    QString m_widgetIconThemeLightBg = QStringLiteral("breeze");
    QString m_widgetIconThemeDarkBg = QStringLiteral("breeze-dark");
    int m_iconLabelMode = IconOnly;
    int m_iconLabelWidth = 110;
    int m_measuredLabelWidth = -1; // -1 = unmeasured; see effectiveLabelWidth()
    int m_iconLabelFontSize = 0; // 0 = derived from iconSize
    int m_widgetLabelMode = IconOnly;
    bool m_labelBold = false;
    int m_labelLines = 1;
    bool m_autoShrinkIcons = true;
    int m_autoShrinkMinIconSize = 16;
    QHash<QString, QString> m_widgetNames; // section token -> user rename
    int m_widgetNamesRevision = 0;
    bool m_showAppIcons = true;
    bool m_showSystray = false;
    int m_systrayIconScale = 100; // % of iconSize applied to systray icons
    QStringList m_systrayHiddenItems;
    QStringList m_relanzadoresHidden;
    QStringList m_relanzadoresShown;
    QStringList m_scriptRunnersHidden;
    QStringList m_scriptRunnersShown;
    bool m_showClock = false;
    bool m_clockFormat24h = true;
    bool m_clockShowDate = false;
    bool m_clockShowSeconds = false;
    int m_clockFontSize = 0; // 0 = derived from widgetIconSize
    QString m_clock2Command;
    bool m_showBrightness = false;
    bool m_showBattery = false;
    bool m_showAutohideToggle = false;
    bool m_showDesktopButton = false;
    bool m_iconRunningBackground = false;
    bool m_iconRunningDots = true;
    bool m_iconRunningLine = false;
    bool m_showMenuButton = false;
    bool m_showTileMenu = false;
    QString m_tileMenuIcon = QStringLiteral("view-list-icons");
    bool m_showControlManager = false;
    QString m_controlManagerIcon = QStringLiteral("preferences-system");
    int m_controlManagerDisplay = 0;
    QString m_controlManagerText;
    QString m_controlManagerFormat = QStringLiteral("ddd d MMM  HH:mm");
    int m_controlManagerFontSize = 0; // 0 = follows clock font / icon size
    bool m_showSessionButton = false;
    bool m_showSettingsButton = false;
    bool m_showMenuPower = true;
    QString m_menuIcon = QStringLiteral("applications-all");
    int m_menuPopupWidth = 540;
    int m_menuPopupHeight = 460;
    int m_menuColumns = 1;
    int m_menuAppIconSize = 32;   // the size the single-column rows always used
    int m_menuGridSpacing = 8;
    QString m_menuEditorApp = QStringLiteral("org.kde.kmenuedit");
    bool m_showClipboard = false;
    bool m_showDisks = false;
    bool m_showNetwork = false;
    bool m_showWeather = false;
    bool m_showIconThemes = false;
    bool m_showColorSchemes = false;
    int m_clipboardPopupWidth = 360;
    int m_clipboardPopupHeight = 460;
    bool m_showOverview = false;
    bool m_showClock2 = false;
    bool m_showMoveToDesktop = false;
    bool m_showMoveToScreen = false;
    bool m_showMaxMin = false;
    bool m_showCloseWindow = false;
    bool m_showNextWallpaper = false;
    bool m_showDarkMode = false;
    bool m_showPager = false;
    bool m_darkMode = false;
    bool m_groupWindows = true;
    QStringList m_menuFavorites;
    int m_separator1 = -1;
    int m_separator2 = -1;
    bool m_separator1Transparent = false;
    bool m_separator2Transparent = false;
    int m_separatorSize = 16;
    QStringList m_widgetOrder;
    int m_dockLength = 0; // 0 = auto, 1-100 = % of the screen edge
};
