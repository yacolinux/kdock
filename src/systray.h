// System tray / StatusNotifierItem host via DBus.
// Implements the freedesktop.org StatusNotifierItem specification.

#pragma once

#include <QDBusAbstractAdaptor>
#include <QDBusMessage>
#include <QDBusServiceWatcher>
#include <QObject>
#include <QPixmap>
#include <QVariantMap>

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
    // Every call this class makes is asynchronous, and that is not an
    // optimisation — it is a correctness requirement. readProperties() runs from
    // inside the D-Bus handler of RegisterStatusNotifierItem, and the client that
    // is registering is typically *blocked* inside that very call waiting for our
    // reply (blueman-tray, and every libappindicator client, register
    // synchronously). Asking it for a property with a blocking call there means
    // both processes wait for each other until the bus gives up: measured
    // 2026-08-21, 24.1 s of frozen dock and 25.2 s of frozen blueman on every
    // kdock start, with the rest of the session's tray clients queued behind it.
    // QDBusInterface is banned here for the same reason: its constructor fetches
    // the introspection XML with a blocking call.
    void applyProperties(const QVariantMap &props);
    // Fallback for items that do not implement Properties.GetAll: one async Get
    // per key, collected into a map and applied when the last one lands.
    void requestPropertiesIndividually();
    // Fire-and-forget method call on the item's SNI interface.
    void callItem(const QString &member, const QVariantList &args);
    DBusMenuClient *m_menu = nullptr;
    // One properties round trip in flight at a time; a refresh asked for while
    // one is pending is remembered and replayed once (an item that emits
    // NewIcon/NewStatus in a burst must not stack round trips).
    bool m_propsPending = false;
    bool m_propsQueued = false;
};

class SystrayHost : public QObject
{
    Q_OBJECT
public:
    explicit SystrayHost(QObject *parent = nullptr);
    ~SystrayHost() override;

    bool active() const { return m_active; }
    // We are the StatusNotifierWatcher of this session, i.e. nobody else was
    // holding the bus name when we started. True under LXQt and on bare
    // wlroots; false under Plasma.
    bool isWatcher() const { return m_isWatcher; }
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

    // ---- the watcher side, called by the two adaptors ---------------------
    // An item registering itself. `serviceOrPath` is either a bus name or an
    // object path; in the second case `caller` (the sender of the D-Bus call)
    // supplies the service.
    void registerItem(const QString &serviceOrPath, const QString &caller);
    // A host registering itself. Only the broadcast matters to us: we are our
    // own host and other hosts are welcome to coexist.
    void registerHost(const QString &service);
    // "service+path" of every known item, which is the id form the spec (and
    // every real watcher) uses. Returning the bare service made a host that
    // re-read this list look for /StatusNotifierItem on an item that lives
    // somewhere else.
    QStringList registeredItemIds() const;

signals:
    void itemAdded(int index);
    void itemRemoved(int index);
    void itemChanged(int index);

    // Relayed onto both watcher interfaces by the adaptors.
    void watcherItemRegistered(const QString &id);
    void watcherItemUnregistered(const QString &id);
    void watcherHostRegistered(const QString &service);

private:
    void ensureWatcher();
    void connectWatcherSignals();
    void onServiceUnregistered(const QString &service);
    void addItem(const QString &service, const QString &path);
    void removeItem(const QString &service);
    int indexOfService(const QString &service) const;

    bool m_active = false;
    bool m_isWatcher = false;
    QDBusServiceWatcher m_watcher;
    QList<SystrayItem *> m_items;
    QString m_hostService;
    // The watcher bus name we actually talk to (org.kde.* on KDE; the one we
    // registered ourselves otherwise). Its interface name equals the service.
    QString m_watcherService;
    // Parent of the two watcher adaptors and the object registered at
    // /StatusNotifierWatcher. Deliberately NOT SystrayHost itself: adaptors
    // belong to an object, not to a path, so hanging them off the host would
    // also export the watcher interfaces at /StatusNotifierHost, which is the
    // same object registered at a second path.
    QObject m_watcherObject;

private slots:
    void onItemRegistered(const QString &service);
    void onItemUnregistered(const QString &service);
};

// One StatusNotifierWatcher interface, forwarding to SystrayHost.
//
// There are two of these because the interface name is baked into the class by
// Q_CLASSINFO and a single object cannot answer to two of them — which is
// exactly the bug this fixes: kdock exported only the freedesktop name while
// calling *itself* with the org.kde one, so becoming the watcher (LXQt, bare
// wlroots) ended in "No such interface … at /StatusNotifierWatcher" and the
// tray never registered a single item.
//
// Everything the spec declares as a property is a property here. It reads like
// pedantry and is not: Qt's own QDBusTrayIcon (so every Qt application using
// QSystemTrayIcon) reads IsStatusNotifierHostRegistered as a property, and when
// that read fails it falls back to X11's XEmbed — which on Wayland means no
// tray icon at all.
class SniWatcherAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_PROPERTY(QStringList RegisteredStatusNotifierItems READ registeredItems)
    Q_PROPERTY(bool IsStatusNotifierHostRegistered READ hostRegistered)
    Q_PROPERTY(int ProtocolVersion READ protocolVersion)

public:
    SniWatcherAdaptor(SystrayHost *host, QObject *parent);

    QStringList registeredItems() const;
    bool hostRegistered() const;
    int protocolVersion() const { return 0; }

public slots:
    // The trailing QDBusMessage is Qt's documented way of learning who called
    // without QDBusContext (which only works for the registered object, not for
    // its adaptors). It is stripped from the exported signature.
    void RegisterStatusNotifierItem(const QString &serviceOrPath, const QDBusMessage &msg);
    void RegisterStatusNotifierHost(const QString &service);
    // Not in any watcher of this codebase before, yet SystrayHost's destructor
    // has always called it: without it, every shutdown logged an error.
    void UnregisterStatusNotifierHost(const QString &service);

signals:
    void StatusNotifierItemRegistered(const QString &id);
    void StatusNotifierItemUnregistered(const QString &id);
    void StatusNotifierHostRegistered();
    void StatusNotifierHostUnregistered();

protected:
    SystrayHost *m_host = nullptr;
};

class SniWatcherKdeAdaptor : public SniWatcherAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.StatusNotifierWatcher")
public:
    using SniWatcherAdaptor::SniWatcherAdaptor;
};

class SniWatcherFdoAdaptor : public SniWatcherAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.StatusNotifierWatcher")
public:
    using SniWatcherAdaptor::SniWatcherAdaptor;
};
