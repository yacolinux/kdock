// Persistent dock settings (QSettings-backed), exposed to QML and to the
// widgets settings dialog.

#pragma once

#include <QColor>
#include <QHash>
#include <QObject>
#include <QSettings>
#include <QStringList>
#include <QUrl>

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
    // KDE appearance pickers (icon theme / color scheme), see AppearanceControl.
    Q_PROPERTY(bool showIconThemes READ showIconThemes WRITE setShowIconThemes NOTIFY showIconThemesChanged)
    Q_PROPERTY(bool showColorSchemes READ showColorSchemes WRITE setShowColorSchemes NOTIFY showColorSchemesChanged)
    Q_PROPERTY(bool showOverview READ showOverview WRITE setShowOverview NOTIFY showOverviewChanged)
    Q_PROPERTY(bool showClock2 READ showClock2 WRITE setShowClock2 NOTIFY showClock2Changed)
    Q_PROPERTY(bool showMoveToDesktop READ showMoveToDesktop WRITE setShowMoveToDesktop NOTIFY showMoveToDesktopChanged)
    Q_PROPERTY(bool showMoveToScreen READ showMoveToScreen WRITE setShowMoveToScreen NOTIFY showMoveToScreenChanged)
    Q_PROPERTY(bool showNextWallpaper READ showNextWallpaper WRITE setShowNextWallpaper NOTIFY showNextWallpaperChanged)
    Q_PROPERTY(bool groupWindows READ groupWindows WRITE setGroupWindows NOTIFY groupWindowsChanged)
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
    Q_PROPERTY(int iconLabelGap READ iconLabelGap NOTIFY dockThicknessChanged)
    // Same idea for the non-app sections (widgets and blocks), configured apart
    // from the apps: a dock can name its widgets and not its apps, or vice versa.
    Q_PROPERTY(int widgetLabelMode READ widgetLabelMode WRITE setWidgetLabelMode NOTIFY widgetLabelModeChanged)
    // Bumped on every rename; QML reads it inside the bindings that call
    // widgetName() so a custom name repaints (same trick as theme.revision).
    Q_PROPERTY(int widgetNamesRevision READ widgetNamesRevision NOTIFY widgetNamesChanged)
    // Shrink every icon (apps, widgets, systray) when the sections no longer
    // fit along the dock; see the "auto-shrink" block in Dock.qml.
    Q_PROPERTY(bool autoShrinkIcons READ autoShrinkIcons WRITE setAutoShrinkIcons NOTIFY autoShrinkIconsChanged)
    Q_PROPERTY(int autoShrinkMinIconSize READ autoShrinkMinIconSize WRITE setAutoShrinkMinIconSize NOTIFY autoShrinkMinIconSizeChanged)
    Q_PROPERTY(int dockThickness READ dockThickness NOTIFY dockThicknessChanged)

public:
    enum Edge { Bottom = 0, Top = 1, Left = 2, Right = 3 };
    Q_ENUM(Edge)
    enum Alignment { Start = 0, Center = 1, End = 2 };
    Q_ENUM(Alignment)

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

    // Ordered dock sections. Widget tokens plus zero or more "spring"
    // (dynamic separator) tokens. See reconcileWidgetOrder().
    static QStringList knownWidgetTokens(); // apps + every widget, no springs

    // Up to this many docks can coexist on a single monitor.
    static constexpr int kMaxDocksPerScreen = 3;

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
    int iconLabelGap() const { return m_compact ? 2 : 4; } // icon <-> label
    // See IconLabelMode. Width and font size are shared with the app labels.
    int widgetLabelMode() const { return m_widgetLabelMode; }
    // Icons shrink (down to autoShrinkMinIconSize) instead of overflowing the
    // dock when the sections do not fit. Without it the layout piles the
    // sections that do not fit on top of each other.
    bool autoShrinkIcons() const { return m_autoShrinkIcons; }
    int autoShrinkMinIconSize() const { return m_autoShrinkMinIconSize; }
    int widgetNamesRevision() const { return m_widgetNamesRevision; }
    // Name drawn for a section: the user's rename if there is one, otherwise
    // defaultWidgetLabel(). Q_INVOKABLE because QML asks per token.
    Q_INVOKABLE QString widgetName(const QString &token) const;
    // Empty (or equal to the default) clears the rename.
    Q_INVOKABLE void setWidgetName(const QString &token, const QString &name);
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
    bool autohide() const { return m_autohide; }
    qreal opacity() const { return m_opacity; }
    // Custom panel background color; invalid = inherit the KDE theme color.
    QColor panelColor() const { return m_panelColor; }
    bool panelColorSet() const { return m_panelColor.isValid(); }
    // Four user-configurable quick background colors (hex strings), offered in
    // the right-click "background color" submenu.
    QStringList panelPresetColors() const { return m_panelPresetColors; }
    // Tiled panel background image (absolute path; empty = none).
    QString panelImage() const { return m_panelImage; }
    QUrl panelImageUrl() const { return m_panelImage.isEmpty() ? QUrl() : QUrl::fromLocalFile(m_panelImage); }
    QStringList pinned() const { return m_pinned; }
    QString screenName() const { return m_screenName; } // empty = compositor default
    bool panelMode() const { return m_panelMode; }      // stretch edge to edge
    bool compact() const { return m_compact; }          // no empty borders
    int alignment() const { return m_alignment; }       // icons along the edge
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
    bool showIconThemes() const { return m_showIconThemes; }
    bool showColorSchemes() const { return m_showColorSchemes; }
    bool showOverview() const { return m_showOverview; }
    bool showClock2() const { return m_showClock2; }
    bool showMoveToDesktop() const { return m_showMoveToDesktop; }
    bool showMoveToScreen() const { return m_showMoveToScreen; }
    bool showNextWallpaper() const { return m_showNextWallpaper; }
    bool groupWindows() const { return m_groupWindows; }
    QStringList menuFavorites() const { return m_menuFavorites; }
    int separator1() const { return m_separator1; }
    int separator2() const { return m_separator2; }
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
    void setAutoShrinkIcons(bool on);
    void setAutoShrinkMinIconSize(int px);
    void setSpacing(int spacing);
    void setScreenMargin(int margin);
    void setAutohide(bool autohide);
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
    void setShowIconThemes(bool show);
    void setShowColorSchemes(bool show);
    void setClipboardPopupWidth(int w);
    void setClipboardPopupHeight(int h);
    void setShowOverview(bool show);
    void setShowClock2(bool show);
    void setShowMoveToDesktop(bool show);
    void setShowMoveToScreen(bool show);
    void setShowNextWallpaper(bool show);
    void setGroupWindows(bool group);
    void setMenuFavorites(const QStringList &favorites);
    void setSeparator1(int pos);
    void setSeparator2(int pos);
    void setSeparatorSize(int size);
    void setWidgetOrder(const QStringList &order);
    void setDockLength(int percent);

    // Drag & drop / context-menu editing of the section order (from QML).
    Q_INVOKABLE void moveSection(int from, int to);
    Q_INVOKABLE void insertSpring(int at);
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
    void autoShrinkIconsChanged();
    void autoShrinkMinIconSizeChanged();
    void widgetNamesChanged();
    // Emitted by every setter the thickness depends on (icon size, compact,
    // edge, label mode/width/font).
    void dockThicknessChanged();
    void spacingChanged();
    void screenMarginChanged();
    void autohideChanged();
    void opacityChanged();
    void panelColorChanged();
    void panelPresetColorsChanged();
    void panelImageChanged();
    void pinnedChanged();
    void screenNameChanged();
    void panelModeChanged();
    void compactChanged();
    void alignmentChanged();
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
    void showIconThemesChanged();
    void showColorSchemesChanged();
    void clipboardPopupWidthChanged();
    void clipboardPopupHeightChanged();
    void showOverviewChanged();
    void showClock2Changed();
    void showMoveToDesktopChanged();
    void showMoveToScreenChanged();
    void showNextWallpaperChanged();
    void groupWindowsChanged();
    void menuFavoritesChanged();
    void separator1Changed();
    void separator2Changed();
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

    // Shared-favorites storage in the shared settings file.
    static QStringList sharedFavorites();
    static void setSharedFavorites(const QStringList &favorites);
    // Every live DockConfig, so shared favorites propagate across instances
    // (all docks run in one process).
    static QList<DockConfig *> s_instances;

    QSettings m_settings;
    QString m_dockId;
    int m_edge = Bottom;
    int m_iconSize = 48;
    int m_widgetIconScale = 100; // % of iconSize applied to widget sections
    int m_spacing = 6;
    int m_screenMargin = 4;
    bool m_autohide = false;
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
    bool m_autoShrinkIcons = true;
    int m_autoShrinkMinIconSize = 16;
    QHash<QString, QString> m_widgetNames; // section token -> user rename
    int m_widgetNamesRevision = 0;
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
    bool m_showIconThemes = false;
    bool m_showColorSchemes = false;
    int m_clipboardPopupWidth = 360;
    int m_clipboardPopupHeight = 460;
    bool m_showOverview = false;
    bool m_showClock2 = false;
    bool m_showMoveToDesktop = false;
    bool m_showMoveToScreen = false;
    bool m_showNextWallpaper = false;
    bool m_groupWindows = true;
    QStringList m_menuFavorites;
    int m_separator1 = -1;
    int m_separator2 = -1;
    int m_separatorSize = 16;
    QStringList m_widgetOrder;
    int m_dockLength = 0; // 0 = auto, 1-100 = % of the screen edge
};
