#include "systrayconfig.h"

#include <QStandardPaths>

QString SystrayConfig::settingsFilePath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
           + QStringLiteral("/kdock/systray.conf");
}

SystrayConfig::SystrayConfig(QObject *parent)
    : QObject(parent)
    , m_settings(settingsFilePath(), QSettings::IniFormat)
{
    load();
}

void SystrayConfig::load()
{
    const auto readInt = [this](const char *key, int def) {
        return m_settings.value(QLatin1String(key), def).toInt();
    };
    m_edge = readInt("edge", m_edge);
    m_alignment = readInt("alignment", m_alignment);
    m_windowWidth = readInt("windowWidth", m_windowWidth);
    m_windowHeight = readInt("windowHeight", m_windowHeight);
    m_windowWidthPercent = readInt("windowWidthPercent", m_windowWidthPercent);
    m_windowHeightPercent = readInt("windowHeightPercent", m_windowHeightPercent);
    m_screenMargin = readInt("screenMargin", m_screenMargin);
    m_keepOpen = m_settings.value(QStringLiteral("keepOpen"), m_keepOpen).toBool();
    m_closeOnFocusLoss = m_settings.value(QStringLiteral("closeOnFocusLoss"),
                                          m_closeOnFocusLoss).toBool();
    m_iconSize = readInt("iconSize", m_iconSize);
    m_iconSpacing = readInt("iconSpacing", m_iconSpacing);
    m_columns = readInt("columns", m_columns);
    m_backgroundOpacity = m_settings.value(QStringLiteral("backgroundOpacity"),
                                           m_backgroundOpacity).toReal();
    m_cornerRadius = readInt("cornerRadius", m_cornerRadius);
    m_showTooltips = m_settings.value(QStringLiteral("showTooltips"), m_showTooltips).toBool();
    m_hiddenItems = m_settings.value(QStringLiteral("hiddenItems")).toStringList();
}

void SystrayConfig::reloadFromDisk()
{
    m_settings.sync();
    load();
    emit windowChanged();
    emit settingsChanged();
    emit hiddenItemsChanged();
}

void SystrayConfig::store(const QString &key, const QVariant &value)
{
    m_settings.setValue(key, value);
    m_settings.sync();
    emit settingsChanged();
}

void SystrayConfig::storeWindow(const QString &key, const QVariant &value)
{
    m_settings.setValue(key, value);
    m_settings.sync();
    emit windowChanged();
}

int SystrayConfig::windowWidthFor(int screenWidth) const
{
    if (m_windowWidthPercent > 0)
        return qMax(120, screenWidth * m_windowWidthPercent / 100);
    return qMax(120, m_windowWidth);
}

int SystrayConfig::windowHeightFor(int screenHeight) const
{
    if (m_windowHeightPercent > 0)
        return qMax(60, screenHeight * m_windowHeightPercent / 100);
    return qMax(60, m_windowHeight);
}

void SystrayConfig::setEdge(int edge)
{
    edge = qBound(0, edge, 3);
    if (m_edge == edge)
        return;
    m_edge = edge;
    storeWindow(QStringLiteral("edge"), edge);
}

void SystrayConfig::setAlignment(int alignment)
{
    alignment = qBound(0, alignment, 2);
    if (m_alignment == alignment)
        return;
    m_alignment = alignment;
    storeWindow(QStringLiteral("alignment"), alignment);
}

void SystrayConfig::setWindowWidth(int px)
{
    if (m_windowWidth == px)
        return;
    m_windowWidth = px;
    storeWindow(QStringLiteral("windowWidth"), px);
}

void SystrayConfig::setWindowHeight(int px)
{
    if (m_windowHeight == px)
        return;
    m_windowHeight = px;
    storeWindow(QStringLiteral("windowHeight"), px);
}

void SystrayConfig::setWindowWidthPercent(int percent)
{
    percent = qBound(0, percent, 100);
    if (m_windowWidthPercent == percent)
        return;
    m_windowWidthPercent = percent;
    storeWindow(QStringLiteral("windowWidthPercent"), percent);
}

void SystrayConfig::setWindowHeightPercent(int percent)
{
    percent = qBound(0, percent, 100);
    if (m_windowHeightPercent == percent)
        return;
    m_windowHeightPercent = percent;
    storeWindow(QStringLiteral("windowHeightPercent"), percent);
}

void SystrayConfig::setScreenMargin(int px)
{
    px = qMax(0, px);
    if (m_screenMargin == px)
        return;
    m_screenMargin = px;
    storeWindow(QStringLiteral("screenMargin"), px);
}

void SystrayConfig::setKeepOpen(bool on)
{
    if (m_keepOpen == on)
        return;
    m_keepOpen = on;
    store(QStringLiteral("keepOpen"), on);
}

void SystrayConfig::setCloseOnFocusLoss(bool on)
{
    if (m_closeOnFocusLoss == on)
        return;
    m_closeOnFocusLoss = on;
    store(QStringLiteral("closeOnFocusLoss"), on);
}

void SystrayConfig::setIconSize(int px)
{
    px = qBound(12, px, 128);
    if (m_iconSize == px)
        return;
    m_iconSize = px;
    store(QStringLiteral("iconSize"), px);
}

void SystrayConfig::setIconSpacing(int px)
{
    px = qBound(0, px, 64);
    if (m_iconSpacing == px)
        return;
    m_iconSpacing = px;
    store(QStringLiteral("iconSpacing"), px);
}

void SystrayConfig::setColumns(int columns)
{
    columns = qMax(0, columns);
    if (m_columns == columns)
        return;
    m_columns = columns;
    store(QStringLiteral("columns"), columns);
}

void SystrayConfig::setBackgroundOpacity(qreal opacity)
{
    opacity = qBound(0.0, opacity, 1.0);
    if (qFuzzyCompare(m_backgroundOpacity, opacity))
        return;
    m_backgroundOpacity = opacity;
    store(QStringLiteral("backgroundOpacity"), opacity);
}

void SystrayConfig::setCornerRadius(int px)
{
    px = qBound(0, px, 40);
    if (m_cornerRadius == px)
        return;
    m_cornerRadius = px;
    store(QStringLiteral("cornerRadius"), px);
}

void SystrayConfig::setShowTooltips(bool on)
{
    if (m_showTooltips == on)
        return;
    m_showTooltips = on;
    store(QStringLiteral("showTooltips"), on);
}

void SystrayConfig::setHiddenItems(const QStringList &items)
{
    if (m_hiddenItems == items)
        return;
    m_hiddenItems = items;
    m_settings.setValue(QStringLiteral("hiddenItems"), items);
    m_settings.sync();
    emit hiddenItemsChanged();
}

bool SystrayConfig::preload()
{
    QSettings s(settingsFilePath(), QSettings::IniFormat);
    // The SNI host/watcher has to be resident to keep collecting tray items over
    // the whole session, so this defaults to true — the accessory that most needs
    // to be up before the tray clients register.
    return s.value(QStringLiteral("preload"), true).toBool();
}

void SystrayConfig::setPreload(bool on)
{
    QSettings s(settingsFilePath(), QSettings::IniFormat);
    s.setValue(QStringLiteral("preload"), on);
}
