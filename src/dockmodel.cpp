#include "dockmodel.h"

#include "virtualdesktops.h"

#include "dockconfig.h"
#include "translations.h"
#include "windowmonitor.h"

#include <QVariantMap>

QString DockModel::Item::displayName() const
{
    if (entry.isValid() && !entry.name.isEmpty())
        return appNameFor(entry);
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

QString DockModel::Item::appKey() const
{
    return entry.isValid() ? entry.id.toLower() : fallbackAppId.toLower();
}

DockModel::DockModel(DockConfig *config, DesktopEntryIndex *apps, WindowMonitor *monitor,
                     VirtualDesktops *desktops, const QString &widgetToken, QObject *parent)
    : QAbstractListModel(parent)
    , m_config(config)
    , m_widgetToken(widgetToken)
    , m_apps(apps)
    , m_monitor(monitor)
    , m_desktops(desktops)
{
    if (m_widgetToken.isEmpty()) {
        connect(m_config, &DockConfig::pinnedChanged, this, [this] {
            if (!m_updatingPinned)
                rebuild();
        });
        connect(m_config, &DockConfig::separator1Changed, this, &DockModel::rebuild);
        connect(m_config, &DockConfig::separator2Changed, this, &DockModel::rebuild);
        connect(m_config, &DockConfig::separator1TransparentChanged, this, &DockModel::rebuild);
        connect(m_config, &DockConfig::separator2TransparentChanged, this, &DockModel::rebuild);
    } else {
        // Both signals carry a token: several appsel models share this config
        // and each one has to ignore the others' edits — *except* when this
        // widget skips what the others pin, which makes their lists an input of
        // this model.
        connect(m_config, &DockConfig::widgetAppsChanged, this, [this](const QString &token) {
            if (m_updatingPinned)
                return;
            if (token == m_widgetToken || m_config->widgetExcludeOthers(m_widgetToken))
                rebuild();
        });
        connect(m_config, &DockConfig::widgetOnlyPinnedChanged, this,
                [this](const QString &token) {
                    if (token == m_widgetToken)
                        rebuild();
                });
        connect(m_config, &DockConfig::widgetExcludeOthersChanged, this,
                [this](const QString &token) {
                    if (token == m_widgetToken)
                        rebuild();
                });
        // A widget added or removed changes the set of "the others" too, and
        // that arrives as an order change and nothing else.
        connect(m_config, &DockConfig::widgetOrderChanged, this, [this] {
            if (m_config->widgetExcludeOthers(m_widgetToken))
                rebuild();
        });
        // The monitor-wide filter, kept on its own wires end to end so it can
        // neither read nor disturb the dock-local one above. Its input is every
        // appsel list of the screen, which is why it also listens to a signal
        // relayed from the other docks' configs (monitorAppsChanged).
        connect(m_config, &DockConfig::widgetExcludeMonitorChanged, this,
                [this](const QString &token) {
                    if (token == m_widgetToken)
                        rebuild();
                });
        connect(m_config, &DockConfig::monitorAppsChanged, this, [this] {
            if (m_config->widgetExcludeMonitor(m_widgetToken))
                rebuild();
        });
        connect(m_config, &DockConfig::widgetAppsChanged, this, [this](const QString &token) {
            if (m_updatingPinned)
                return;
            if (token != m_widgetToken && m_config->widgetExcludeMonitor(m_widgetToken))
                rebuild();
        });
        connect(m_config, &DockConfig::widgetOrderChanged, this, [this] {
            if (m_config->widgetExcludeMonitor(m_widgetToken))
                rebuild();
        });
        connect(m_config, &DockConfig::widgetUngroupWindowsChanged, this,
                [this](const QString &token) {
                    if (token == m_widgetToken)
                        rebuild();
                });
    }
    connect(m_config, &DockConfig::groupWindowsChanged, this, [this] { rebuild(); });
    // App names come from the translation layer (see Item::displayName), so a
    // language change is a rename of every row at once.
    if (Translations *layer = Translations::instance()) {
        connect(layer, &Translations::changed, this, [this] {
            if (m_items.isEmpty())
                return;
            emit dataChanged(index(0), index(m_items.size() - 1), {NameRole});
        });
    }
    if (m_monitor) {
        const auto watchWindow = [this](AbstractWindow *w) {
            connect(w, &AbstractWindow::changed, this, [this, w] { windowChanged(w); });
        };
        connect(m_monitor, &WindowMonitor::windowAdded, this, [this, watchWindow](AbstractWindow *w) {
            watchWindow(w);
            if (!w->skipTaskbar)
                placeWindow(w);
        });
        connect(m_monitor, &WindowMonitor::windowRemoved, this, &DockModel::removeWindow);
        // An appsel model is created lazily while Dock.qml loads its section.
        // By then KWin may already have announced the window, so windowAdded
        // will never be emitted for it again. Without this connection, a later
        // app_id/state update is invisible to the widget and a launcher keeps
        // looking idle even though its window is already open.
        for (AbstractWindow *w : std::as_const(m_monitor->windows))
            watchWindow(w);
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
        // Ungrouped, the extra window icons of a launcher have no flag of their
        // own but belong to the same list entry (see pinnedByApp).
        return item.pinned || pinnedByApp(item);
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
    case SeparatorTransparentRole:
        return item.separatorTransparent;
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
        {SeparatorTransparentRole, "separatorTransparent"},
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

bool DockModel::groupsWindows() const
{
    if (!m_config->groupWindows())
        return false;
    return m_widgetToken.isEmpty() || !m_config->widgetUngroupWindows(m_widgetToken);
}

bool DockModel::listsApp(const QString &key) const
{
    for (const QString &id : pinnedIds())
        if (keyForAppId(id, nullptr) == key)
            return true;
    return false;
}

bool DockModel::pinnedByApp(const Item &item) const
{
    if (item.pinned || item.isSeparator || groupsWindows())
        return false;
    return listsApp(item.appKey());
}

int DockModel::rowOfPinnedApp(const QString &appKey) const
{
    for (int i = 0; i < m_items.size(); ++i) {
        const Item &item = m_items.at(i);
        if (item.pinned && !item.isSeparator && item.appKey() == appKey)
            return i;
    }
    return -1;
}

QString DockModel::keyForWindow(AbstractWindow *w) const
{
    const QString appKey = keyForAppId(w->appId, nullptr);
    if (groupsWindows())
        return appKey;
    // Ungrouped: the first window reuses the app/launcher row, so a pinned
    // launcher collapses into the first window's icon instead of duplicating.
    // Every later window becomes its own separate icon.
    const int row = rowOfKey(appKey);
    if (row >= 0 && m_items.at(row).windows.isEmpty())
        return appKey;
    return QStringLiteral("win:") + QString::number(reinterpret_cast<quintptr>(w));
}

QStringList DockModel::pinnedIds() const
{
    return m_widgetToken.isEmpty() ? m_config->pinned()
                                   : m_config->widgetApps(m_widgetToken);
}

void DockModel::savePinnedIds(const QStringList &ids)
{
    m_updatingPinned = true;
    if (m_widgetToken.isEmpty())
        m_config->setPinned(ids);
    else
        m_config->setWidgetApps(m_widgetToken, ids);
    m_updatingPinned = false;
}

bool DockModel::acceptsStrayWindows() const
{
    return m_widgetToken.isEmpty() || !m_config->widgetOnlyPinned(m_widgetToken);
}

bool DockModel::acceptsStrayWindow(const QString &appId) const
{
    if (m_widgetToken.isEmpty())
        return acceptsStrayWindows();
    // keyForAppId(), not keyForWindow(): ungrouped mode keys extra windows by
    // pointer, and what the other widgets list are applications.
    const QString key = keyForAppId(appId, nullptr);
    // Ungrouped, the second window of an app this widget lists is asked about
    // here (the first one took the launcher row, every later one is keyed by
    // pointer and has no row yet). It is not a stray: it is the whole point of
    // the flag, so it comes in ahead of "only pinned" and of the two exclusion
    // filters — which never hide a widget's own launchers either.
    if (!groupsWindows() && listsApp(key))
        return true;
    if (!acceptsStrayWindows())
        return false;
    // The monitor-wide filter reads its own cached set and never touches the
    // dock-local one below, so turning either on leaves the other's behaviour
    // exactly as it was.
    if (m_config->widgetExcludeMonitor(m_widgetToken) && m_monitorPinned.contains(key))
        return false;
    if (!m_config->widgetExcludeOthers(m_widgetToken))
        return true;
    for (const QString &id : m_config->appsPinnedElsewhere(m_widgetToken))
        if (keyForAppId(id, nullptr) == key)
            return false;
    return true;
}

void DockModel::refreshMonitorPinned()
{
    if (m_widgetToken.isEmpty() || !m_config->widgetExcludeMonitor(m_widgetToken)) {
        m_monitorPinned.clear();
        return;
    }
    const QStringList ids = m_config->appsPinnedOnMonitor(m_widgetToken);
    m_monitorPinned.clear();
    for (const QString &id : ids)
        m_monitorPinned.insert(keyForAppId(id, nullptr));
}

void DockModel::rebuild()
{
    refreshMonitorPinned();
    beginResetModel();
    m_items.clear();

    for (const QString &id : pinnedIds()) {
        Item item;
        item.entry = m_apps->forAppId(id);
        item.key = item.entry.isValid() ? item.entry.id.toLower() : id.toLower();
        if (!item.entry.isValid())
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
                // "only pinned", or already drawn by another widget: this
                // window gets no icon here.
                if (!acceptsStrayWindow(w->appId))
                    continue;
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

    // Insert separators (descending order so indices don't shift). They belong
    // to the dock's apps block: an appsel widget places its own icons and would
    // otherwise draw the same two lines again, at indices that mean nothing in
    // its list.
    QList<int> sepPos = m_widgetToken.isEmpty()
        ? QList<int>{m_config->separator1(), m_config->separator2()}
        : QList<int>{};
    std::sort(sepPos.begin(), sepPos.end(), std::greater<int>());
    for (int pos : sepPos) {
        if (pos < 0 || pos > m_items.size())
            continue;
        Item sep;
        sep.isSeparator = true;
        const bool first = pos == m_config->separator1();
        sep.separatorIndex = first ? 1 : 2;
        sep.separatorTransparent = first ? m_config->separator1Transparent()
                                         : m_config->separator2Transparent();
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
        if (!acceptsStrayWindow(window->appId))
            return;
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
        if (groupsWindows()) {
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
    QStringList pinned = pinnedIds();

    if (m_items.at(row).pinned || pinnedByApp(m_items.at(row))) {
        const Item &item = m_items.at(row);
        pinned.removeAll(item.entry.isValid() ? item.entry.id : item.fallbackAppId);
        // Ungrouped, one launcher entry can back several rows (its extra windows
        // are icons of their own), and all of them read as pinned. The flag
        // itself lives on the row that came from the list, which is the one that
        // would outlive its windows as an orphan launcher — so that is the row
        // to clear, clicked or not.
        const int owner = item.pinned ? row : rowOfPinnedApp(item.appKey());
        if (owner >= 0) {
            m_items[owner].pinned = false;
            if (m_items.at(owner).windows.isEmpty()) {
                beginRemoveRows({}, owner, owner);
                m_items.removeAt(owner);
                endRemoveRows();
            }
        }
        // Every row of that app just changed how it reads, not only the one
        // clicked.
        if (!m_items.isEmpty())
            emit dataChanged(index(0), index(m_items.size() - 1), {PinnedRole});
    } else {
        Item &item = m_items[row];
        if (!item.entry.isValid())
            return; // nothing to launch later; not pinnable
        // Re-key the row to the application. Ungrouped mode keys a window row
        // by the window itself ("win:<pointer>"), a key no launcher can ever
        // match: leaving it would turn this row into an orphan launcher the
        // moment its window closes (the row survives because it is pinned).
        // Clicking it would then find no windows and start a new instance every
        // time, and each new window would add yet another icon, because
        // keyForWindow() cannot find the launcher either. And when another row
        // already owns the app key, that row is the launcher: giving this one
        // the flag would leave two pinned rows behind a single list entry, so
        // the pin goes there instead.
        const QString appKey = item.entry.id.toLower();
        const int owner = rowOfKey(appKey);
        if (owner < 0)
            item.key = appKey;
        if (!pinned.contains(item.entry.id))
            pinned.append(item.entry.id);
        m_items[owner < 0 ? row : owner].pinned = true;
        emit dataChanged(index(0), index(m_items.size() - 1), {PinnedRole});
    }

    savePinnedIds(pinned);
    // savePinnedIds() suppresses this model's own rebuild (it has just edited
    // its rows in place), which for the dock's apps block is exactly right: an
    // unpinned app that is still running keeps its icon. A widget can *filter*
    // by that list, though — with "only pinned" on, the row it just dropped
    // must go, and with a monitor filter its neighbours' lists changed too. So
    // the widget re-reads itself, or it keeps drawing an app it no longer lists
    // until some unrelated change rebuilds it, which from the outside looks
    // exactly like the unpin did nothing (reported 2026-08-15).
    if (!m_widgetToken.isEmpty())
        rebuild();
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

QVariantMap DockModel::previewWindow(int row) const
{
    QVariantMap map;
    if (row < 0 || row >= m_items.size())
        return map;
    const Item &item = m_items.at(row);
    if (item.isSeparator || item.windows.isEmpty())
        return map;

    AbstractWindow *pick = item.windows.first();
    for (AbstractWindow *w : item.windows) {
        if (w->activated) {
            pick = w;
            break;
        }
    }
    // The wlr backend leaves both empty: no uuid means no capture, and QML
    // treats that exactly like "no window".
    if (pick->uuid.isEmpty())
        return map;

    map[QStringLiteral("uuid")] = pick->uuid;
    map[QStringLiteral("width")] = pick->geometry.width();
    map[QStringLiteral("height")] = pick->geometry.height();
    // What the preview's window buttons need: which window they act on, and the
    // two states they toggle. `windowIndex` is the index into this row's window
    // list, i.e. the same coordinate activateWindow() and the per-window menu
    // entries already use.
    map[QStringLiteral("windowIndex")] = item.windows.indexOf(pick);
    map[QStringLiteral("title")] = pick->title.isEmpty() ? item.displayName() : pick->title;
    map[QStringLiteral("minimized")] = pick->minimized;
    map[QStringLiteral("maximized")] = pick->maximized;
    // The two backends spell the capability differently: plasma-window fills the
    // flag from its state bitmask, wlr-foreign-toplevel answers the method. Ask
    // both, or the maximize button would never show on one of them.
    map[QStringLiteral("maximizable")] = pick->maximizable || pick->canMaximize();
    return map;
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
        savePinnedIds(pinned);
    }
}

void DockModel::activateWindow(int row, int windowIndex)
{
    if (AbstractWindow *w = windowAt(row, windowIndex))
        w->activate();
}

AbstractWindow *DockModel::windowAt(int row, int windowIndex) const
{
    if (row < 0 || row >= m_items.size())
        return nullptr;
    const Item &item = m_items.at(row);
    if (windowIndex < 0 || windowIndex >= item.windows.size())
        return nullptr;
    return item.windows.at(windowIndex);
}

void DockModel::closeWindow(int row, int windowIndex)
{
    if (AbstractWindow *w = windowAt(row, windowIndex))
        w->requestClose();
}

void DockModel::setWindowMinimized(int row, int windowIndex, bool on)
{
    AbstractWindow *w = windowAt(row, windowIndex);
    if (!w)
        return;
    if (on) {
        w->minimize();
    } else {
        // Restoring means "give it back to me", not just "undo the minimize":
        // an unminimized window that stays behind everything else reads as a
        // button that did nothing. Same pair activate() already does.
        w->unminimize();
        w->activate();
    }
}

void DockModel::setWindowMaximized(int row, int windowIndex, bool on)
{
    if (AbstractWindow *w = windowAt(row, windowIndex))
        w->setMaximized(on);
}
