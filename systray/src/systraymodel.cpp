#include "systraymodel.h"

#include "dbusmenu.h"
#include "systray.h"
#include "systrayconfig.h"

#include <QIcon>
#include <QPixmap>

SystrayModel::SystrayModel(SystrayHost *host, SystrayConfig *config, QObject *parent)
    : QAbstractListModel(parent)
    , m_host(host)
    , m_config(config)
{
    rebuild();

    if (m_host) {
        connect(m_host, &SystrayHost::itemAdded, this, [this](int /*hostIndex*/) {
            beginResetModel();
            rebuild();
            endResetModel();
            emit countChanged();
        });
        connect(m_host, &SystrayHost::itemRemoved, this, [this](int /*hostIndex*/) {
            beginResetModel();
            rebuild();
            endResetModel();
            emit countChanged();
        });
        connect(m_host, &SystrayHost::itemChanged, this, [this](int /*hostIndex*/) {
            // Could be smarter, but full reset is fine for small lists
            beginResetModel();
            rebuild();
            endResetModel();
        });
    }
    connect(m_config, &SystrayConfig::hiddenItemsChanged, this, [this] {
        beginResetModel();
        rebuild();
        endResetModel();
        emit countChanged();
    });
}

void SystrayModel::rebuild()
{
    m_visible.clear();
    if (!m_host)
        return;
    const auto items = m_host->items();
    const QStringList hidden = m_config->hiddenItems();
    for (int i = 0; i < items.size(); ++i) {
        if (!hidden.contains(items.at(i)->service))
            m_visible.append(i);
        trackItemActivateFallback(items.at(i));
    }
}

void SystrayModel::trackItemActivateFallback(SystrayItem *item)
{
    if (!item || m_trackedItems.contains(item))
        return;
    m_trackedItems.insert(item);
    // A left click that the item refuses to handle becomes a menu: for items
    // like blueman, whose Activate does not exist, that is the only useful
    // outcome. Goes through requestMenu() so QML opens it on the usual
    // menuReady, with no extra wiring on that side.
    connect(item, &SystrayItem::activateFailed, this, [this, item] {
        const int row = rowOfItem(item);
        if (row >= 0 && item->menu())
            requestMenu(row);
    });
    connect(item, &QObject::destroyed, this, [this, item] {
        m_trackedItems.remove(item);
    });
}

int SystrayModel::effectiveIndex(int row) const
{
    if (row < 0 || row >= m_visible.size())
        return -1;
    return m_visible.at(row);
}

int SystrayModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_visible.size();
}

QVariant SystrayModel::data(const QModelIndex &index, int role) const
{
    int eff = effectiveIndex(index.row());
    if (eff < 0)
        return {};
    SystrayItem *item = m_host->items().at(eff);
    if (!item)
        return {};

    switch (role) {
    case IconNameRole:
        return item->iconName;
    case TooltipRole:
        return item->tooltipTitle;
    case ServiceRole:
        return item->service;
    case HasMenuRole:
        return item->hasMenu;
    case IconSerialRole:
        return item->iconSerial;
    case ItemIsMenuRole:
        return item->itemIsMenu;
    }
    return {};
}

QHash<int, QByteArray> SystrayModel::roleNames() const
{
    return {
        {IconNameRole, "iconName"},
        {TooltipRole, "tooltip"},
        {ServiceRole, "service"},
        {HasMenuRole, "hasMenu"},
        {IconSerialRole, "iconSerial"},
        {ItemIsMenuRole, "itemIsMenu"},
    };
}

void SystrayModel::activate(int row, int x, int y)
{
    int eff = effectiveIndex(row);
    if (eff >= 0 && m_host)
        m_host->activateItem(eff, x, y);
}

void SystrayModel::secondaryActivate(int row, int x, int y)
{
    int eff = effectiveIndex(row);
    if (eff >= 0 && m_host)
        m_host->secondaryActivateItem(eff, x, y);
}

void SystrayModel::contextMenu(int row, int x, int y)
{
    int eff = effectiveIndex(row);
    if (eff >= 0 && m_host)
        m_host->showMenu(eff, x, y);
}

int SystrayModel::rowOfItem(const SystrayItem *item) const
{
    if (!m_host || !item)
        return -1;
    const int eff = m_host->items().indexOf(const_cast<SystrayItem *>(item));
    return eff < 0 ? -1 : m_visible.indexOf(eff);
}

void SystrayModel::trackItemMenu(SystrayItem *item)
{
    DBusMenuClient *menu = item ? item->menu() : nullptr;
    if (!menu || m_trackedMenus.contains(menu))
        return;
    m_trackedMenus.insert(menu);
    // The row is resolved when the reply arrives, not now: hiding an item or
    // another one appearing shifts the visible rows in between.
    connect(menu, &DBusMenuClient::layoutReady, this, [this, item] {
        const int row = rowOfItem(item);
        if (row >= 0) emit menuReady(row);
    });
    connect(menu, &DBusMenuClient::layoutFailed, this, [this, item] {
        const int row = rowOfItem(item);
        if (row >= 0) emit menuFailed(row);
    });
    connect(menu, &DBusMenuClient::layoutInvalidated, this, [this, item] {
        const int row = rowOfItem(item);
        if (row >= 0) emit menuInvalidated(row);
    });
    connect(menu, &QObject::destroyed, this, [this, menu] {
        m_trackedMenus.remove(menu);
    });
}

void SystrayModel::requestMenu(int row)
{
    const int eff = effectiveIndex(row);
    SystrayItem *item = (eff >= 0 && m_host) ? m_host->items().at(eff) : nullptr;
    DBusMenuClient *menu = item ? item->menu() : nullptr;
    if (!menu) {
        emit menuFailed(row);
        return;
    }
    trackItemMenu(item);
    // A menu already fetched is served straight away — the tree is kept fresh by
    // the item's LayoutUpdated signal, so there is no reason to make the user
    // wait for a round trip on every right click.
    if (menu->ready()) {
        emit menuReady(row);
        return;
    }
    menu->requestLayout();
}

QVariantList SystrayModel::menuTree(int row) const
{
    const int eff = effectiveIndex(row);
    SystrayItem *item = (eff >= 0 && m_host) ? m_host->items().at(eff) : nullptr;
    DBusMenuClient *menu = item ? item->menu() : nullptr;
    return menu ? menu->tree() : QVariantList();
}

void SystrayModel::triggerMenuItem(int row, int id)
{
    const int eff = effectiveIndex(row);
    SystrayItem *item = (eff >= 0 && m_host) ? m_host->items().at(eff) : nullptr;
    if (item && item->menu())
        item->menu()->trigger(id);
}

void SystrayModel::menuAboutToShow(int row, int id)
{
    const int eff = effectiveIndex(row);
    SystrayItem *item = (eff >= 0 && m_host) ? m_host->items().at(eff) : nullptr;
    if (item && item->menu())
        item->menu()->aboutToShow(id);
}

void SystrayModel::setMenuOpen(int row, bool open)
{
    const int eff = effectiveIndex(row);
    SystrayItem *item = (eff >= 0 && m_host) ? m_host->items().at(eff) : nullptr;
    if (item && item->menu())
        item->menu()->setOpen(open);
}
