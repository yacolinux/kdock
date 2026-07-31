// QAbstractListModel exposing the SNI items to QML.

#pragma once

#include <QAbstractListModel>
#include <QSet>
#include <QStringList>

class SystrayHost;
class DockConfig;

class SystrayModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Roles {
        IconNameRole = Qt::UserRole + 1,
        TooltipRole,
        ServiceRole,
        HasMenuRole,
        IconSerialRole, // bumps on refresh; busts the image://systray cache
        ItemIsMenuRole, // left click must open the menu, not activate
    };

    explicit SystrayModel(SystrayHost *host, DockConfig *config, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const { return rowCount(); }

    Q_INVOKABLE void activate(int row, int x, int y);
    Q_INVOKABLE void secondaryActivate(int row, int x, int y);
    // Last resort only: asks the *item* to draw its own menu, which cannot work
    // on Wayland (no surface to parent a popup to). Used when the item exposes
    // no DBusMenu at all.
    Q_INVOKABLE void contextMenu(int row, int x, int y);

    // Menu of the item, fetched over DBusMenu and drawn by the dock. Asking is
    // asynchronous: QML calls requestMenu() and opens on menuReady(row).
    Q_INVOKABLE void requestMenu(int row);
    Q_INVOKABLE QVariantList menuTree(int row) const;
    Q_INVOKABLE void triggerMenuItem(int row, int id);
    Q_INVOKABLE void menuAboutToShow(int row, int id);
    Q_INVOKABLE void setMenuOpen(int row, bool open);

signals:
    void countChanged();
    void menuReady(int row);
    // No menu, or the item failed to answer: the caller falls back to the SNI
    // ContextMenu method.
    void menuFailed(int row);
    // The item rebuilt its menu while it was open.
    void menuInvalidated(int row);

private:
    void rebuild();
    int effectiveIndex(int row) const;
    // Row a host item currently occupies, or -1 when hidden: menu replies come
    // back from the item, and the visible rows may have shifted meanwhile.
    int rowOfItem(const class SystrayItem *item) const;
    // Wires an item's menu signals to ours, once per menu client.
    void trackItemMenu(class SystrayItem *item);
    void trackItemActivateFallback(class SystrayItem *item);

    SystrayHost *m_host;
    DockConfig *m_config;
    QList<int> m_visible; // indices into m_host->items()
    // Items whose menu signals are already connected (a menu client is created
    // lazily and replaced if the item moves its Menu path).
    QSet<const class DBusMenuClient *> m_trackedMenus;
    QSet<const class SystrayItem *> m_trackedItems;
};
