#include "previewconfig.h"

#include <QDir>
#include <QStandardPaths>

namespace {
QString configDir()
{
    // Same directory as kdock's own settings: one place for everything the dock
    // family persists (and one place for the user to back up).
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
        + QStringLiteral("/kdock");
    QDir().mkpath(dir);
    return dir;
}
} // namespace

QString PreviewConfig::settingsFilePath()
{
    return configDir() + QStringLiteral("/previews.conf");
}

QString PreviewConfig::instanceSettingsFilePath(const QString &screenName)
{
    if (screenName.isEmpty())
        return settingsFilePath();
    // Sanitize so odd output names never escape the directory.
    QString safe = screenName;
    for (QChar &c : safe) {
        if (!c.isLetterOrNumber() && c != QLatin1Char('-') && c != QLatin1Char('_'))
            c = QLatin1Char('_');
    }
    return configDir() + QStringLiteral("/previews-") + safe + QStringLiteral(".conf");
}

bool PreviewConfig::previewsEnabled()
{
    QSettings s(settingsFilePath(), QSettings::IniFormat);
    return s.value(QStringLiteral("enabled"), false).toBool();
}

void PreviewConfig::setPreviewsEnabled(bool enabled)
{
    QSettings s(settingsFilePath(), QSettings::IniFormat);
    s.setValue(QStringLiteral("enabled"), enabled);
}

QStringList PreviewConfig::enabledScreens()
{
    QSettings s(settingsFilePath(), QSettings::IniFormat);
    return s.value(QStringLiteral("enabledScreens")).toStringList();
}

void PreviewConfig::setScreenEnabled(const QString &screenName, bool enabled)
{
    if (screenName.isEmpty())
        return;
    QSettings s(settingsFilePath(), QSettings::IniFormat);
    QStringList list = s.value(QStringLiteral("enabledScreens")).toStringList();
    const bool present = list.contains(screenName);
    if (enabled && !present)
        list.append(screenName);
    else if (!enabled && present)
        list.removeAll(screenName);
    else
        return;
    s.setValue(QStringLiteral("enabledScreens"), list);
}

QStringList PreviewConfig::knownScreens()
{
    QSettings s(settingsFilePath(), QSettings::IniFormat);
    return s.value(QStringLiteral("knownScreens")).toStringList();
}

void PreviewConfig::addKnownScreen(const QString &screenName)
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

PreviewConfig::PreviewConfig(const QString &screenName, QObject *parent)
    : QObject(parent)
    , m_settings(instanceSettingsFilePath(screenName), QSettings::IniFormat)
    , m_screenName(screenName)
{
    load();
}

void PreviewConfig::load()
{
    m_edge = qBound(int(Bottom), m_settings.value(QStringLiteral("edge"), Left).toInt(), int(Right));
    m_alignment = qBound(int(Start), m_settings.value(QStringLiteral("alignment"), Center).toInt(),
                         int(End));
    m_opacity = qBound(0.0, m_settings.value(QStringLiteral("opacity"), 0.85).toDouble(), 1.0);
    const QString color = m_settings.value(QStringLiteral("panelColor")).toString();
    if (!color.isEmpty())
        m_panelColor = QColor(color);
    m_panelPresetColors = m_settings.value(QStringLiteral("panelPresetColors")).toStringList();
    if (m_panelPresetColors.isEmpty()) {
        // Four swatches so the right-click colour submenu is useful out of the
        // box (the panel's colour picker covers everything else).
        m_panelPresetColors = {QStringLiteral("#000000"), QStringLiteral("#1b1e2b"),
                               QStringLiteral("#2e3440"), QStringLiteral("#3daee9")};
    }
    m_stripThickness = qBound(120, m_settings.value(QStringLiteral("stripThickness"), 260).toInt(), 800);
    m_stripLength = qBound(0, m_settings.value(QStringLiteral("stripLength"), 0).toInt(), 100);
    m_screenMargin = qBound(0, m_settings.value(QStringLiteral("screenMargin"), 4).toInt(), 200);
    m_reserveSpace = m_settings.value(QStringLiteral("reserveSpace"), true).toBool();
    m_autohide = m_settings.value(QStringLiteral("autohide"), false).toBool();
    m_showTitles = m_settings.value(QStringLiteral("showTitles"), true).toBool();
    m_cardSpacing = qBound(0, m_settings.value(QStringLiteral("cardSpacing"), 10).toInt(), 60);
    m_autoFitCards = m_settings.value(QStringLiteral("autoFitCards"), true).toBool();
    m_fitMinCardWidth =
        qBound(48, m_settings.value(QStringLiteral("fitMinCardWidth"), 96).toInt(), 800);
    m_captureMode = qBound(int(OnceOnFocus),
                           m_settings.value(QStringLiteral("captureMode"), OnceOnFocus).toInt(),
                           int(Periodic));
    // Floors keep a mistyped value from turning the strip into a capture storm.
    m_refreshInterval = qMax(500, m_settings.value(QStringLiteral("refreshInterval"), 4000).toInt());
    m_activeRefreshInterval =
        qMax(300, m_settings.value(QStringLiteral("activeRefreshInterval"), 1500).toInt());
    m_includeMinimized = m_settings.value(QStringLiteral("includeMinimized"), true).toBool();
    m_currentDesktopOnly = m_settings.value(QStringLiteral("currentDesktopOnly"), true).toBool();
    m_thisMonitorOnly = m_settings.value(QStringLiteral("thisMonitorOnly"), true).toBool();
}

void PreviewConfig::setEdge(int edge)
{
    edge = qBound(int(Bottom), edge, int(Right));
    if (m_edge == edge)
        return;
    m_edge = edge;
    m_settings.setValue(QStringLiteral("edge"), edge);
    emit edgeChanged();
}

void PreviewConfig::setAlignment(int alignment)
{
    alignment = qBound(int(Start), alignment, int(End));
    if (m_alignment == alignment)
        return;
    m_alignment = alignment;
    m_settings.setValue(QStringLiteral("alignment"), alignment);
    emit alignmentChanged();
}

void PreviewConfig::setOpacity(qreal opacity)
{
    opacity = qBound(0.0, opacity, 1.0);
    if (qFuzzyCompare(m_opacity, opacity))
        return;
    m_opacity = opacity;
    m_settings.setValue(QStringLiteral("opacity"), opacity);
    emit opacityChanged();
}

void PreviewConfig::setPanelColor(const QColor &color)
{
    if (m_panelColor == color)
        return;
    m_panelColor = color;
    m_settings.setValue(QStringLiteral("panelColor"), color.isValid() ? color.name() : QString());
    emit panelColorChanged();
}

void PreviewConfig::resetPanelColor()
{
    setPanelColor(QColor());
}

void PreviewConfig::setPanelPresetColors(const QStringList &colors)
{
    if (m_panelPresetColors == colors)
        return;
    m_panelPresetColors = colors;
    m_settings.setValue(QStringLiteral("panelPresetColors"), colors);
    emit panelPresetColorsChanged();
}

void PreviewConfig::setStripThickness(int px)
{
    px = qBound(120, px, 800);
    if (m_stripThickness == px)
        return;
    m_stripThickness = px;
    m_settings.setValue(QStringLiteral("stripThickness"), px);
    emit stripThicknessChanged();
}

void PreviewConfig::setStripLength(int percent)
{
    percent = qBound(0, percent, 100);
    if (m_stripLength == percent)
        return;
    m_stripLength = percent;
    m_settings.setValue(QStringLiteral("stripLength"), percent);
    emit stripLengthChanged();
}

void PreviewConfig::setScreenMargin(int margin)
{
    margin = qBound(0, margin, 200);
    if (m_screenMargin == margin)
        return;
    m_screenMargin = margin;
    m_settings.setValue(QStringLiteral("screenMargin"), margin);
    emit screenMarginChanged();
}

void PreviewConfig::setReserveSpace(bool reserve)
{
    if (m_reserveSpace == reserve)
        return;
    m_reserveSpace = reserve;
    m_settings.setValue(QStringLiteral("reserveSpace"), reserve);
    emit reserveSpaceChanged();
}

void PreviewConfig::setAutohide(bool autohide)
{
    if (m_autohide == autohide)
        return;
    m_autohide = autohide;
    m_settings.setValue(QStringLiteral("autohide"), autohide);
    emit autohideChanged();
}

void PreviewConfig::setShowTitles(bool show)
{
    if (m_showTitles == show)
        return;
    m_showTitles = show;
    m_settings.setValue(QStringLiteral("showTitles"), show);
    emit showTitlesChanged();
}

void PreviewConfig::setCardSpacing(int spacing)
{
    spacing = qBound(0, spacing, 60);
    if (m_cardSpacing == spacing)
        return;
    m_cardSpacing = spacing;
    m_settings.setValue(QStringLiteral("cardSpacing"), spacing);
    emit cardSpacingChanged();
}

void PreviewConfig::setAutoFitCards(bool fit)
{
    if (m_autoFitCards == fit)
        return;
    m_autoFitCards = fit;
    m_settings.setValue(QStringLiteral("autoFitCards"), fit);
    emit autoFitCardsChanged();
}

void PreviewConfig::setFitMinCardWidth(int px)
{
    px = qBound(48, px, 800);
    if (m_fitMinCardWidth == px)
        return;
    m_fitMinCardWidth = px;
    m_settings.setValue(QStringLiteral("fitMinCardWidth"), px);
    emit fitMinCardWidthChanged();
}

void PreviewConfig::setCaptureMode(int mode)
{
    mode = qBound(int(OnceOnFocus), mode, int(Periodic));
    if (m_captureMode == mode)
        return;
    m_captureMode = mode;
    m_settings.setValue(QStringLiteral("captureMode"), mode);
    emit captureModeChanged();
}

void PreviewConfig::setRefreshInterval(int ms)
{
    ms = qMax(500, ms);
    if (m_refreshInterval == ms)
        return;
    m_refreshInterval = ms;
    m_settings.setValue(QStringLiteral("refreshInterval"), ms);
    emit refreshIntervalChanged();
}

void PreviewConfig::setActiveRefreshInterval(int ms)
{
    ms = qMax(300, ms);
    if (m_activeRefreshInterval == ms)
        return;
    m_activeRefreshInterval = ms;
    m_settings.setValue(QStringLiteral("activeRefreshInterval"), ms);
    emit activeRefreshIntervalChanged();
}

void PreviewConfig::setIncludeMinimized(bool include)
{
    if (m_includeMinimized == include)
        return;
    m_includeMinimized = include;
    m_settings.setValue(QStringLiteral("includeMinimized"), include);
    emit filtersChanged();
}

void PreviewConfig::setCurrentDesktopOnly(bool only)
{
    if (m_currentDesktopOnly == only)
        return;
    m_currentDesktopOnly = only;
    m_settings.setValue(QStringLiteral("currentDesktopOnly"), only);
    emit filtersChanged();
}

void PreviewConfig::setThisMonitorOnly(bool only)
{
    if (m_thisMonitorOnly == only)
        return;
    m_thisMonitorOnly = only;
    m_settings.setValue(QStringLiteral("thisMonitorOnly"), only);
    emit filtersChanged();
}
