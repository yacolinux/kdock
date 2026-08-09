#include "cmconfig.h"

#include "cmsections.h"
#include "dockconfig.h"

#include <QDir>
#include <QStandardPaths>

QString CmConfig::settingsFilePath()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
                        + QStringLiteral("/kdock");
    QDir().mkpath(dir);
    return dir + QStringLiteral("/controlmanager.conf");
}

CmConfig::CmConfig(QObject *parent)
    : QObject(parent)
    , m_settings(settingsFilePath(), QSettings::IniFormat)
{
    load();
}

void CmConfig::load()
{
    const auto readInt = [this](const char *key, int def, int lo, int hi) {
        return qBound(lo, m_settings.value(QLatin1String(key), def).toInt(), hi);
    };
    const auto readBool = [this](const char *key, bool def) {
        return m_settings.value(QLatin1String(key), def).toBool();
    };

    m_edge = readInt("edge", Top, 0, 3);
    m_alignment = readInt("alignment", Center, 0, 2);
    m_panelWidth = readInt("panelWidth", 900, 240, 8000);
    m_panelHeight = readInt("panelHeight", 420, 160, 8000);
    m_panelWidthPercent = readInt("panelWidthPercent", 0, 0, 100);
    m_panelHeightPercent = readInt("panelHeightPercent", 0, 0, 100);
    m_screenMargin = readInt("screenMargin", 8, 0, 400);
    m_keepOpen = readBool("keepOpen", false);
    m_closeOnFocusLoss = readBool("closeOnFocusLoss", true);
    m_closeOnLeave = readBool("closeOnLeave", false);

    m_backgroundMode = readInt("backgroundMode", 0, 0, 1);
    m_backgroundColor = QColor(m_settings.value(QStringLiteral("backgroundColor")).toString());
    m_backgroundOpacity =
        qBound(0.10, m_settings.value(QStringLiteral("backgroundOpacity"), 0.94).toDouble(), 1.0);
    m_backgroundImage = m_settings.value(QStringLiteral("backgroundImage")).toString();
    m_cornerRadius = readInt("cornerRadius", 12, 0, 48);
    m_labelBold = readBool("labelBold", true);
    m_buttonWidth = readInt("buttonWidth", 0, 0, 600);
    m_buttonHeight = readInt("buttonHeight", 0, 0, 400);
    m_tabsPosition = readInt("tabsPosition", 0, 0, 1);
    m_showTabIcons = readBool("showTabIcons", true);
    m_showCardTitles = readBool("showCardTitles", true);

    m_columns = readInt("columns", 6, 0, 24);
    m_cellSize = readInt("cellSize", 96, 32, 400);
    m_cellStretch = readBool("cellStretch", true);
    m_cellMin = readInt("cellMin", 48, 32, 400);
    m_cellMax = readInt("cellMax", 220, 32, 600);
    m_cellSpacing = readInt("cellSpacing", 8, 0, 48);
    if (m_cellMax < m_cellMin)
        m_cellMax = m_cellMin;

    m_sectionOrder = m_settings.value(QStringLiteral("sectionOrder")).toStringList();
    // A first run enables every section and puts four of them on Principal: the
    // four that say something at a glance.
    m_enabledSections = m_settings.contains(QStringLiteral("enabledSections"))
                            ? m_settings.value(QStringLiteral("enabledSections")).toStringList()
                            : CmSections::tabIds();
    m_principalCards = m_settings.contains(QStringLiteral("principalCards"))
                           ? m_settings.value(QStringLiteral("principalCards")).toStringList()
                           : QStringList{QStringLiteral("clock"), QStringLiteral("audio"),
                                         QStringLiteral("video"), QStringLiteral("play")};
    m_rememberTab = readBool("rememberTab", true);
    m_lastTab = m_settings.value(QStringLiteral("lastTab")).toString();
    m_wallpaperScript = m_settings.value(QStringLiteral("wallpaperScript"),
                                         QStringLiteral("/usr/local/bin/next-wall.sh")).toString();

    reconcileSections();
}

void CmConfig::reconcileSections()
{
    const QStringList known = CmSections::ids();

    // Order: keep what is known, in the saved order, then append anything the
    // table has and the file does not (a section added by a newer version).
    QStringList order;
    for (const QString &id : std::as_const(m_sectionOrder)) {
        if (known.contains(id) && !order.contains(id))
            order.append(id);
    }
    for (const QString &id : known) {
        if (!order.contains(id))
            order.append(id);
    }
    m_sectionOrder = order;

    const auto prune = [&known](QStringList list) {
        QStringList out;
        for (const QString &id : std::as_const(list)) {
            if (known.contains(id) && !out.contains(id))
                out.append(id);
        }
        return out;
    };
    m_enabledSections = prune(m_enabledSections);
    m_principalCards = prune(m_principalCards);

    // A section with no tab can only ever be a card; keeping it in the enabled
    // list would draw an empty tab.
    QStringList enabled;
    for (const QString &id : std::as_const(m_enabledSections)) {
        if (CmSections::byId(id).hasTab)
            enabled.append(id);
    }
    m_enabledSections = enabled;
}

void CmConfig::store(const QString &key, const QVariant &value)
{
    m_settings.setValue(key, value);
    emit settingsChanged();
}

void CmConfig::storeWindow(const QString &key, const QVariant &value)
{
    m_settings.setValue(key, value);
    emit windowChanged();
}

void CmConfig::storeSections()
{
    m_settings.setValue(QStringLiteral("sectionOrder"), m_sectionOrder);
    m_settings.setValue(QStringLiteral("enabledSections"), m_enabledSections);
    m_settings.setValue(QStringLiteral("principalCards"), m_principalCards);
    m_settings.sync();
    emit sectionsChanged();
}

QUrl CmConfig::backgroundImageUrl() const
{
    return m_backgroundImage.isEmpty() ? QUrl() : QUrl::fromLocalFile(m_backgroundImage);
}

QStringList CmConfig::presetColors() const
{
    QSettings shared(DockConfig::settingsFilePath(), QSettings::IniFormat);
    return DockConfig::normalizedPresetColors(
        shared.value(QStringLiteral("panelPresetColors")).toStringList());
}

int CmConfig::panelWidthFor(int screenWidth) const
{
    if (screenWidth <= 0)
        return m_panelWidth;
    const int wanted = m_panelWidthPercent > 0
                           ? qRound(screenWidth * m_panelWidthPercent / 100.0)
                           : m_panelWidth;
    // Never wider than the screen minus the margins, or the panel hangs off the
    // side of the output it is anchored to.
    return qBound(240, wanted, qMax(240, screenWidth - 2 * m_screenMargin));
}

int CmConfig::panelHeightFor(int screenHeight) const
{
    if (screenHeight <= 0)
        return m_panelHeight;
    const int wanted = m_panelHeightPercent > 0
                           ? qRound(screenHeight * m_panelHeightPercent / 100.0)
                           : m_panelHeight;
    return qBound(160, wanted, qMax(160, screenHeight - 2 * m_screenMargin));
}

bool CmConfig::sectionEnabled(const QString &id) const
{
    return m_enabledSections.contains(id);
}

bool CmConfig::cardEnabled(const QString &id) const
{
    return m_principalCards.contains(id);
}

void CmConfig::setSectionEnabled(const QString &id, bool on)
{
    if (!CmSections::byId(id).hasTab || sectionEnabled(id) == on)
        return;
    if (on)
        m_enabledSections.append(id);
    else
        m_enabledSections.removeAll(id);
    storeSections();
}

void CmConfig::setCardEnabled(const QString &id, bool on)
{
    if (!CmSections::exists(id) || cardEnabled(id) == on)
        return;
    if (on)
        m_principalCards.append(id);
    else
        m_principalCards.removeAll(id);
    storeSections();
}

void CmConfig::moveSection(int from, int to)
{
    if (from == to || from < 0 || to < 0 || from >= m_sectionOrder.size()
        || to >= m_sectionOrder.size())
        return;
    m_sectionOrder.move(from, to);
    storeSections();
}

QStringList CmConfig::visibleTabs() const
{
    QStringList out;
    for (const QString &id : m_sectionOrder) {
        if (m_enabledSections.contains(id) && CmSections::byId(id).hasTab)
            out.append(id);
    }
    return out;
}

void CmConfig::setEdge(int edge)
{
    edge = qBound(0, edge, 3);
    if (m_edge == edge)
        return;
    m_edge = edge;
    storeWindow(QStringLiteral("edge"), edge);
}

void CmConfig::setAlignment(int alignment)
{
    alignment = qBound(0, alignment, 2);
    if (m_alignment == alignment)
        return;
    m_alignment = alignment;
    storeWindow(QStringLiteral("alignment"), alignment);
}

void CmConfig::setPanelWidth(int px)
{
    px = qBound(240, px, 8000);
    if (m_panelWidth == px)
        return;
    m_panelWidth = px;
    storeWindow(QStringLiteral("panelWidth"), px);
}

void CmConfig::setPanelHeight(int px)
{
    px = qBound(160, px, 8000);
    if (m_panelHeight == px)
        return;
    m_panelHeight = px;
    storeWindow(QStringLiteral("panelHeight"), px);
}

void CmConfig::setPanelWidthPercent(int percent)
{
    percent = qBound(0, percent, 100);
    if (m_panelWidthPercent == percent)
        return;
    m_panelWidthPercent = percent;
    storeWindow(QStringLiteral("panelWidthPercent"), percent);
}

void CmConfig::setPanelHeightPercent(int percent)
{
    percent = qBound(0, percent, 100);
    if (m_panelHeightPercent == percent)
        return;
    m_panelHeightPercent = percent;
    storeWindow(QStringLiteral("panelHeightPercent"), percent);
}

void CmConfig::setScreenMargin(int px)
{
    px = qBound(0, px, 400);
    if (m_screenMargin == px)
        return;
    m_screenMargin = px;
    storeWindow(QStringLiteral("screenMargin"), px);
}

void CmConfig::setKeepOpen(bool on)
{
    if (m_keepOpen == on)
        return;
    m_keepOpen = on;
    store(QStringLiteral("keepOpen"), on);
}

void CmConfig::setCloseOnFocusLoss(bool on)
{
    if (m_closeOnFocusLoss == on)
        return;
    m_closeOnFocusLoss = on;
    store(QStringLiteral("closeOnFocusLoss"), on);
}

void CmConfig::setCloseOnLeave(bool on)
{
    if (m_closeOnLeave == on)
        return;
    m_closeOnLeave = on;
    store(QStringLiteral("closeOnLeave"), on);
}

void CmConfig::setBackgroundMode(int mode)
{
    mode = qBound(0, mode, 1);
    if (m_backgroundMode == mode)
        return;
    m_backgroundMode = mode;
    store(QStringLiteral("backgroundMode"), mode);
}

void CmConfig::setBackgroundColor(const QColor &color)
{
    if (m_backgroundColor == color)
        return;
    m_backgroundColor = color;
    store(QStringLiteral("backgroundColor"), color.isValid() ? color.name() : QString());
}

void CmConfig::setBackgroundOpacity(qreal opacity)
{
    opacity = qBound(0.10, opacity, 1.0);
    if (qFuzzyCompare(m_backgroundOpacity, opacity))
        return;
    m_backgroundOpacity = opacity;
    store(QStringLiteral("backgroundOpacity"), opacity);
}

void CmConfig::setBackgroundImage(const QString &path)
{
    if (m_backgroundImage == path)
        return;
    m_backgroundImage = path;
    store(QStringLiteral("backgroundImage"), path);
}

void CmConfig::setCornerRadius(int px)
{
    px = qBound(0, px, 48);
    if (m_cornerRadius == px)
        return;
    m_cornerRadius = px;
    store(QStringLiteral("cornerRadius"), px);
}

void CmConfig::setLabelBold(bool on)
{
    if (m_labelBold == on)
        return;
    m_labelBold = on;
    store(QStringLiteral("labelBold"), on);
}

void CmConfig::setButtonWidth(int px)
{
    px = qBound(0, px, 600);
    if (m_buttonWidth == px)
        return;
    m_buttonWidth = px;
    store(QStringLiteral("buttonWidth"), px);
}

void CmConfig::setButtonHeight(int px)
{
    px = qBound(0, px, 400);
    if (m_buttonHeight == px)
        return;
    m_buttonHeight = px;
    store(QStringLiteral("buttonHeight"), px);
}

void CmConfig::setTabsPosition(int position)
{
    position = qBound(0, position, 1);
    if (m_tabsPosition == position)
        return;
    m_tabsPosition = position;
    store(QStringLiteral("tabsPosition"), position);
}

void CmConfig::setShowTabIcons(bool on)
{
    if (m_showTabIcons == on)
        return;
    m_showTabIcons = on;
    store(QStringLiteral("showTabIcons"), on);
}

void CmConfig::setShowCardTitles(bool on)
{
    if (m_showCardTitles == on)
        return;
    m_showCardTitles = on;
    store(QStringLiteral("showCardTitles"), on);
}

void CmConfig::setColumns(int columns)
{
    columns = qBound(0, columns, 24);
    if (m_columns == columns)
        return;
    m_columns = columns;
    store(QStringLiteral("columns"), columns);
}

void CmConfig::setCellSize(int px)
{
    px = qBound(32, px, 400);
    if (m_cellSize == px)
        return;
    m_cellSize = px;
    store(QStringLiteral("cellSize"), px);
}

void CmConfig::setCellStretch(bool on)
{
    if (m_cellStretch == on)
        return;
    m_cellStretch = on;
    store(QStringLiteral("cellStretch"), on);
}

void CmConfig::setCellMin(int px)
{
    px = qBound(32, px, 400);
    if (m_cellMin == px)
        return;
    m_cellMin = px;
    store(QStringLiteral("cellMin"), px);
}

void CmConfig::setCellMax(int px)
{
    px = qBound(32, px, 600);
    if (m_cellMax == px)
        return;
    m_cellMax = px;
    store(QStringLiteral("cellMax"), px);
}

void CmConfig::setCellSpacing(int px)
{
    px = qBound(0, px, 48);
    if (m_cellSpacing == px)
        return;
    m_cellSpacing = px;
    store(QStringLiteral("cellSpacing"), px);
}

void CmConfig::setRememberTab(bool on)
{
    if (m_rememberTab == on)
        return;
    m_rememberTab = on;
    store(QStringLiteral("rememberTab"), on);
}

void CmConfig::setLastTab(const QString &tab)
{
    if (m_lastTab == tab)
        return;
    m_lastTab = tab;
    // Deliberately no signal: the current tab is what *caused* this write, so
    // repainting from it would loop through the binding that set it (same
    // reasoning as TileConfig::setLastSection).
    m_settings.setValue(QStringLiteral("lastTab"), tab);
}

void CmConfig::setWallpaperScript(const QString &path)
{
    if (m_wallpaperScript == path)
        return;
    m_wallpaperScript = path;
    store(QStringLiteral("wallpaperScript"), path);
}

bool CmConfig::preload()
{
    QSettings s(settingsFilePath(), QSettings::IniFormat);
    return s.value(QStringLiteral("preload"), false).toBool();
}

void CmConfig::setPreload(bool on)
{
    QSettings s(settingsFilePath(), QSettings::IniFormat);
    s.setValue(QStringLiteral("preload"), on);
    s.sync();
}

QString CmConfig::layoutJson() const
{
    return m_settings.value(QStringLiteral("layout")).toString();
}

void CmConfig::setLayoutJson(const QString &json)
{
    // No signal here either: CmLayout — which just wrote this — emits its own,
    // finer-grained one.
    m_settings.setValue(QStringLiteral("layout"), json);
    m_settings.sync();
}
