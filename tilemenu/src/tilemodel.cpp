#include "tilemodel.h"

#include "appmenu.h"
#include "tileconfig.h"
#include "tileusage.h"

#include <QVariantMap>

#include <algorithm>

TileModel::TileModel(TileLayout *layout, AppMenu *menu, TileConfig *config, TileUsage *usage,
                     QObject *parent)
    : QAbstractListModel(parent)
    , m_layout(layout)
    , m_menu(menu)
    , m_config(config)
    , m_usage(usage)
{
    connect(m_layout, &TileLayout::changed, this, [this](const QString &section) {
        if (section.isEmpty() || section == m_section)
            refresh();
    });
    // The column count and the tile size come from the config, and both change
    // where every tile lands.
    connect(m_config, &TileConfig::settingsChanged, this, &TileModel::refresh);
    if (m_menu) {
        connect(m_menu, &AppMenu::changed, this, &TileModel::refresh);
        connect(m_menu, &AppMenu::favoritesChanged, this, &TileModel::refresh);
    }
}

int TileModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_tiles.size();
}

QHash<int, QByteArray> TileModel::roleNames() const
{
    return {
        {IdRole, "tileId"},          {NameRole, "name"},
        {CommentRole, "comment"},    {IconRole, "icon"},
        {FavoriteRole, "favorite"},  {GroupRole, "group"},
        {ColRole, "col"},            {RowRole, "row"},
        {WidthRole, "span"},
        {HeightRole, "vspan"},       {BackgroundRole, "background"},
        {ImageRole, "image"},        {ShowIconRole, "showIcon"},
        {ShowLabelRole, "showLabel"},
    };
}

QVariant TileModel::data(const QModelIndex &index, int role) const
{
    if (index.row() < 0 || index.row() >= m_tiles.size())
        return {};
    const TileRecord &t = m_tiles.at(index.row());
    const QVariantMap app = m_apps.value(t.id);

    switch (role) {
    case IdRole:
        return t.id;
    case NameRole:
        // A per-tile rename wins over the .desktop name; the id is the last
        // resort for an entry the index no longer knows.
        return !t.label.isEmpty() ? t.label
                                  : app.value(QStringLiteral("name"), t.id).toString();
    case CommentRole:
        return app.value(QStringLiteral("comment")).toString();
    case IconRole:
        return !t.icon.isEmpty()
                   ? t.icon
                   : app.value(QStringLiteral("icon"),
                               QStringLiteral("application-x-executable")).toString();
    case FavoriteRole:
        return app.value(QStringLiteral("favorite"), false).toBool();
    case GroupRole:
        return t.group;
    case ColRole:
        return t.col;
    case RowRole:
        return t.row;
    case WidthRole:
        return t.w;
    case HeightRole:
        return t.h;
    case BackgroundRole:
        return t.bg;
    case ImageRole:
        return t.image;
    case ShowIconRole:
        // -1 = follow the global switch.
        return t.showIcon < 0 ? m_config->showIcons() : t.showIcon == 1;
    case ShowLabelRole:
        return t.showLabel < 0 ? m_config->showLabels() : t.showLabel == 1;
    }
    return {};
}

void TileModel::setCurrentGroup(int group)
{
    if (m_currentGroup == group)
        return;
    m_currentGroup = group;
    emit currentGroupChanged();
    refresh();
}

void TileModel::setSection(const QString &section)
{
    if (m_section == section)
        return;
    m_section = section;
    // Each section has its own tabs; there is no reason the third tab of one
    // would mean anything in the next.
    if (m_currentGroup != 0) {
        m_currentGroup = 0;
        emit currentGroupChanged();
    }
    if (m_config->rememberSection())
        m_config->setLastSection(section);
    emit sectionChanged();
    refresh();
}

void TileModel::setQuery(const QString &query)
{
    if (m_query == query)
        return;
    const bool wasSearching = searching();
    m_query = query;
    emit queryChanged();
    Q_UNUSED(wasSearching);
    refresh();
}

void TileModel::reloadApps()
{
    m_apps.clear();
    if (!m_menu)
        return;
    const QVariantList apps = searching() ? sortedSearchApps()
                                          : m_menu->appsInCategory(m_section);
    for (const QVariant &v : apps) {
        const QVariantMap m = v.toMap();
        m_apps.insert(m.value(QStringLiteral("id")).toString(), m);
    }
}

QVariantList TileModel::sortedSearchApps() const
{
    const int mode = m_config ? m_config->searchSort() : 2;
    if (m_searchCacheValid && m_searchCacheQuery == m_query && m_searchCacheSort == mode)
        return m_searchCache;
    m_searchCache = computeSortedSearchApps();
    m_searchCacheQuery = m_query;
    m_searchCacheSort = mode;
    m_searchCacheValid = true;
    return m_searchCache;
}

QVariantList TileModel::computeSortedSearchApps() const
{
    if (!m_menu)
        return {};
    QVariantList apps = m_menu->search(m_query);

    const auto nameOf = [](const QVariant &v) {
        return v.toMap().value(QStringLiteral("name")).toString();
    };
    const auto idOf = [](const QVariant &v) {
        return v.toMap().value(QStringLiteral("id")).toString();
    };
    const auto byName = [&nameOf](const QVariant &a, const QVariant &b) {
        return QString::localeAwareCompare(nameOf(a), nameOf(b)) < 0;
    };

    const int mode = m_config ? m_config->searchSort() : 2;
    if (mode == 1 && m_usage) { // frequency of use
        std::stable_sort(apps.begin(), apps.end(), [&](const QVariant &a, const QVariant &b) {
            const int ca = m_usage->count(idOf(a));
            const int cb = m_usage->count(idOf(b));
            return ca != cb ? ca > cb : byName(a, b);
        });
    } else if (mode == 2 && m_usage) { // recent use
        std::stable_sort(apps.begin(), apps.end(), [&](const QVariant &a, const QVariant &b) {
            const qint64 ta = m_usage->lastMs(idOf(a));
            const qint64 tb = m_usage->lastMs(idOf(b));
            return ta != tb ? ta > tb : byName(a, b);
        });
    } else { // alphabetical (mode 0, or no usage backend)
        std::stable_sort(apps.begin(), apps.end(), byName);
    }
    return apps;
}

QVariantList TileModel::searchResults() const
{
    if (!searching())
        return {};
    QVariantList out;
    for (const QVariant &v : sortedSearchApps()) {
        const QVariantMap m = v.toMap();
        out.append(QVariantMap{
            {QStringLiteral("tileId"), m.value(QStringLiteral("id"))},
            {QStringLiteral("name"), m.value(QStringLiteral("name"))},
            {QStringLiteral("icon"), m.value(QStringLiteral("icon"))},
            {QStringLiteral("comment"), m.value(QStringLiteral("comment"))},
        });
    }
    return out;
}

QVariantList TileModel::recentApps() const
{
    QVariantList out;
    if (!m_usage || !m_menu)
        return out;
    for (const QString &id : m_usage->recentIds()) {
        const QVariantMap m = m_menu->appById(id);
        if (m.isEmpty())
            continue; // uninstalled since it was last launched
        out.append(QVariantMap{
            {QStringLiteral("tileId"), m.value(QStringLiteral("id"))},
            {QStringLiteral("name"), m.value(QStringLiteral("name"))},
            {QStringLiteral("icon"), m.value(QStringLiteral("icon"))},
            {QStringLiteral("comment"), m.value(QStringLiteral("comment"))},
        });
    }
    return out;
}

QList<TileRecord> TileModel::searchPlacement() const
{
    // Results fill the matrix row-major at 1x1 — a hit list, not an arrangement.
    // The order is TileConfig::searchSort, shared with the list view.
    QList<TileRecord> out;
    if (!m_menu)
        return out;
    const int cols = qMax(1, m_layout->columns());
    const QVariantList apps = sortedSearchApps();
    int i = 0;
    for (const QVariant &v : apps) {
        TileRecord r;
        r.id = v.toMap().value(QStringLiteral("id")).toString();
        if (r.id.isEmpty())
            continue;
        r.col = i % cols;
        r.row = i / cols;
        out.append(r);
        ++i;
    }
    return out;
}

void TileModel::refresh()
{
    // Every input that changes the search order (query, config sort, menu and
    // favourites) reaches the model through refresh(); drop the memo here so the
    // recompute below and any later QML searchResults() see the new state.
    invalidateSearchCache();
    reloadApps();

    // Clamp first: a tab can disappear under us (removed from the panel, or the
    // section changed), and asking for tiles of a group that no longer exists
    // would just draw nothing with no way back.
    const int tabCount = qMax(1, m_layout->groups(m_section).size());
    if (m_currentGroup >= tabCount || m_currentGroup < 0) {
        m_currentGroup = qBound(0, m_currentGroup, tabCount - 1);
        emit currentGroupChanged();
    }

    QList<TileRecord> next;
    if (searching()) {
        // A search crosses every tab: it is a hit list, not an arrangement.
        next = searchPlacement();
    } else {
        for (const TileRecord &t : m_layout->placement(m_section)) {
            if (t.group == m_currentGroup)
                next.append(t);
        }
    }

    // Same tiles in the same order (the common case: a move, a resize, a color
    // change) — update in place so the QML delegates survive and animate.
    bool sameIds = next.size() == m_tiles.size();
    if (sameIds) {
        for (int i = 0; i < next.size(); ++i) {
            if (next.at(i).id != m_tiles.at(i).id) {
                sameIds = false;
                break;
            }
        }
    }

    if (sameIds) {
        m_tiles = next;
        if (!m_tiles.isEmpty())
            emit dataChanged(index(0), index(m_tiles.size() - 1));
    } else {
        beginResetModel();
        m_tiles = next;
        endResetModel();
    }

    if (searching()) {
        int rows = 0;
        for (const TileRecord &t : m_tiles)
            rows = qMax(rows, t.row + t.h);
        m_rows = rows;
        m_groups = {};
        m_customized = false;
    } else {
        m_rows = m_layout->rowsOfGroup(m_section, m_currentGroup);
        m_groups = m_layout->groups(m_section);
        m_customized = m_layout->isCustomized(m_section);
    }
    emit layoutChanged();
}

QVariantMap TileModel::get(int row) const
{
    QVariantMap out;
    if (row < 0 || row >= m_tiles.size())
        return out;
    const QHash<int, QByteArray> names = roleNames();
    for (auto it = names.cbegin(); it != names.cend(); ++it)
        out.insert(QString::fromUtf8(it.value()), data(index(row), it.key()));
    return out;
}

int TileModel::indexOfLetter(const QString &letter) const
{
    if (letter.isEmpty())
        return -1;
    const QChar target = letter.at(0).toUpper();
    for (int i = 0; i < m_tiles.size(); ++i) {
        const QString name = data(index(i), NameRole).toString();
        if (!name.isEmpty() && name.at(0).toUpper() == target)
            return i;
    }
    return -1;
}

QStringList TileModel::availableLetters() const
{
    QStringList out;
    for (int i = 0; i < m_tiles.size(); ++i) {
        const QString name = data(index(i), NameRole).toString();
        if (name.isEmpty())
            continue;
        const QString initial = name.at(0).toUpper();
        if (!out.contains(initial))
            out.append(initial);
    }
    return out;
}
