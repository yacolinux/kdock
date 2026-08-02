#include "dockconfig.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

#include <utility>

namespace {
// Widgets accept every label position but LabelOnly: a widget without its icon
// would be unrecognizable, so that value falls back to "no label".
int sanitizedWidgetLabelMode(int mode)
{
    mode = qBound(int(DockConfig::IconOnly), mode, int(DockConfig::LabelLeft));
    return mode == DockConfig::LabelOnly ? int(DockConfig::IconOnly) : mode;
}
} // namespace

QString DockConfig::settingsFilePath()
{
    // XDG data dir: ~/.local/share/kdock/kdock.conf
    const QString dir =
        QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
        + QStringLiteral("/kdock");
    const QString path = dir + QStringLiteral("/kdock.conf");

    static bool migrated = false;
    if (!migrated) {
        migrated = true;
        QDir().mkpath(dir);
        if (!QFileInfo::exists(path)) {
            // One-time migration from the legacy QSettings location
            // (~/.config/kdock/kdock.conf).
            const QString legacy =
                QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
                + QStringLiteral("/kdock/kdock.conf");
            if (QFileInfo::exists(legacy))
                QFile::copy(legacy, path);
        }
    }
    return path;
}

QString DockConfig::makeDockId(const QString &screenName, int slot)
{
    if (screenName.isEmpty() || slot <= 0)
        return screenName;
    return screenName + QLatin1Char('#') + QString::number(slot);
}

QString DockConfig::screenOfDockId(const QString &dockId)
{
    const int hash = dockId.indexOf(QLatin1Char('#'));
    return hash < 0 ? dockId : dockId.left(hash);
}

int DockConfig::slotOfDockId(const QString &dockId)
{
    const int hash = dockId.indexOf(QLatin1Char('#'));
    return hash < 0 ? 0 : dockId.mid(hash + 1).toInt();
}

QString DockConfig::instanceSettingsFilePath(const QString &dockId)
{
    if (dockId.isEmpty())
        return settingsFilePath();
    const QString dir =
        QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
        + QStringLiteral("/kdock");
    QDir().mkpath(dir);
    // Sanitize so odd output names never escape the directory.
    QString safe = screenOfDockId(dockId);
    for (QChar &c : safe) {
        if (!c.isLetterOrNumber() && c != QLatin1Char('-') && c != QLatin1Char('_'))
            c = QLatin1Char('_');
    }
    // Slot 0 keeps the legacy name; extra slots append "-<slot>".
    const int slot = slotOfDockId(dockId);
    if (slot > 0)
        safe += QLatin1Char('-') + QString::number(slot);
    return dir + QStringLiteral("/kdock-") + safe + QStringLiteral(".conf");
}

QStringList DockConfig::enabledDocks()
{
    QSettings s(settingsFilePath(), QSettings::IniFormat);
    return s.value(QStringLiteral("enabledScreens")).toStringList();
}

void DockConfig::setDockEnabled(const QString &dockId, bool enabled)
{
    if (dockId.isEmpty())
        return;
    QSettings s(settingsFilePath(), QSettings::IniFormat);
    QStringList list = s.value(QStringLiteral("enabledScreens")).toStringList();
    const bool present = list.contains(dockId);
    if (enabled && !present)
        list.append(dockId);
    else if (!enabled && present)
        list.removeAll(dockId);
    else
        return;
    s.setValue(QStringLiteral("enabledScreens"), list);
}

QStringList DockConfig::knownScreens()
{
    QSettings s(settingsFilePath(), QSettings::IniFormat);
    return s.value(QStringLiteral("knownScreens")).toStringList();
}

void DockConfig::addKnownScreen(const QString &screenName)
{
    if (screenName.isEmpty())
        return;
    QSettings s(settingsFilePath(), QSettings::IniFormat);
    QStringList list = s.value(QStringLiteral("knownScreens")).toStringList();
    if (list.contains(screenName))
        return;
    list.append(screenName);
    s.setValue(QStringLiteral("knownScreens"), list);
}

QStringList DockConfig::knownDocks()
{
    QSettings s(settingsFilePath(), QSettings::IniFormat);
    return s.value(QStringLiteral("knownDocks")).toStringList();
}

void DockConfig::addKnownDock(const QString &dockId)
{
    if (dockId.isEmpty())
        return;
    QSettings s(settingsFilePath(), QSettings::IniFormat);
    QStringList list = s.value(QStringLiteral("knownDocks")).toStringList();
    if (list.contains(dockId))
        return;
    list.append(dockId);
    s.setValue(QStringLiteral("knownDocks"), list);
}

void DockConfig::removeKnownDock(const QString &dockId)
{
    if (dockId.isEmpty())
        return;
    QSettings s(settingsFilePath(), QSettings::IniFormat);
    QStringList list = s.value(QStringLiteral("knownDocks")).toStringList();
    if (list.removeAll(dockId) > 0)
        s.setValue(QStringLiteral("knownDocks"), list);
}

QList<DockConfig *> DockConfig::s_instances;

bool DockConfig::favoritesShared()
{
    QSettings s(settingsFilePath(), QSettings::IniFormat);
    return s.value(QStringLiteral("shareFavorites"), false).toBool();
}

QStringList DockConfig::sharedFavorites()
{
    QSettings s(settingsFilePath(), QSettings::IniFormat);
    return s.value(QStringLiteral("sharedMenuFavorites")).toStringList();
}

void DockConfig::setSharedFavorites(const QStringList &favorites)
{
    QSettings s(settingsFilePath(), QSettings::IniFormat);
    s.setValue(QStringLiteral("sharedMenuFavorites"), favorites);
}

bool DockConfig::darkModeAllDocks()
{
    QSettings s(settingsFilePath(), QSettings::IniFormat);
    return s.value(QStringLiteral("darkModeAllDocks"), false).toBool();
}

void DockConfig::setDarkModeAllDocks(bool on)
{
    if (darkModeAllDocks() == on)
        return;
    {
        QSettings s(settingsFilePath(), QSettings::IniFormat);
        s.setValue(QStringLiteral("darkModeAllDocks"), on);
    }
    notifyDarkModeChanged();
}

QStringList DockConfig::darkModeExceptions()
{
    QSettings s(settingsFilePath(), QSettings::IniFormat);
    return s.value(QStringLiteral("darkModeExceptions")).toStringList();
}

void DockConfig::setDarkModeExceptions(const QStringList &dockIds)
{
    if (darkModeExceptions() == dockIds)
        return;
    {
        QSettings s(settingsFilePath(), QSettings::IniFormat);
        s.setValue(QStringLiteral("darkModeExceptions"), dockIds);
    }
    notifyDarkModeChanged();
}

QColor DockConfig::darkAccentColor()
{
    QSettings s(settingsFilePath(), QSettings::IniFormat);
    const QColor c(s.value(QStringLiteral("darkAccent"),
                           QString::fromLatin1(kDarkAccentDefault)).toString());
    return c.isValid() ? c : QColor(QString::fromLatin1(kDarkAccentDefault));
}

void DockConfig::setDarkAccentColor(const QColor &color)
{
    if (!color.isValid() || darkAccentColor() == color)
        return;
    {
        QSettings s(settingsFilePath(), QSettings::IniFormat);
        s.setValue(QStringLiteral("darkAccent"), color.name());
    }
    notifyDarkModeChanged();
}

QColor DockConfig::darkBackgroundColor()
{
    QSettings s(settingsFilePath(), QSettings::IniFormat);
    const QColor c(s.value(QStringLiteral("darkBackground"),
                           QString::fromLatin1(kDarkBackgroundDefault)).toString());
    return c.isValid() ? c : QColor(QString::fromLatin1(kDarkBackgroundDefault));
}

void DockConfig::setDarkBackgroundColor(const QColor &color)
{
    if (!color.isValid() || darkBackgroundColor() == color)
        return;
    {
        QSettings s(settingsFilePath(), QSettings::IniFormat);
        s.setValue(QStringLiteral("darkBackground"), color.name());
    }
    notifyDarkModeChanged();
}

void DockConfig::notifyDarkModeChanged()
{
    for (DockConfig *cfg : std::as_const(s_instances))
        emit cfg->darkModeChanged();
}

bool DockConfig::darkModeActive() const
{
    if (darkModeAllDocks())
        return !darkModeExceptions().contains(m_dockId);
    return m_darkMode;
}

void DockConfig::setDarkModeActive(bool on)
{
    if (darkModeAllDocks()) {
        // The app-wide switch defines the *scope* of the action, not just a
        // default: picking a mode from any one dock applies it to all of them.
        // Exceptions are an editing tool of the DarkMode tab, so a mode change
        // clears them instead of adding one (which is what this used to do, and
        // it left the switch on with the dock reading as dark — bug 2026-08-02).
        setDarkModeExceptions(QStringList());
        if (!on) {
            // Dropping the switch hands every dock back to its own flag, so
            // that flag has to come down too or the dock stays dark.
            for (DockConfig *cfg : std::as_const(s_instances))
                cfg->setDarkMode(false);
            setDarkModeAllDocks(false);
        }
        return;
    }
    setDarkMode(on);
}

void DockConfig::setFavoritesShared(bool shared)
{
    if (favoritesShared() == shared)
        return;
    {
        QSettings s(settingsFilePath(), QSettings::IniFormat);
        s.setValue(QStringLiteral("shareFavorites"), shared);
    }
    // On enable, seed the shared list from a live config if it's still empty,
    // so switching on doesn't wipe the user's current favorites.
    if (shared && sharedFavorites().isEmpty() && !s_instances.isEmpty())
        setSharedFavorites(s_instances.first()->m_menuFavorites);

    // Re-sync every live dock to the (now) effective source.
    for (DockConfig *cfg : std::as_const(s_instances))
        cfg->reloadFavorites();
}

DockConfig::DockConfig(QObject *parent)
    : QObject(parent)
    , m_settings(settingsFilePath(), QSettings::IniFormat)
{
    load();
    s_instances.append(this);
}

DockConfig::DockConfig(const QString &dockId, QObject *parent)
    : QObject(parent)
    , m_settings(instanceSettingsFilePath(dockId), QSettings::IniFormat)
    , m_dockId(dockId)
{
    load();
    s_instances.append(this);
    // Bind to the output derived from the dockId (extra slots share a screen
    // with slot 0 but persist to their own file).
    const QString screen = screenOfDockId(dockId);
    if (!screen.isEmpty() && m_screenName != screen)
        setScreenName(screen);
}

DockConfig::~DockConfig()
{
    s_instances.removeAll(this);
}

bool DockConfig::menuConfigShared()
{
    QSettings s(settingsFilePath(), QSettings::IniFormat);
    return s.value(QStringLiteral("shareMenuConfig"), false).toBool();
}

void DockConfig::writeMenuConfigValue(const QString &key, const QVariant &value)
{
    if (menuConfigShared()) {
        // Persist to the shared file and propagate to every other live dock so
        // all menus pick up the new value at once.
        QSettings s(settingsFilePath(), QSettings::IniFormat);
        s.setValue(key, value);
        for (DockConfig *cfg : std::as_const(s_instances)) {
            if (cfg != this)
                cfg->reloadMenuConfig();
        }
    } else {
        m_settings.setValue(key, value);
    }
}

void DockConfig::reloadMenuConfig()
{
    // Effective source: the shared file when sharing is on, else this dock's own.
    const bool shared = menuConfigShared();
    QSettings shall(settingsFilePath(), QSettings::IniFormat);
    QSettings &src = shared ? shall : m_settings;

    const bool power = src.value(QStringLiteral("showMenuPower"), m_showMenuPower).toBool();
    const QString icon = src.value(QStringLiteral("menuIcon"), m_menuIcon).toString();
    const int w = src.value(QStringLiteral("menuPopupWidth"), m_menuPopupWidth).toInt();
    const int h = src.value(QStringLiteral("menuPopupHeight"), m_menuPopupHeight).toInt();
    const int cols = qBound(1, src.value(QStringLiteral("menuColumns"), m_menuColumns).toInt(), 8);
    const int iconSize = qBound(16, src.value(QStringLiteral("menuAppIconSize"),
                                              m_menuAppIconSize).toInt(), 96);
    const int gridSpacing = qBound(0, src.value(QStringLiteral("menuGridSpacing"),
                                                m_menuGridSpacing).toInt(), 40);
    const QString editor = src.value(QStringLiteral("menuEditorApp"), m_menuEditorApp).toString();

    if (m_showMenuPower != power) {
        m_showMenuPower = power;
        emit showMenuPowerChanged();
    }
    if (m_menuIcon != icon) {
        m_menuIcon = icon;
        emit menuIconChanged();
    }
    if (m_menuPopupWidth != w) {
        m_menuPopupWidth = w;
        emit menuPopupWidthChanged();
    }
    if (m_menuPopupHeight != h) {
        m_menuPopupHeight = h;
        emit menuPopupHeightChanged();
    }
    if (m_menuColumns != cols) {
        m_menuColumns = cols;
        emit menuColumnsChanged();
    }
    if (m_menuAppIconSize != iconSize) {
        m_menuAppIconSize = iconSize;
        emit menuAppIconSizeChanged();
    }
    if (m_menuGridSpacing != gridSpacing) {
        m_menuGridSpacing = gridSpacing;
        emit menuGridSpacingChanged();
    }
    if (m_menuEditorApp != editor) {
        m_menuEditorApp = editor;
        emit menuEditorAppChanged();
    }
}

void DockConfig::setMenuConfigShared(bool shared)
{
    if (menuConfigShared() == shared)
        return;
    {
        QSettings s(settingsFilePath(), QSettings::IniFormat);
        s.setValue(QStringLiteral("shareMenuConfig"), shared);
    }
    // On enable, seed any not-yet-present shared keys from a live config so the
    // switch doesn't reset the menu appearance to defaults.
    if (shared && !s_instances.isEmpty()) {
        QSettings s(settingsFilePath(), QSettings::IniFormat);
        const DockConfig *seed = s_instances.first();
        const auto seedKey = [&](const QString &key, const QVariant &v) {
            if (!s.contains(key))
                s.setValue(key, v);
        };
        seedKey(QStringLiteral("showMenuPower"), seed->m_showMenuPower);
        seedKey(QStringLiteral("menuIcon"), seed->m_menuIcon);
        seedKey(QStringLiteral("menuPopupWidth"), seed->m_menuPopupWidth);
        seedKey(QStringLiteral("menuPopupHeight"), seed->m_menuPopupHeight);
        seedKey(QStringLiteral("menuColumns"), seed->m_menuColumns);
        seedKey(QStringLiteral("menuAppIconSize"), seed->m_menuAppIconSize);
        seedKey(QStringLiteral("menuGridSpacing"), seed->m_menuGridSpacing);
        seedKey(QStringLiteral("menuEditorApp"), seed->m_menuEditorApp);
    }
    // Re-sync every live dock to the (now) effective source.
    for (DockConfig *cfg : std::as_const(s_instances))
        cfg->reloadMenuConfig();
}

void DockConfig::load()
{
    m_edge = m_settings.value(QStringLiteral("edge"), Bottom).toInt();
    m_iconSize = m_settings.value(QStringLiteral("iconSize"), 48).toInt();
    m_widgetIconScale = m_settings.value(QStringLiteral("widgetIconScale"), 100).toInt();
    m_widgetIconThemeMode = m_settings.value(QStringLiteral("widgetIconThemeMode"),
                                             int(MatchDockColor)).toInt();
    m_widgetIconThemeLightBg = m_settings.value(QStringLiteral("widgetIconThemeLightBg"),
                                                QStringLiteral("breeze")).toString();
    m_widgetIconThemeDarkBg = m_settings.value(QStringLiteral("widgetIconThemeDarkBg"),
                                               QStringLiteral("breeze-dark")).toString();
    m_iconLabelMode = qBound(int(IconOnly),
                             m_settings.value(QStringLiteral("iconLabelMode"),
                                              int(IconOnly)).toInt(),
                             int(LabelLeft));
    m_iconLabelWidth = qBound(60, m_settings.value(QStringLiteral("iconLabelWidth"), 110).toInt(), 400);
    m_iconLabelFontSize = m_settings.value(QStringLiteral("iconLabelFontSize"), 0).toInt();
    m_widgetLabelMode = sanitizedWidgetLabelMode(
        m_settings.value(QStringLiteral("widgetLabelMode"), int(IconOnly)).toInt());
    m_labelBold = m_settings.value(QStringLiteral("labelBold"), false).toBool();
    m_autoShrinkIcons = m_settings.value(QStringLiteral("autoShrinkIcons"), true).toBool();
    m_autoShrinkMinIconSize =
        qBound(8, m_settings.value(QStringLiteral("autoShrinkMinIconSize"), 16).toInt(), 64);
    // Renames live in their own group, one key per section token.
    m_widgetNames.clear();
    m_settings.beginGroup(QStringLiteral("widgetNames"));
    const QStringList renamed = m_settings.childKeys();
    for (const QString &token : renamed) {
        const QString name = m_settings.value(token).toString();
        if (!name.isEmpty())
            m_widgetNames.insert(token, name);
    }
    m_settings.endGroup();
    m_spacing = m_settings.value(QStringLiteral("spacing"), 6).toInt();
    m_screenMargin = m_settings.value(QStringLiteral("screenMargin"), 4).toInt();
    m_autohide = m_settings.value(QStringLiteral("autohide"), false).toBool();
    m_opacity = m_settings.value(QStringLiteral("opacity"), 0.85).toDouble();
    // Stored as a #RRGGBB string; empty/absent = inherit the theme color.
    const QString pc = m_settings.value(QStringLiteral("panelColor")).toString();
    m_panelColor = pc.isEmpty() ? QColor() : QColor(pc);
    m_panelPresetColors = m_settings.value(QStringLiteral("panelPresetColors"),
                                           QStringList{QStringLiteral("#31363b"),
                                                       QStringLiteral("#2c5aa0"),
                                                       QStringLiteral("#3a7d44"),
                                                       QStringLiteral("#7a4a8c")})
                              .toStringList();
    // Keep exactly four entries so the UI/menu can index them safely.
    while (m_panelPresetColors.size() < 4)
        m_panelPresetColors.append(QStringLiteral("#31363b"));
    if (m_panelPresetColors.size() > 4)
        m_panelPresetColors = m_panelPresetColors.mid(0, 4);
    m_panelImage = m_settings.value(QStringLiteral("panelImage")).toString();
    m_pinned = m_settings.value(QStringLiteral("pinned"),
                                QStringList{QStringLiteral("org.kde.dolphin"),
                                            QStringLiteral("org.kde.konsole")})
                   .toStringList();
    m_screenName = m_settings.value(QStringLiteral("screenName")).toString();
    m_panelMode = m_settings.value(QStringLiteral("panelMode"), false).toBool();
    m_compact = m_settings.value(QStringLiteral("compact"), false).toBool();
    m_alignment = m_settings.value(QStringLiteral("alignment"), Center).toInt();
    m_showVolume = m_settings.value(QStringLiteral("showVolume"), true).toBool();
    m_showSystray = m_settings.value(QStringLiteral("showSystray"), false).toBool();
    m_systrayIconScale = m_settings.value(QStringLiteral("systrayIconScale"), 100).toInt();
    m_systrayHiddenItems = m_settings.value(QStringLiteral("systrayHiddenItems")).toStringList();
    m_relanzadoresHidden = m_settings.value(QStringLiteral("relanzadoresHidden")).toStringList();
    m_relanzadoresShown = m_settings.value(QStringLiteral("relanzadoresShown")).toStringList();
    m_scriptRunnersHidden = m_settings.value(QStringLiteral("scriptRunnersHidden")).toStringList();
    m_scriptRunnersShown = m_settings.value(QStringLiteral("scriptRunnersShown")).toStringList();
    m_showClock = m_settings.value(QStringLiteral("showClock"), false).toBool();
    m_clockFormat24h = m_settings.value(QStringLiteral("clockFormat24h"), true).toBool();
    m_clockShowDate = m_settings.value(QStringLiteral("clockShowDate"), false).toBool();
    m_clockShowSeconds = m_settings.value(QStringLiteral("clockShowSeconds"), false).toBool();
    m_clockFontSize = m_settings.value(QStringLiteral("clockFontSize"), 0).toInt();
    m_clock2Command = m_settings.value(QStringLiteral("clock2Command")).toString();
    m_showBrightness = m_settings.value(QStringLiteral("showBrightness"), false).toBool();
    m_showBattery = m_settings.value(QStringLiteral("showBattery"), false).toBool();
    m_showAutohideToggle = m_settings.value(QStringLiteral("showAutohideToggle"), false).toBool();
    m_showDesktopButton = m_settings.value(QStringLiteral("showDesktopButton"), false).toBool();
    m_iconRunningBackground = m_settings.value(QStringLiteral("iconRunningBackground"), false).toBool();
    m_iconRunningLine = m_settings.value(QStringLiteral("iconRunningLine"), false).toBool();
    // Dots used to appear whenever the color background was off; preserve that as
    // the default so existing setups keep their indicator after the update.
    m_iconRunningDots = m_settings.value(QStringLiteral("iconRunningDots"),
                                         !m_iconRunningBackground).toBool();
    m_showMenuButton = m_settings.value(QStringLiteral("showMenuButton"), false).toBool();
    m_showSessionButton = m_settings.value(QStringLiteral("showSessionButton"), false).toBool();
    m_showSettingsButton = m_settings.value(QStringLiteral("showSettingsButton"), false).toBool();
    m_showMenuPower = m_settings.value(QStringLiteral("showMenuPower"), true).toBool();
    m_menuIcon = m_settings.value(QStringLiteral("menuIcon"),
                                  QStringLiteral("applications-all")).toString();
    m_menuPopupWidth = m_settings.value(QStringLiteral("menuPopupWidth"), 540).toInt();
    m_menuPopupHeight = m_settings.value(QStringLiteral("menuPopupHeight"), 460).toInt();
    m_menuColumns = qBound(1, m_settings.value(QStringLiteral("menuColumns"), 1).toInt(), 8);
    m_menuAppIconSize = qBound(16, m_settings.value(QStringLiteral("menuAppIconSize"), 32).toInt(), 96);
    m_menuGridSpacing = qBound(0, m_settings.value(QStringLiteral("menuGridSpacing"), 8).toInt(), 40);
    m_menuEditorApp = m_settings.value(QStringLiteral("menuEditorApp"),
                                       QStringLiteral("org.kde.kmenuedit")).toString();
    // When the menu appearance group is shared, its effective values come from
    // the shared settings file (defaults = the instance values just loaded).
    if (menuConfigShared()) {
        QSettings s(settingsFilePath(), QSettings::IniFormat);
        m_showMenuPower = s.value(QStringLiteral("showMenuPower"), m_showMenuPower).toBool();
        m_menuIcon = s.value(QStringLiteral("menuIcon"), m_menuIcon).toString();
        m_menuPopupWidth = s.value(QStringLiteral("menuPopupWidth"), m_menuPopupWidth).toInt();
        m_menuPopupHeight = s.value(QStringLiteral("menuPopupHeight"), m_menuPopupHeight).toInt();
        m_menuColumns = qBound(1, s.value(QStringLiteral("menuColumns"), m_menuColumns).toInt(), 8);
        m_menuAppIconSize = qBound(16, s.value(QStringLiteral("menuAppIconSize"),
                                               m_menuAppIconSize).toInt(), 96);
        m_menuGridSpacing = qBound(0, s.value(QStringLiteral("menuGridSpacing"),
                                              m_menuGridSpacing).toInt(), 40);
        m_menuEditorApp = s.value(QStringLiteral("menuEditorApp"), m_menuEditorApp).toString();
    }
    m_showClipboard = m_settings.value(QStringLiteral("showClipboard"), false).toBool();
    m_showDisks = m_settings.value(QStringLiteral("showDisks"), false).toBool();
    m_showNetwork = m_settings.value(QStringLiteral("showNetwork"), false).toBool();
    m_showIconThemes = m_settings.value(QStringLiteral("showIconThemes"), false).toBool();
    m_showColorSchemes = m_settings.value(QStringLiteral("showColorSchemes"), false).toBool();
    m_clipboardPopupWidth = m_settings.value(QStringLiteral("clipboardPopupWidth"), 360).toInt();
    m_clipboardPopupHeight = m_settings.value(QStringLiteral("clipboardPopupHeight"), 460).toInt();
    m_showOverview = m_settings.value(QStringLiteral("showOverview"), false).toBool();
    m_showMoveToDesktop = m_settings.value(QStringLiteral("showMoveToDesktop"), false).toBool();
    m_showMoveToScreen = m_settings.value(QStringLiteral("showMoveToScreen"), false).toBool();
    m_showMaxMin = m_settings.value(QStringLiteral("showMaxMin"), false).toBool();
    m_showCloseWindow = m_settings.value(QStringLiteral("showCloseWindow"), false).toBool();
    m_showNextWallpaper = m_settings.value(QStringLiteral("showNextWallpaper"), false).toBool();
    m_showDarkMode = m_settings.value(QStringLiteral("showDarkMode"), false).toBool();
    m_darkMode = m_settings.value(QStringLiteral("darkMode"), false).toBool();
    m_showClock2 = m_settings.value(QStringLiteral("showClock2"), false).toBool();
    m_groupWindows = m_settings.value(QStringLiteral("groupWindows"), true).toBool();
    m_menuFavorites = m_settings.value(QStringLiteral("menuFavorites"),
                                       QStringList{QStringLiteral("org.kde.dolphin"),
                                                   QStringLiteral("org.kde.konsole"),
                                                   QStringLiteral("systemsettings")})
                          .toStringList();
    // When favorites are shared, the effective list comes from the shared file.
    if (favoritesShared())
        m_menuFavorites = sharedFavorites();
    m_separator1 = m_settings.value(QStringLiteral("separator1"), -1).toInt();
    m_separator2 = m_settings.value(QStringLiteral("separator2"), -1).toInt();
    m_separatorSize = m_settings.value(QStringLiteral("separatorSize"), 16).toInt();
    m_widgetOrder = m_settings.value(QStringLiteral("widgetOrder"), knownWidgetTokens()).toStringList();
    m_dockLength = m_settings.value(QStringLiteral("dockLength"), 0).toInt();
    reconcileWidgetOrder();
}

QStringList DockConfig::knownWidgetTokens()
{
    // Order here is also the default section order (menu, apps, then widgets).
    return {QStringLiteral("menu"),        QStringLiteral("apps"),
            QStringLiteral("clipboard"),   QStringLiteral("disks"),
            QStringLiteral("network"),
            QStringLiteral("iconthemes"),  QStringLiteral("colorschemes"),
            QStringLiteral("volume"),      QStringLiteral("brightness"),
            QStringLiteral("battery"),
            QStringLiteral("clock"),       QStringLiteral("clock2"),
            QStringLiteral("overview"),    QStringLiteral("movetodesktop"),
            QStringLiteral("movetoscreen"), QStringLiteral("maxmin"),
            QStringLiteral("closewindow"),
            QStringLiteral("nextwallpaper"), QStringLiteral("darkmode"),
            QStringLiteral("autohide"),
            QStringLiteral("showdesktop"), QStringLiteral("systray"),
            QStringLiteral("relanzadores"), QStringLiteral("scriptrunners"),
            QStringLiteral("session"),     QStringLiteral("settings")};
}

void DockConfig::reconcileWidgetOrder()
{
    const QStringList known = knownWidgetTokens();
    QStringList result;
    // Keep known tokens (deduplicated) and spring tokens, in their saved order.
    for (const QString &token : std::as_const(m_widgetOrder)) {
        if (token == QLatin1String("spring")) {
            result.append(token);
        } else if (known.contains(token) && !result.contains(token)) {
            result.append(token);
        }
        // unknown tokens are dropped
    }
    // Append any known token that wasn't present (e.g. new widget after upgrade).
    for (const QString &token : known) {
        if (!result.contains(token))
            result.append(token);
    }
    m_widgetOrder = result;
}

void DockConfig::setWidgetOrder(const QStringList &order)
{
    if (m_widgetOrder == order)
        return;
    m_widgetOrder = order;
    reconcileWidgetOrder();
    m_settings.setValue(QStringLiteral("widgetOrder"), m_widgetOrder);
    emit widgetOrderChanged();
}

void DockConfig::moveSection(int from, int to)
{
    if (from == to || from < 0 || to < 0
        || from >= m_widgetOrder.size() || to >= m_widgetOrder.size())
        return;
    m_widgetOrder.move(from, to);
    m_settings.setValue(QStringLiteral("widgetOrder"), m_widgetOrder);
    emit widgetOrderChanged();
}

void DockConfig::insertSpring(int at)
{
    at = qBound(0, at, m_widgetOrder.size());
    m_widgetOrder.insert(at, QStringLiteral("spring"));
    m_settings.setValue(QStringLiteral("widgetOrder"), m_widgetOrder);
    emit widgetOrderChanged();
}

void DockConfig::removeSectionAt(int at)
{
    if (at < 0 || at >= m_widgetOrder.size())
        return;
    if (m_widgetOrder.at(at) != QLatin1String("spring"))
        return; // only dynamic separators are removable; widgets use show* flags
    m_widgetOrder.removeAt(at);
    m_settings.setValue(QStringLiteral("widgetOrder"), m_widgetOrder);
    emit widgetOrderChanged();
}

void DockConfig::setAlignment(int alignment)
{
    if (m_alignment == alignment)
        return;
    m_alignment = alignment;
    m_settings.setValue(QStringLiteral("alignment"), alignment);
    emit alignmentChanged();
}

void DockConfig::setShowVolume(bool show)
{
    if (m_showVolume == show)
        return;
    m_showVolume = show;
    m_settings.setValue(QStringLiteral("showVolume"), show);
    emit showVolumeChanged();
}

void DockConfig::setShowSystray(bool show)
{
    if (m_showSystray == show)
        return;
    m_showSystray = show;
    m_settings.setValue(QStringLiteral("showSystray"), show);
    emit showSystrayChanged();
}

void DockConfig::setSystrayHiddenItems(const QStringList &items)
{
    if (m_systrayHiddenItems == items)
        return;
    m_systrayHiddenItems = items;
    m_settings.setValue(QStringLiteral("systrayHiddenItems"), items);
    emit systrayHiddenItemsChanged();
}

void DockConfig::setRelanzadoresHidden(const QStringList &ids)
{
    if (m_relanzadoresHidden == ids)
        return;
    m_relanzadoresHidden = ids;
    m_settings.setValue(QStringLiteral("relanzadoresHidden"), ids);
    emit relanzadoresHiddenChanged();
}

void DockConfig::setRelanzadoresShown(const QStringList &ids)
{
    if (m_relanzadoresShown == ids)
        return;
    m_relanzadoresShown = ids;
    m_settings.setValue(QStringLiteral("relanzadoresShown"), ids);
    emit relanzadoresShownChanged();
}

void DockConfig::setScriptRunnersHidden(const QStringList &ids)
{
    if (m_scriptRunnersHidden == ids)
        return;
    m_scriptRunnersHidden = ids;
    m_settings.setValue(QStringLiteral("scriptRunnersHidden"), ids);
    emit scriptRunnersHiddenChanged();
}

void DockConfig::setScriptRunnersShown(const QStringList &ids)
{
    if (m_scriptRunnersShown == ids)
        return;
    m_scriptRunnersShown = ids;
    m_settings.setValue(QStringLiteral("scriptRunnersShown"), ids);
    emit scriptRunnersShownChanged();
}

void DockConfig::setShowClock(bool show)
{
    if (m_showClock == show)
        return;
    m_showClock = show;
    m_settings.setValue(QStringLiteral("showClock"), show);
    emit showClockChanged();
}

void DockConfig::setClockFormat24h(bool v)
{
    if (m_clockFormat24h == v)
        return;
    m_clockFormat24h = v;
    m_settings.setValue(QStringLiteral("clockFormat24h"), v);
    emit clockFormat24hChanged();
}

void DockConfig::setClockShowDate(bool v)
{
    if (m_clockShowDate == v)
        return;
    m_clockShowDate = v;
    m_settings.setValue(QStringLiteral("clockShowDate"), v);
    emit clockShowDateChanged();
}

void DockConfig::setClockShowSeconds(bool v)
{
    if (m_clockShowSeconds == v)
        return;
    m_clockShowSeconds = v;
    m_settings.setValue(QStringLiteral("clockShowSeconds"), v);
    emit clockShowSecondsChanged();
}

void DockConfig::setClockFontSize(int px)
{
    if (m_clockFontSize == px)
        return;
    m_clockFontSize = px;
    m_settings.setValue(QStringLiteral("clockFontSize"), px);
    emit clockFontSizeChanged();
}

void DockConfig::setClock2Command(const QString &v)
{
    if (m_clock2Command == v)
        return;
    m_clock2Command = v;
    m_settings.setValue(QStringLiteral("clock2Command"), v);
    emit clock2CommandChanged();
}

void DockConfig::setShowBrightness(bool show)
{
    if (m_showBrightness == show)
        return;
    m_showBrightness = show;
    m_settings.setValue(QStringLiteral("showBrightness"), show);
    emit showBrightnessChanged();
}

void DockConfig::setShowBattery(bool show)
{
    if (m_showBattery == show)
        return;
    m_showBattery = show;
    m_settings.setValue(QStringLiteral("showBattery"), show);
    emit showBatteryChanged();
}

void DockConfig::setShowAutohideToggle(bool show)
{
    if (m_showAutohideToggle == show)
        return;
    m_showAutohideToggle = show;
    m_settings.setValue(QStringLiteral("showAutohideToggle"), show);
    emit showAutohideToggleChanged();
}

void DockConfig::setShowDesktopButton(bool show)
{
    if (m_showDesktopButton == show)
        return;
    m_showDesktopButton = show;
    m_settings.setValue(QStringLiteral("showDesktopButton"), show);
    emit showDesktopButtonChanged();
}

void DockConfig::setIconRunningBackground(bool on)
{
    if (m_iconRunningBackground == on)
        return;
    m_iconRunningBackground = on;
    m_settings.setValue(QStringLiteral("iconRunningBackground"), on);
    emit iconRunningBackgroundChanged();
}

void DockConfig::setIconRunningDots(bool on)
{
    if (m_iconRunningDots == on)
        return;
    m_iconRunningDots = on;
    m_settings.setValue(QStringLiteral("iconRunningDots"), on);
    emit iconRunningDotsChanged();
    // Dots and the edge line are mutually exclusive.
    if (on)
        setIconRunningLine(false);
}

void DockConfig::setIconRunningLine(bool on)
{
    if (m_iconRunningLine == on)
        return;
    m_iconRunningLine = on;
    m_settings.setValue(QStringLiteral("iconRunningLine"), on);
    emit iconRunningLineChanged();
    if (on)
        setIconRunningDots(false);
}

void DockConfig::setShowMenuButton(bool show)
{
    if (m_showMenuButton == show)
        return;
    m_showMenuButton = show;
    m_settings.setValue(QStringLiteral("showMenuButton"), show);
    emit showMenuButtonChanged();
}

void DockConfig::setShowSessionButton(bool show)
{
    if (m_showSessionButton == show)
        return;
    m_showSessionButton = show;
    m_settings.setValue(QStringLiteral("showSessionButton"), show);
    emit showSessionButtonChanged();
}

void DockConfig::setShowSettingsButton(bool show)
{
    if (m_showSettingsButton == show)
        return;
    m_showSettingsButton = show;
    m_settings.setValue(QStringLiteral("showSettingsButton"), show);
    emit showSettingsButtonChanged();
}

void DockConfig::setShowMenuPower(bool show)
{
    if (m_showMenuPower == show)
        return;
    m_showMenuPower = show;
    writeMenuConfigValue(QStringLiteral("showMenuPower"), show);
    emit showMenuPowerChanged();
}

void DockConfig::setMenuIcon(const QString &name)
{
    if (m_menuIcon == name)
        return;
    m_menuIcon = name;
    writeMenuConfigValue(QStringLiteral("menuIcon"), name);
    emit menuIconChanged();
}

void DockConfig::setMenuPopupWidth(int w)
{
    if (m_menuPopupWidth == w)
        return;
    m_menuPopupWidth = w;
    writeMenuConfigValue(QStringLiteral("menuPopupWidth"), w);
    emit menuPopupWidthChanged();
}

void DockConfig::setMenuPopupHeight(int h)
{
    if (m_menuPopupHeight == h)
        return;
    m_menuPopupHeight = h;
    writeMenuConfigValue(QStringLiteral("menuPopupHeight"), h);
    emit menuPopupHeightChanged();
}

void DockConfig::setMenuColumns(int columns)
{
    columns = qBound(1, columns, 8);
    if (m_menuColumns == columns)
        return;
    m_menuColumns = columns;
    writeMenuConfigValue(QStringLiteral("menuColumns"), columns);
    emit menuColumnsChanged();
}

void DockConfig::setMenuAppIconSize(int px)
{
    px = qBound(16, px, 96);
    if (m_menuAppIconSize == px)
        return;
    m_menuAppIconSize = px;
    writeMenuConfigValue(QStringLiteral("menuAppIconSize"), px);
    emit menuAppIconSizeChanged();
}

void DockConfig::setMenuGridSpacing(int px)
{
    px = qBound(0, px, 40);
    if (m_menuGridSpacing == px)
        return;
    m_menuGridSpacing = px;
    writeMenuConfigValue(QStringLiteral("menuGridSpacing"), px);
    emit menuGridSpacingChanged();
}

void DockConfig::setMenuEditorApp(const QString &app)
{
    if (m_menuEditorApp == app)
        return;
    m_menuEditorApp = app;
    writeMenuConfigValue(QStringLiteral("menuEditorApp"), app);
    emit menuEditorAppChanged();
}

void DockConfig::setShowClipboard(bool show)
{
    if (m_showClipboard == show)
        return;
    m_showClipboard = show;
    m_settings.setValue(QStringLiteral("showClipboard"), show);
    emit showClipboardChanged();
}

void DockConfig::setShowDisks(bool show)
{
    if (m_showDisks == show)
        return;
    m_showDisks = show;
    m_settings.setValue(QStringLiteral("showDisks"), show);
    emit showDisksChanged();
}

void DockConfig::setShowNetwork(bool show)
{
    if (m_showNetwork == show)
        return;
    m_showNetwork = show;
    m_settings.setValue(QStringLiteral("showNetwork"), show);
    emit showNetworkChanged();
}

void DockConfig::setShowIconThemes(bool show)
{
    if (m_showIconThemes == show)
        return;
    m_showIconThemes = show;
    m_settings.setValue(QStringLiteral("showIconThemes"), show);
    emit showIconThemesChanged();
}

void DockConfig::setShowColorSchemes(bool show)
{
    if (m_showColorSchemes == show)
        return;
    m_showColorSchemes = show;
    m_settings.setValue(QStringLiteral("showColorSchemes"), show);
    emit showColorSchemesChanged();
}

void DockConfig::setClipboardPopupWidth(int w)
{
    if (m_clipboardPopupWidth == w)
        return;
    m_clipboardPopupWidth = w;
    m_settings.setValue(QStringLiteral("clipboardPopupWidth"), w);
    emit clipboardPopupWidthChanged();
}

void DockConfig::setClipboardPopupHeight(int h)
{
    if (m_clipboardPopupHeight == h)
        return;
    m_clipboardPopupHeight = h;
    m_settings.setValue(QStringLiteral("clipboardPopupHeight"), h);
    emit clipboardPopupHeightChanged();
}

void DockConfig::setShowOverview(bool show)
{
    if (m_showOverview == show)
        return;
    m_showOverview = show;
    m_settings.setValue(QStringLiteral("showOverview"), show);
    emit showOverviewChanged();
}

void DockConfig::setShowClock2(bool show)
{
    if (m_showClock2 == show)
        return;
    m_showClock2 = show;
    m_settings.setValue(QStringLiteral("showClock2"), show);
    emit showClock2Changed();
}

void DockConfig::setShowMoveToDesktop(bool show)
{
    if (m_showMoveToDesktop == show)
        return;
    m_showMoveToDesktop = show;
    m_settings.setValue(QStringLiteral("showMoveToDesktop"), show);
    emit showMoveToDesktopChanged();
}

void DockConfig::setShowMoveToScreen(bool show)
{
    if (m_showMoveToScreen == show)
        return;
    m_showMoveToScreen = show;
    m_settings.setValue(QStringLiteral("showMoveToScreen"), show);
    emit showMoveToScreenChanged();
}

void DockConfig::setShowMaxMin(bool show)
{
    if (m_showMaxMin == show)
        return;
    m_showMaxMin = show;
    m_settings.setValue(QStringLiteral("showMaxMin"), show);
    emit showMaxMinChanged();
}

void DockConfig::setShowCloseWindow(bool show)
{
    if (m_showCloseWindow == show)
        return;
    m_showCloseWindow = show;
    m_settings.setValue(QStringLiteral("showCloseWindow"), show);
    emit showCloseWindowChanged();
}

void DockConfig::setShowNextWallpaper(bool show)
{
    if (m_showNextWallpaper == show)
        return;
    m_showNextWallpaper = show;
    m_settings.setValue(QStringLiteral("showNextWallpaper"), show);
    emit showNextWallpaperChanged();
}

void DockConfig::setShowDarkMode(bool show)
{
    if (m_showDarkMode == show)
        return;
    m_showDarkMode = show;
    m_settings.setValue(QStringLiteral("showDarkMode"), show);
    emit showDarkModeChanged();
}

void DockConfig::setDarkMode(bool on)
{
    if (m_darkMode == on)
        return;
    m_darkMode = on;
    m_settings.setValue(QStringLiteral("darkMode"), on);
    emit darkModeChanged();
}

void DockConfig::setGroupWindows(bool group)
{
    if (m_groupWindows == group)
        return;
    m_groupWindows = group;
    m_settings.setValue(QStringLiteral("groupWindows"), group);
    emit groupWindowsChanged();
}

void DockConfig::setMenuFavorites(const QStringList &favorites)
{
    if (favoritesShared()) {
        // Persist to the shared file and propagate to every live dock so all
        // open menus update at once (extra instances read the same list).
        if (sharedFavorites() == favorites)
            return;
        setSharedFavorites(favorites);
        for (DockConfig *cfg : std::as_const(s_instances)) {
            if (cfg->m_menuFavorites == favorites)
                continue;
            cfg->m_menuFavorites = favorites;
            emit cfg->menuFavoritesChanged();
        }
        return;
    }
    if (m_menuFavorites == favorites)
        return;
    m_menuFavorites = favorites;
    m_settings.setValue(QStringLiteral("menuFavorites"), favorites);
    emit menuFavoritesChanged();
}

void DockConfig::reloadFavorites()
{
    const QStringList favs = favoritesShared()
                                 ? sharedFavorites()
                                 : m_settings.value(QStringLiteral("menuFavorites")).toStringList();
    if (m_menuFavorites == favs)
        return;
    m_menuFavorites = favs;
    emit menuFavoritesChanged();
}

void DockConfig::setSeparator1(int pos)
{
    if (m_separator1 == pos)
        return;
    m_separator1 = pos;
    m_settings.setValue(QStringLiteral("separator1"), pos);
    emit separator1Changed();
}

void DockConfig::setSeparator2(int pos)
{
    if (m_separator2 == pos)
        return;
    m_separator2 = pos;
    m_settings.setValue(QStringLiteral("separator2"), pos);
    emit separator2Changed();
}

void DockConfig::setSeparatorSize(int size)
{
    if (m_separatorSize == size)
        return;
    m_separatorSize = size;
    m_settings.setValue(QStringLiteral("separatorSize"), size);
    emit separatorSizeChanged();
}

void DockConfig::setDockLength(int percent)
{
    if (percent < 0)
        percent = 0;
    if (percent > 100)
        percent = 100;
    if (m_dockLength == percent)
        return;
    m_dockLength = percent;
    m_settings.setValue(QStringLiteral("dockLength"), percent);
    emit dockLengthChanged();
}

void DockConfig::setScreenName(const QString &name)
{
    if (m_screenName == name)
        return;
    m_screenName = name;
    m_settings.setValue(QStringLiteral("screenName"), name);
    emit screenNameChanged();
}

void DockConfig::setPanelMode(bool panelMode)
{
    if (m_panelMode == panelMode)
        return;
    m_panelMode = panelMode;
    m_settings.setValue(QStringLiteral("panelMode"), panelMode);
    emit panelModeChanged();
}

void DockConfig::setCompact(bool compact)
{
    if (m_compact == compact)
        return;
    m_compact = compact;
    m_settings.setValue(QStringLiteral("compact"), compact);
    emit compactChanged();
    emit effectiveMarginChanged();
    emit dockThicknessChanged(); // padding and the icon<->label gap both shrink
}

void DockConfig::setEdge(int edge)
{
    if (m_edge == edge)
        return;
    m_edge = edge;
    m_settings.setValue(QStringLiteral("edge"), edge);
    emit edgeChanged();
    emit dockThicknessChanged(); // labels grow the dock along one axis only
}

void DockConfig::setIconSize(int size)
{
    if (m_iconSize == size)
        return;
    m_iconSize = size;
    m_settings.setValue(QStringLiteral("iconSize"), size);
    emit iconSizeChanged();
    emit widgetIconSizeChanged();
    emit systrayIconSizeChanged();
    emit dockThicknessChanged();
}

int DockConfig::iconLabelFontPx() const
{
    return m_iconLabelFontSize > 0 ? m_iconLabelFontSize
                                   : qMax(9, qRound(m_iconSize * 0.22));
}

int DockConfig::iconLabelLineHeight() const
{
    return qRound(iconLabelFontPx() * 1.4);
}

// Cross-axis extent of an icon box of `iconPx` with the label laid out around
// it in `mode`. The label only grows the thickness when it sits along the
// dock's cross axis: below/above on a horizontal dock, left/right on a
// vertical one. Shared by the app cells and the widget sections.
int DockConfig::cellThicknessFor(int mode, int iconPx) const
{
    const bool horizontal = (m_edge == Bottom || m_edge == Top);
    const int gap = iconLabelGap();
    // Only the vertical branches use the name box at all: on a horizontal dock
    // the box is part of the cell *length*, and each cell already shrinks to its
    // own name there (see labelW in Dock.qml). See effectiveLabelWidth().
    const int labelW = effectiveLabelWidth();
    switch (mode) {
    case LabelBelow:
    case LabelAbove:
        return horizontal ? iconPx + gap + iconLabelLineHeight()
                          : qMax(iconPx, labelW);
    case LabelRight:
    case LabelLeft:
        return horizontal ? qMax(iconPx, iconLabelLineHeight())
                          : iconPx + gap + labelW;
    case LabelOnly:
        return horizontal ? iconLabelLineHeight() : labelW;
    }
    return iconPx;
}

void DockConfig::setMeasuredLabelWidth(int px)
{
    px = qMax(0, px); // only Dock.qml calls this, and only with a real measurement
    if (m_measuredLabelWidth == px)
        return;
    m_measuredLabelWidth = px;
    // The dock's cross-axis size just changed, so the layer-shell exclusive zone
    // has to follow (DockWindow re-applies its layer properties on this signal).
    emit dockThicknessChanged();
}

int DockConfig::appCellThickness() const
{
    return cellThicknessFor(m_iconLabelMode, m_iconSize);
}

int DockConfig::widgetCellThickness() const
{
    // Widget icons are scaled down, but blocks (systray, relanzadores…) draw
    // taller content, so the app icon size stays the floor.
    return cellThicknessFor(m_widgetLabelMode, qMax(m_iconSize, widgetIconSize()));
}

int DockConfig::dockThickness() const
{
    // The icon size stays the floor even in label-only mode: the widget
    // sections (volume, clock, systray…) are still drawn at that size.
    return qMax(m_iconSize, qMax(appCellThickness(), widgetCellThickness()))
           + (m_compact ? 12 : 20);
}

void DockConfig::setIconLabelMode(int mode)
{
    mode = qBound(int(IconOnly), mode, int(LabelLeft));
    if (m_iconLabelMode == mode)
        return;
    m_iconLabelMode = mode;
    m_settings.setValue(QStringLiteral("iconLabelMode"), mode);
    emit iconLabelModeChanged();
    emit dockThicknessChanged();
}

void DockConfig::setIconLabelWidth(int px)
{
    px = qBound(60, px, 400);
    if (m_iconLabelWidth == px)
        return;
    m_iconLabelWidth = px;
    m_settings.setValue(QStringLiteral("iconLabelWidth"), px);
    emit iconLabelWidthChanged();
    emit dockThicknessChanged();
}

void DockConfig::setIconLabelFontSize(int px)
{
    px = qBound(0, px, 48);
    if (m_iconLabelFontSize == px)
        return;
    m_iconLabelFontSize = px;
    m_settings.setValue(QStringLiteral("iconLabelFontSize"), px);
    emit iconLabelFontSizeChanged();
    emit dockThicknessChanged();
}

void DockConfig::setWidgetLabelMode(int mode)
{
    mode = sanitizedWidgetLabelMode(mode);
    if (m_widgetLabelMode == mode)
        return;
    m_widgetLabelMode = mode;
    m_settings.setValue(QStringLiteral("widgetLabelMode"), mode);
    emit widgetLabelModeChanged();
    emit dockThicknessChanged();
}

void DockConfig::setLabelBold(bool on)
{
    if (m_labelBold == on)
        return;
    m_labelBold = on;
    m_settings.setValue(QStringLiteral("labelBold"), on);
    emit labelBoldChanged();
    // Bold names are wider; Dock.qml re-measures on this signal and reports the
    // new width back, which is what actually moves the thickness.
}

void DockConfig::setAutoShrinkIcons(bool on)
{
    if (m_autoShrinkIcons == on)
        return;
    m_autoShrinkIcons = on;
    m_settings.setValue(QStringLiteral("autoShrinkIcons"), on);
    emit autoShrinkIconsChanged();
}

void DockConfig::setAutoShrinkMinIconSize(int px)
{
    px = qBound(8, px, 64);
    if (m_autoShrinkMinIconSize == px)
        return;
    m_autoShrinkMinIconSize = px;
    m_settings.setValue(QStringLiteral("autoShrinkMinIconSize"), px);
    emit autoShrinkMinIconSizeChanged();
}

QString DockConfig::defaultWidgetLabel(const QString &token)
{
    static const QHash<QString, QString> labels = {
        {QStringLiteral("menu"),          tr("Application menu")},
        {QStringLiteral("apps"),          tr("Applications")},
        {QStringLiteral("clipboard"),     tr("Clipboard")},
        {QStringLiteral("disks"),         tr("Disks")},
        {QStringLiteral("network"),       tr("Network")},
        {QStringLiteral("iconthemes"),    tr("Icon theme")},
        {QStringLiteral("colorschemes"),  tr("Color scheme")},
        {QStringLiteral("volume"),        tr("Volume")},
        {QStringLiteral("brightness"),    tr("Brightness")},
        {QStringLiteral("battery"),       tr("Battery")},
        {QStringLiteral("clock"),         tr("Clock")},
        {QStringLiteral("clock2"),        tr("Clock 2")},
        {QStringLiteral("overview"),      tr("Overview")},
        {QStringLiteral("movetodesktop"), tr("Move to desktop")},
        {QStringLiteral("movetoscreen"),  tr("Move to monitor")},
        {QStringLiteral("maxmin"),        tr("MaxMin")},
        {QStringLiteral("closewindow"),   tr("Close window")},
        {QStringLiteral("nextwallpaper"), tr("Next wallpaper")},
        {QStringLiteral("darkmode"),      tr("Modo oscuro")},
        {QStringLiteral("autohide"),      tr("Auto-hide toggle")},
        {QStringLiteral("showdesktop"),   tr("Show desktop")},
        {QStringLiteral("systray"),       tr("System tray")},
        {QStringLiteral("relanzadores"),  tr("Relanzadores")},
        {QStringLiteral("scriptrunners"), tr("Script Runner")},
        {QStringLiteral("session"),       tr("Session / power")},
        {QStringLiteral("settings"),      tr("Settings button")},
        {QStringLiteral("spring"),        tr("Dynamic separator")},
    };
    return labels.value(token, token);
}

QString DockConfig::widgetName(const QString &token) const
{
    const QString custom = m_widgetNames.value(token);
    return custom.isEmpty() ? defaultWidgetLabel(token) : custom;
}

void DockConfig::setWidgetName(const QString &token, const QString &name)
{
    const QString trimmed = name.trimmed();
    // Storing the default name would only make a later default change invisible.
    const QString value = (trimmed == defaultWidgetLabel(token)) ? QString() : trimmed;
    if (m_widgetNames.value(token) == value)
        return;
    const QString key = QStringLiteral("widgetNames/") + token;
    if (value.isEmpty()) {
        m_widgetNames.remove(token);
        m_settings.remove(key);
    } else {
        m_widgetNames.insert(token, value);
        m_settings.setValue(key, value);
    }
    ++m_widgetNamesRevision;
    emit widgetNamesChanged();
}

void DockConfig::setWidgetIconScale(int percent)
{
    percent = qBound(20, percent, 100);
    if (m_widgetIconScale == percent)
        return;
    m_widgetIconScale = percent;
    m_settings.setValue(QStringLiteral("widgetIconScale"), percent);
    emit widgetIconScaleChanged();
    emit widgetIconSizeChanged();
}

void DockConfig::setWidgetIconThemeMode(int mode)
{
    mode = qBound(int(FollowIconTheme), mode, int(AlwaysDarkBg));
    if (m_widgetIconThemeMode == mode)
        return;
    m_widgetIconThemeMode = mode;
    m_settings.setValue(QStringLiteral("widgetIconThemeMode"), mode);
    emit widgetIconThemeChanged();
}

void DockConfig::setWidgetIconThemeLightBg(const QString &themeId)
{
    if (m_widgetIconThemeLightBg == themeId)
        return;
    m_widgetIconThemeLightBg = themeId;
    m_settings.setValue(QStringLiteral("widgetIconThemeLightBg"), themeId);
    emit widgetIconThemeChanged();
}

void DockConfig::setWidgetIconThemeDarkBg(const QString &themeId)
{
    if (m_widgetIconThemeDarkBg == themeId)
        return;
    m_widgetIconThemeDarkBg = themeId;
    m_settings.setValue(QStringLiteral("widgetIconThemeDarkBg"), themeId);
    emit widgetIconThemeChanged();
}

void DockConfig::setSystrayIconScale(int percent)
{
    percent = qBound(20, percent, 100);
    if (m_systrayIconScale == percent)
        return;
    m_systrayIconScale = percent;
    m_settings.setValue(QStringLiteral("systrayIconScale"), percent);
    emit systrayIconScaleChanged();
    emit systrayIconSizeChanged();
}

void DockConfig::setSpacing(int spacing)
{
    if (m_spacing == spacing)
        return;
    m_spacing = spacing;
    m_settings.setValue(QStringLiteral("spacing"), spacing);
    emit spacingChanged();
}

void DockConfig::setScreenMargin(int margin)
{
    if (m_screenMargin == margin)
        return;
    m_screenMargin = margin;
    m_settings.setValue(QStringLiteral("screenMargin"), margin);
    emit screenMarginChanged();
    emit effectiveMarginChanged();
}

void DockConfig::setAutohide(bool autohide)
{
    if (m_autohide == autohide)
        return;
    m_autohide = autohide;
    m_settings.setValue(QStringLiteral("autohide"), autohide);
    emit autohideChanged();
}

void DockConfig::setOpacity(qreal opacity)
{
    if (qFuzzyCompare(m_opacity, opacity))
        return;
    m_opacity = opacity;
    m_settings.setValue(QStringLiteral("opacity"), opacity);
    emit opacityChanged();
}

void DockConfig::setPanelColor(const QColor &color)
{
    if (m_panelColor == color)
        return;
    m_panelColor = color;
    m_settings.setValue(QStringLiteral("panelColor"),
                        color.isValid() ? color.name(QColor::HexRgb) : QString());
    emit panelColorChanged();
}

void DockConfig::resetPanelColor()
{
    setPanelColor(QColor()); // invalid = inherit theme
}

void DockConfig::setPanelPresetColors(const QStringList &colors)
{
    // Normalize to exactly four entries.
    QStringList v = colors;
    while (v.size() < 4)
        v.append(QStringLiteral("#31363b"));
    if (v.size() > 4)
        v = v.mid(0, 4);
    if (m_panelPresetColors == v)
        return;
    m_panelPresetColors = v;
    m_settings.setValue(QStringLiteral("panelPresetColors"), v);
    emit panelPresetColorsChanged();
}

void DockConfig::setPanelImage(const QString &path)
{
    if (m_panelImage == path)
        return;
    m_panelImage = path;
    m_settings.setValue(QStringLiteral("panelImage"), path);
    emit panelImageChanged();
}

void DockConfig::setPinned(const QStringList &pinned)
{
    if (m_pinned == pinned)
        return;
    m_pinned = pinned;
    m_settings.setValue(QStringLiteral("pinned"), pinned);
    emit pinnedChanged();
}
