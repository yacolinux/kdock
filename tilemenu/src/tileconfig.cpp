#include "tileconfig.h"

#include "dockconfig.h"

#include <QDir>
#include <QStandardPaths>

QString TileConfig::settingsFilePath()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
                        + QStringLiteral("/kdock");
    QDir().mkpath(dir);
    return dir + QStringLiteral("/tilemenu.conf");
}

TileConfig::TileConfig(QObject *parent)
    : QObject(parent)
    , m_settings(settingsFilePath(), QSettings::IniFormat)
{
    load();
}

void TileConfig::load()
{
    const auto readInt = [this](const char *key, int def, int lo, int hi) {
        return qBound(lo, m_settings.value(QLatin1String(key), def).toInt(), hi);
    };
    const auto readBool = [this](const char *key, bool def) {
        return m_settings.value(QLatin1String(key), def).toBool();
    };

    // 0 columns means "fit as many as the width allows"; the upper bound keeps a
    // fat-fingered value from producing cells narrower than an icon.
    m_columns = readInt("columns", 10, 0, 40);
    m_cellSize = readInt("cellSize", 96, 32, 400);
    m_cellStretch = readBool("cellStretch", true);
    m_cellMin = readInt("cellMin", 48, 32, 400);
    m_cellMax = readInt("cellMax", 220, 32, 600);
    m_cellSpacing = readInt("cellSpacing", 8, 0, 48);
    m_sidebar = readInt("sidebar", 0, 0, 2);
    m_sidebarWidth = readInt("sidebarWidth", 200, 120, 480);
    m_showIcons = readBool("showIcons", true);
    m_showLabels = readBool("showLabels", true);
    m_iconScale = readInt("iconScale", 55, 20, 100);
    m_labelPosition = readInt("labelPosition", 0, 0, 1);
    m_backgroundMode = readInt("backgroundMode", 0, 0, 1);
    m_backgroundColor = QColor(m_settings.value(QStringLiteral("backgroundColor")).toString());
    m_backgroundOpacity =
        qBound(0.10, m_settings.value(QStringLiteral("backgroundOpacity"), 0.92).toDouble(), 1.0);
    m_backgroundImage = m_settings.value(QStringLiteral("backgroundImage")).toString();
    m_showSearch = readBool("showSearch", true);
    m_showPower = readBool("showPower", true);
    m_showLetterIndex = readBool("showLetterIndex", true);
    m_closeOnLaunch = readBool("closeOnLaunch", true);
    m_closeOnFocusLoss = readBool("closeOnFocusLoss", true);
    m_keepOpen = readBool("keepOpen", false);
    m_rememberSection = readBool("rememberSection", true);
    m_lastSection = m_settings.value(QStringLiteral("lastSection")).toString();

    if (m_cellMax < m_cellMin)
        m_cellMax = m_cellMin;
}

void TileConfig::store(const QString &key, const QVariant &value)
{
    m_settings.setValue(key, value);
    emit settingsChanged();
}

QUrl TileConfig::backgroundImageUrl() const
{
    return m_backgroundImage.isEmpty() ? QUrl() : QUrl::fromLocalFile(m_backgroundImage);
}

QStringList TileConfig::presetColors() const
{
    // kdock's shared file, so the tile color menu offers exactly the eight quick
    // colors the dock's own submenu offers. normalizedPresetColors() pads a
    // short (legacy four-color) list so the menu can index it safely.
    QSettings shared(DockConfig::settingsFilePath(), QSettings::IniFormat);
    return DockConfig::normalizedPresetColors(
        shared.value(QStringLiteral("panelPresetColors")).toStringList());
}

void TileConfig::setColumns(int columns)
{
    columns = qBound(0, columns, 40);
    if (m_columns == columns)
        return;
    m_columns = columns;
    store(QStringLiteral("columns"), columns);
}

void TileConfig::setCellSize(int px)
{
    px = qBound(32, px, 400);
    if (m_cellSize == px)
        return;
    m_cellSize = px;
    store(QStringLiteral("cellSize"), px);
}

void TileConfig::setCellStretch(bool on)
{
    if (m_cellStretch == on)
        return;
    m_cellStretch = on;
    store(QStringLiteral("cellStretch"), on);
}

void TileConfig::setCellMin(int px)
{
    px = qBound(32, px, 400);
    if (m_cellMin == px)
        return;
    m_cellMin = px;
    store(QStringLiteral("cellMin"), px);
}

void TileConfig::setCellMax(int px)
{
    px = qBound(32, px, 600);
    if (m_cellMax == px)
        return;
    m_cellMax = px;
    store(QStringLiteral("cellMax"), px);
}

void TileConfig::setCellSpacing(int px)
{
    px = qBound(0, px, 48);
    if (m_cellSpacing == px)
        return;
    m_cellSpacing = px;
    store(QStringLiteral("cellSpacing"), px);
}

void TileConfig::setSidebar(int mode)
{
    mode = qBound(0, mode, 2);
    if (m_sidebar == mode)
        return;
    m_sidebar = mode;
    store(QStringLiteral("sidebar"), mode);
}

void TileConfig::setSidebarWidth(int px)
{
    px = qBound(120, px, 480);
    if (m_sidebarWidth == px)
        return;
    m_sidebarWidth = px;
    store(QStringLiteral("sidebarWidth"), px);
}

void TileConfig::setShowIcons(bool on)
{
    if (m_showIcons == on)
        return;
    m_showIcons = on;
    store(QStringLiteral("showIcons"), on);
}

void TileConfig::setShowLabels(bool on)
{
    if (m_showLabels == on)
        return;
    m_showLabels = on;
    store(QStringLiteral("showLabels"), on);
}

void TileConfig::setIconScale(int percent)
{
    percent = qBound(20, percent, 100);
    if (m_iconScale == percent)
        return;
    m_iconScale = percent;
    store(QStringLiteral("iconScale"), percent);
}

void TileConfig::setLabelPosition(int position)
{
    position = qBound(0, position, 1);
    if (m_labelPosition == position)
        return;
    m_labelPosition = position;
    store(QStringLiteral("labelPosition"), position);
}

void TileConfig::setBackgroundMode(int mode)
{
    mode = qBound(0, mode, 1);
    if (m_backgroundMode == mode)
        return;
    m_backgroundMode = mode;
    store(QStringLiteral("backgroundMode"), mode);
}

void TileConfig::setBackgroundColor(const QColor &color)
{
    if (m_backgroundColor == color)
        return;
    m_backgroundColor = color;
    store(QStringLiteral("backgroundColor"), color.isValid() ? color.name() : QString());
}

void TileConfig::setBackgroundOpacity(qreal opacity)
{
    opacity = qBound(0.10, opacity, 1.0);
    if (qFuzzyCompare(m_backgroundOpacity, opacity))
        return;
    m_backgroundOpacity = opacity;
    store(QStringLiteral("backgroundOpacity"), opacity);
}

void TileConfig::setBackgroundImage(const QString &path)
{
    if (m_backgroundImage == path)
        return;
    m_backgroundImage = path;
    store(QStringLiteral("backgroundImage"), path);
}

void TileConfig::setShowSearch(bool on)
{
    if (m_showSearch == on)
        return;
    m_showSearch = on;
    store(QStringLiteral("showSearch"), on);
}

void TileConfig::setShowPower(bool on)
{
    if (m_showPower == on)
        return;
    m_showPower = on;
    store(QStringLiteral("showPower"), on);
}

void TileConfig::setShowLetterIndex(bool on)
{
    if (m_showLetterIndex == on)
        return;
    m_showLetterIndex = on;
    store(QStringLiteral("showLetterIndex"), on);
}

void TileConfig::setCloseOnLaunch(bool on)
{
    if (m_closeOnLaunch == on)
        return;
    m_closeOnLaunch = on;
    store(QStringLiteral("closeOnLaunch"), on);
}

void TileConfig::setCloseOnFocusLoss(bool on)
{
    if (m_closeOnFocusLoss == on)
        return;
    m_closeOnFocusLoss = on;
    store(QStringLiteral("closeOnFocusLoss"), on);
}

void TileConfig::setKeepOpen(bool on)
{
    if (m_keepOpen == on)
        return;
    m_keepOpen = on;
    store(QStringLiteral("keepOpen"), on);
}

void TileConfig::setRememberSection(bool on)
{
    if (m_rememberSection == on)
        return;
    m_rememberSection = on;
    store(QStringLiteral("rememberSection"), on);
}

void TileConfig::setLastSection(const QString &section)
{
    if (m_lastSection == section)
        return;
    m_lastSection = section;
    // Deliberately no settingsChanged(): the current section is what *caused*
    // this write, so repainting from it would loop through the QML binding that
    // set it in the first place.
    m_settings.setValue(QStringLiteral("lastSection"), section);
}

QString TileConfig::layoutJson() const
{
    return m_settings.value(QStringLiteral("layout")).toString();
}

void TileConfig::setLayoutJson(const QString &json)
{
    // No settingsChanged() either: the layout has its own, finer-grained signal
    // and TileLayout — which just wrote this — is the one that emits it.
    m_settings.setValue(QStringLiteral("layout"), json);
    m_settings.sync();
}
