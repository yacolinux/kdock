// System tray / StatusNotifierItem host via DBus.
// Implements the freedesktop.org StatusNotifierItem specification.

#pragma once

#include <QDBusContext>
#include <QDBusInterface>
#include <QDBusServiceWatcher>
#include <QObject>
#include <QPixmap>

class DBusMenuClient;

class SystrayItem : public QObject
{
    Q_OBJECT
public:
    explicit SystrayItem(const QString &service, const QString &path, QObject *parent = nullptr);
    ~SystrayItem() override;

    void refresh();
    void activate(int x, int y);
    void secondaryActivate(int x, int y);
    void contextMenu(int x, int y);

    // The item's own menu, fetched over DBusMenu and drawn by us. Null when the
    // item exposes no Menu path.
    DBusMenuClient *menu() const { return m_menu; }

    QString service;
    QString path;
    QString iconName;
    QString iconThemePath;
    QString overlayIconName;
    QString tooltipTitle;
    QString tooltipSub;
    QString status; // Passive, Active, NeedsAttention
    QString category;
    QString title;
    QString menuPath;
    bool hasMenu = false;
    // The item declares itself menu-only: a left click must show the menu
    // instead of calling Activate (which such items leave unimplemented or
    // pointing at nothing).
    bool itemIsMenu = false;
    QPixmap iconPixmap;
    int iconWidth = 0;
    int iconHeight = 0;
    // Bumped on every property refresh so QML can bust the image:// cache of
    // pixmap-only icons (same trick as theme.revision for themed icons).
    int iconSerial = 0;

signals:
    void changed();
    // Activate() came back with an error: the item does not really implement it
    // (blueman does not even export the method), so the caller should fall back
    // to showing the menu.
    void activateFailed();

private slots:
    // A slot so the SNI NewIcon/NewToolTip/... signals can be wired to it.
    void readProperties();

private:
    QPixmap decodePixmap(const QVariant &variant);
    // Read an SNI pixmap property (a(iiay)) via a raw Properties.Get call —
    // QDBusInterface::property() does not reliably demarshal that type. Returns
    // the largest frame, converted from big-endian ARGB32.
    QPixmap readPixmapProperty(const QString &name) const;
    QDBusInterface *m_iface = nullptr;
    DBusMenuClient *m_menu = nullptr;
};

class SystrayHost : public QObject, protected QDBusContext
{
    Q_OBJECT
public:
    explicit SystrayHost(QObject *parent = nullptr);
    ~SystrayHost() override;

    bool active() const { return m_active; }
    QList<SystrayItem *> items() const { return m_items; }
    SystrayItem *itemAt(int index) const;
    // Raw IconPixmap of the item with the given service (for the systray image
    // provider); empty if not found or the item has no pixmap.
    QPixmap iconPixmapForService(const QString &service) const;
    // Same, for a menu entry that ships raw icon-data instead of a theme icon
    // name (Qt/KDE items embed a PNG blob per entry).
    QPixmap menuIconPixmapForService(const QString &service, int itemId) const;

    void activateItem(int index, int x, int y);
    void secondaryActivateItem(int index, int x, int y);
    void showMenu(int index, int x, int y);

    Q_INVOKABLE void refreshItem(int index);

signals:
    void itemAdded(int index);
    void itemRemoved(int index);
    void itemChanged(int index);

protected:
    // org.freedesktop.DBus.Introspectable
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.StatusNotifierWatcher")

public slots:
    // Called by items registering themselves
    Q_SCRIPTABLE void RegisterStatusNotifierItem(const QString &serviceOrPath);
    Q_SCRIPTABLE void RegisterStatusNotifierHost(const QString &service);
    Q_SCRIPTABLE QStringList RegisteredStatusNotifierItems() const;
    Q_SCRIPTABLE bool IsStatusNotifierHostRegistered() const;
    Q_SCRIPTABLE uint ProtocolVersion() const { return 0; }

private:
    void ensureWatcher();
    void connectWatcherSignals();
    void onServiceUnregistered(const QString &service);
    void addItem(const QString &service, const QString &path);
    void removeItem(const QString &service);
    int indexOfService(const QString &service) const;

    bool m_active = false;
    QDBusServiceWatcher m_watcher;
    QList<SystrayItem *> m_items;
    QString m_hostService;
    // The watcher bus name we actually talk to (org.kde.* on KDE; the one we
    // registered ourselves otherwise). Its interface name equals the service.
    QString m_watcherService;

private slots:
    void onItemRegistered(const QString &service);
    void onItemUnregistered(const QString &service);
};
