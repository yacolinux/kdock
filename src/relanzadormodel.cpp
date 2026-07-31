#include "relanzadormodel.h"
#include "relanzadorconfig.h"

RelanzadorModel::RelanzadorModel(RelanzadorConfig *config, DesktopEntryIndex *apps, QObject *parent)
    : QAbstractListModel(parent)
    , m_config(config)
    , m_apps(apps)
{
    connect(m_config, &RelanzadorConfig::pinnedChanged, this, &RelanzadorModel::rebuild);
    rebuild();
}

int RelanzadorModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_items.size();
}

QVariant RelanzadorModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_items.size())
        return {};
    const Item &item = m_items.at(index.row());
    switch (role) {
    case NameRole:
        return item.entry.isValid() ? item.entry.name : item.desktopId;
    case IconNameRole:
        return item.entry.isValid() ? item.entry.icon : item.desktopId.toLower();
    case DesktopIdRole:
        return item.desktopId;
    case IsSubRelanzadorRole:
        return item.isSubRelanzador;
    }
    return {};
}

QHash<int, QByteArray> RelanzadorModel::roleNames() const
{
    return {
        {NameRole, "name"},
        {IconNameRole, "iconName"},
        {DesktopIdRole, "desktopId"},
        {IsSubRelanzadorRole, "isSubRelanzador"},
    };
}

void RelanzadorModel::launch(int row)
{
    if (row < 0 || row >= m_items.size())
        return;
    const Item &item = m_items.at(row);
    if (item.isSubRelanzador)
        return; // sub-relanzadores are opened via popup, not launched
    if (item.entry.isValid())
        DesktopEntryIndex::launch(item.entry);
}

void RelanzadorModel::moveItem(int from, int to)
{
    if (from == to || from < 0 || to < 0 || from >= m_items.size() || to >= m_items.size())
        return;

    if (!beginMoveRows({}, from, from, {}, to + (to > from ? 1 : 0)))
        return;
    m_items.move(from, to);
    endMoveRows();
    saveOrder();
}

void RelanzadorModel::addItem(const QString &desktopId)
{
    const int row = m_items.size();
    beginInsertRows({}, row, row);
    Item item;
    item.desktopId = desktopId;
    item.entry = m_apps->forAppId(desktopId);
    item.isSubRelanzador = false;
    m_items.append(item);
    endInsertRows();
    saveOrder();
}

void RelanzadorModel::removeItem(int row)
{
    if (row < 0 || row >= m_items.size())
        return;
    beginRemoveRows({}, row, row);
    m_items.removeAt(row);
    endRemoveRows();
    saveOrder();
}

void RelanzadorModel::rebuild()
{
    beginResetModel();
    m_items.clear();

    const QStringList pinned = m_config->pinned();
    for (const QString &id : pinned) {
        Item item;
        item.desktopId = id;
        item.entry = m_apps->forAppId(id);
        item.isSubRelanzador = false;
        m_items.append(item);
    }

    // Add sub-relanzador item if configured
    if (m_config->hasSubRelanzador() && !m_config->subRelanzadorId().isEmpty()
        && m_config->nestingLevel() < 2) {
        Item item;
        item.desktopId = m_config->subRelanzadorId();
        item.entry = DesktopEntry(); // invalid, will use fallback
        item.isSubRelanzador = true;
        m_items.append(item);
    }

    endResetModel();
}

void RelanzadorModel::saveOrder()
{
    QStringList pinned;
    for (const Item &item : std::as_const(m_items)) {
        if (!item.isSubRelanzador)
            pinned.append(item.desktopId);
    }
    m_config->setPinned(pinned);
}