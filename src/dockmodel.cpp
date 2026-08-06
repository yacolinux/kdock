#include "dockmodel.h"

#include "virtualdesktops.h"

#include "dockconfig.h"
#include "windowmonitor.h"

#include <QVariantMap>

QString DockModel::Item::displayName() const
{
    if (entry.isValid() && !entry.name.isEmpty())
        return entry.name;
    if (!windows.isEmpty() && !windows.first()->title.isEmpty())
        return windows.first()->title;
    return fallbackAppId;
}

QString DockModel::Item::iconName() const
{
    if (entry.isValid() && !entry.icon.isEmpty())
        return entry.icon;
    return fallbackAppId.toLower();
}

DockModel::DockModel(DockConfig *config, DesktopEntryIndex *apps, WindowMonitor *monitor,
                     VirtualDesktops *desktops, QObject *parent)
    : QAbstractListModel(parent)
    , m_config(config)
    , m_apps(apps)
    , m_monitor(monitor)
    , m_desktops(desktops)
{
    connect(m_config, &DockConfig::pinnedChanged, this, [this] {
        if (!m_updatingPinned)
            rebuild();
    });
    connect(m_config, &DockConfig::separator1Changed, this, &DockModel::rebuild);
    connect(m_config, &DockConfig::separator2Changed, this, &DockModel::rebuild);
    connect(m_config, &DockConfig::groupWindowsChanged, this, [this] { rebuild(); });
    if (m_monitor) {
        connect(m_monitor, &WindowMonitor::windowAdded, this, [this](AbstractWindow *w) {
            connect(w, &AbstractWindow::changed, this, [this, w] { windowChanged(w); });
            if (!w->skipTaskbar)
                placeWindow(w);
        });
        connect(m_monitor, &WindowMonitor::windowRemoved, this, &DockModel::removeWindow);
    }
    rebuild();
}

int DockModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_items.size();
}

QVariant DockModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_items.size())
        return {};
    const Item &item = m_items.at(index.row());
    switch (role) {
    case NameRole:
        return item.displayName();
    case IconNameRole:
        return item.iconName();
    case PinnedRole:
        return item.pinned;
    case WindowCountRole:
        return item.windows.size();
    case ActiveRole:
        for (AbstractWindow *w : item.windows)
            if (w->activated)
                return true;
        return false;
    case MinimizedRole:
        if (item.windows.isEmpty())
            return false;
        for (AbstractWindow *w : item.windows)
            if (!w->minimized)
                return false;
        return true;
    case IsSeparatorRole:
        return item.isSeparator;
    case TitleRole:
        return item.windows.size() == 1 ? item.windows.first()->title : QString();
    }
    return {};
}

QHash<int, QByteArray> DockModel::roleNames() const
{
    return {
        {NameRole, "name"},
        {IconNameRole, "iconName"},
        {PinnedRole, "pinned"},
        {WindowCountRole, "windowCount"},
        {ActiveRole, "active"},
        {MinimizedRole, "minimized"},
        {IsSeparatorRole, "isSeparator"},
        {TitleRole, "title"},
    };
}

QString DockModel::keyForAppId(const QString &appId, DesktopEntry *outEntry) const
{
    const DesktopEntry entry = m_apps->forAppId(appId);
    if (outEntry)
        *outEntry = entry;
    return entry.isValid() ? entry.id.toLower() : appId.toLower();
}

int DockModel::rowOfKey(const QString &key) const
{
    for (int i = 0; i < m_items.size(); ++i)
        if (m_items.at(i).key == key)
            return i;
    return -1;
}

int DockModel::rowOfWindow(AbstractWindow *window) const
{
    for (int i = 0; i < m_items.size(); ++i)
        if (m_items.at(i).windows.contains(window))
            return i;
    return -1;
}

QString DockModel::keyForWindow(AbstractWindow *w) const
{
    const QString appKey = keyForAppId(w->appId, nullptr);
    if (m_config->groupWindows())
        return appKey;
    // Ungrouped: the first window reuses the app/launcher row, so a pinned
    // launcher collapses into the first window's icon instead of duplicating.
    // Every later window becomes its own separate icon.
    const int row = rowOfKey(appKey);
    if (row >= 0 && m_items.at(row).windows.isEmpty())
        return appKey;
    return QStringLiteral("win:") + QString::number(reinterpret_cast<quintptr>(w));
}

void DockModel::rebuild()
{
    beginResetModel();
    m_items.clear();

    for (const QString &id : m_config->pinned()) {
        Item item;
        item.key = id.toLower();
        item.entry = m_apps->byId(id);
        item.fallbackAppId = id;
        item.pinned = true;
        m_items.append(item);
    }

    if (m_monitor) {
        for (AbstractWindow *w : std::as_const(m_monitor->windows)) {
            if (w->skipTaskbar)
                continue;
            DesktopEntry entry;
            const QString key = keyForWindow(w);
            int row = rowOfKey(key);
            if (row < 0) {
                Item item;
                item.key = key;
                item.entry = m_apps->forAppId(w->appId);
                item.fallbackAppId = w->appId;
                m_items.append(item);
                row = m_items.size() - 1;
            }
            m_items[row].windows.append(w);
        }
    }

    // Insert separators (descending order so indices don't shift)
    QList<int> sepPos = {m_config->separator1(), m_config->separator2()};
    std::sort(sepPos.begin(), sepPos.end(), std::greater<int>());
    for (int pos : sepPos) {
        if (pos < 0 || pos > m_items.size())
            continue;
        Item sep;
        sep.isSeparator = true;
        sep.separatorIndex = (pos == m_config->separator1() ? 1 : 2);
        m_items.insert(pos, sep);
    }

    endResetModel();
}

void DockModel::placeWindow(AbstractWindow *window)
{
    DesktopEntry entry;
    const QString key = keyForWindow(window);
    const int row = rowOfKey(key);
    if (row >= 0) {
        m_items[row].windows.append(window);
        emit dataChanged(index(row), index(row));
    } else {
        Item item;
        item.key = key;
        item.entry = m_apps->forAppId(window->appId);
        item.fallbackAppId = window->appId;
        item.windows.append(window);
        beginInsertRows({}, m_items.size(), m_items.size());
        m_items.append(item);
        endInsertRows();
    }
}

void DockModel::removeWindow(AbstractWindow *window)
{
    const int row = rowOfWindow(window);
    if (row < 0)
        return;
    m_items[row].windows.removeOne(window);
    if (m_items[row].windows.isEmpty() && !m_items[row].pinned) {
        beginRemoveRows({}, row, row);
        m_items.removeAt(row);
        endRemoveRows();
    } else {
        emit dataChanged(index(row), index(row));
    }
}

void DockModel::syncWindows()
{
    if (!m_monitor)
        return;
    for (AbstractWindow *w : std::as_const(m_monitor->windows)) {
        if (w->skipTaskbar)
            continue;
        if (rowOfWindow(w) >= 0)
            continue;
        placeWindow(w);
    }
}

QStringList DockModel::labelStrings() const
{
    QStringList names;
    names.reserve(m_items.size());
    for (const Item &item : m_items) {
        if (item.isSeparator)
            continue;
        names << item.displayName();
    }
    return names;
}

void DockModel::windowChanged(AbstractWindow *window)
{
    const int row = rowOfWindow(window);
    if (row >= 0) {
        if (window->skipTaskbar) {
            removeWindow(window);
            return;
        }
        // app_id can change (e.g. from a generic id to the real one, or a PWA
        // id assigned after the window maps); re-evaluate grouping so the window
        // lands on its launcher/app row instead of a stray win: icon.
        const QString appKey = keyForAppId(window->appId, nullptr);
        const QString &curKey = m_items.at(row).key;
        bool regroup = false;
        if (m_config->groupWindows()) {
            regroup = (curKey != appKey);
        } else if (curKey != appKey) {
            // Ungrouped: a window belongs on the app/launcher row only when it is
            // the first window there. Move it over if it's stranded elsewhere and
            // that row is still free (empty launcher or newly matched app).
            const int appRow = rowOfKey(appKey);
            regroup = (appRow >= 0 && m_items.at(appRow).windows.isEmpty());
        }
        if (regroup) {
            removeWindow(window);
            placeWindow(window);
            return;
        }
        emit dataChanged(index(row), index(row));
    } else if (!window->skipTaskbar) {
        placeWindow(window);
    }
}

void DockModel::activate(int row)
{
    if (row < 0 || row >= m_items.size() || m_items.at(row).isSeparator)
        return;
    Item &item = m_items[row];

    if (item.windows.isEmpty()) {
        launch(row);
        return;
    }
    if (item.windows.size() == 1) {
        AbstractWindow *w = item.windows.first();
        if (w->activated)
            w->minimize();
        else
            w->activate();
        return;
    }
    // Several windows: cycle through them
    int current = -1;
    for (int i = 0; i < item.windows.size(); ++i)
        if (item.windows.at(i)->activated)
            current = i;
    item.windows.at((current + 1) % item.windows.size())->activate();
}

void DockModel::launch(int row)
{
    if (row < 0 || row >= m_items.size() || m_items.at(row).isSeparator)
        return;
    const Item &item = m_items.at(row);
    if (item.entry.isValid())
        DesktopEntryIndex::launch(item.entry);
}

void DockModel::closeAll(int row)
{
    if (row < 0 || row >= m_items.size() || m_items.at(row).isSeparator)
        return;
    const auto windows = m_items.at(row).windows;
    for (AbstractWindow *w : windows)
        w->requestClose();
}

void DockModel::sendToDesktop(int row, int position)
{
    if (row < 0 || row >= m_items.size() || m_items.at(row).isSeparator || !m_desktops)
        return;
    const QString id = m_desktops->idOf(position);
    if (id.isEmpty())
        return;
    // Every window under the icon moves, like "close all windows" closes them
    // all: the menu hangs off the app, not off one of its windows. The view
    // stays where it is — this never switches desktops.
    const auto windows = m_items.at(row).windows;
    for (AbstractWindow *w : windows) {
        if (w->canChangeDesktop())
            w->moveToOnlyDesktop(id);
    }
}

void DockModel::togglePinned(int row)
{
    if (row < 0 || row >= m_items.size() || m_items.at(row).isSeparator)
        return;
    Item &item = m_items[row];
    QStringList pinned = m_config->pinned();

    if (item.pinned) {
        pinned.removeAll(item.entry.isValid() ? item.entry.id : item.fallbackAppId);
        item.pinned = false;
        if (item.windows.isEmpty()) {
            beginRemoveRows({}, row, row);
            m_items.removeAt(row);
            endRemoveRows();
        } else {
            emit dataChanged(index(row), index(row));
        }
    } else {
        if (!item.entry.isValid())
            return; // nothing to launch later; not pinnable
        if (!pinned.contains(item.entry.id))
            pinned.append(item.entry.id);
        item.pinned = true;
        // Re-key the row to the application. Ungrouped mode keys a window row
        // by the window itself ("win:<pointer>"), a key no launcher can ever
        // match: leaving it would turn this row into an orphan launcher the
        // moment its window closes (the row survives because it is pinned).
        // Clicking it would then find no windows and start a new instance every
        // time, and each new window would add yet another icon, because
        // keyForWindow() cannot find the launcher either. Skipped if some other
        // row already owns the app key, to avoid two rows with one key.
        const QString appKey = item.entry.id.toLower();
        if (rowOfKey(appKey) < 0)
            item.key = appKey;
        emit dataChanged(index(row), index(row));
    }

    m_updatingPinned = true;
    m_config->setPinned(pinned);
    m_updatingPinned = false;
}

QVariantList DockModel::windowList(int row) const
{
    QVariantList list;
    if (row < 0 || row >= m_items.size())
        return list;
    const Item &item = m_items.at(row);
    for (int i = 0; i < item.windows.size(); ++i) {
        AbstractWindow *w = item.windows.at(i);
        QVariantMap entry;
        entry[QStringLiteral("windowIndex")] = i;
        entry[QStringLiteral("title")] = w->title.isEmpty() ? item.displayName() : w->title;
        entry[QStringLiteral("activated")] = w->activated;
        entry[QStringLiteral("minimized")] = w->minimized;
        list.append(entry);
    }
    return list;
}

void DockModel::moveItem(int from, int to)
{
    if (from == to || from < 0 || to < 0 || from >= m_items.size() || to >= m_items.size())
        return;
    // Don't move separators or drag across them
    if (m_items.at(from).isSeparator || m_items.at(to).isSeparator)
        return;
    // Pinned items form a block at the front whose order is persisted;
    // don't drag items across the pinned/unpinned boundary.
    if (m_items.at(from).pinned != m_items.at(to).pinned)
        return;

    if (!beginMoveRows({}, from, from, {}, to + (to > from ? 1 : 0)))
        return;
    m_items.move(from, to);
    endMoveRows();

    if (m_items.at(to).pinned) {
        QStringList pinned;
        for (const Item &item : std::as_const(m_items)) {
            if (item.pinned && !item.isSeparator)
                pinned.append(item.entry.isValid() ? item.entry.id : item.fallbackAppId);
        }
        m_updatingPinned = true;
        m_config->setPinned(pinned);
        m_updatingPinned = false;
    }
}

void DockModel::activateWindow(int row, int windowIndex)
{
    if (row < 0 || row >= m_items.size())
        return;
    const Item &item = m_items.at(row);
    if (windowIndex >= 0 && windowIndex < item.windows.size())
        item.windows.at(windowIndex)->activate();
}
