#include "tilemodel.h"

#include "appmenu.h"
#include "tileconfig.h"

#include <QVariantMap>

TileModel::TileModel(TileLayout *layout, AppMenu *menu, TileConfig *config, QObject *parent)
    : QAbstractListModel(parent)
    , m_layout(layout)
    , m_menu(menu)
    , m_config(config)
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
    const QVariantList apps = searching() ? m_menu->search(m_query)
                                          : m_menu->appsInCategory(m_section);
    for (const QVariant &v : apps) {
        const QVariantMap m = v.toMap();
        m_apps.insert(m.value(QStringLiteral("id")).toString(), m);
    }
}

QList<TileRecord> TileModel::searchPlacement() const
{
    // Results keep the order AppMenu returned them in and fill the matrix
    // row-major at 1x1 — a hit list, not an arrangement.
    QList<TileRecord> out;
    if (!m_menu)
        return out;
    const int cols = qMax(1, m_layout->columns());
    const QVariantList apps = m_menu->search(m_query);
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
