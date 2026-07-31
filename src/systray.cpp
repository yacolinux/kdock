#include "systray.h"

#include "dbusmenu.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QDBusVariant>
#include <QDebug>
#include <QIcon>
#include <QImage>
#include <QPainter>
#include <QPixmap>
#include <QVariantMap>
#include <QtEndian>

// The StatusNotifierItem spec is KDE-originated: in practice every watcher,
// host and item uses the org.kde.* bus names (KDE, libappindicator, Qt/GTK
// trays). The org.freedesktop.* names are essentially never implemented, so we
// speak org.kde.* and only register the freedesktop name as an extra alias when
// we have to become the watcher ourselves (bare wlroots sessions).
static const QString KDE_WATCHER_SERVICE = QStringLiteral("org.kde.StatusNotifierWatcher");
static const QString FDO_WATCHER_SERVICE = QStringLiteral("org.freedesktop.StatusNotifierWatcher");
static const QString WATCHER_PATH = QStringLiteral("/StatusNotifierWatcher");
static const QString ITEM_IFACE = QStringLiteral("org.kde.StatusNotifierItem");

// SNI pixmap wire types: IconPixmap et al. are a(iiay) = array of
// (width, height, ARGB32-big-endian bytes). These must be registered with
// QtDBus (qDBusRegisterMetaType) or QDBusArgument demarshalling desyncs and
// libdbus aborts ("type struct not a basic type").
struct KDbusImageStruct {
    int width = 0;
    int height = 0;
    QByteArray data;
};
using KDbusImageVector = QList<KDbusImageStruct>;

Q_DECLARE_METATYPE(KDbusImageStruct)
Q_DECLARE_METATYPE(KDbusImageVector)

static QDBusArgument &operator<<(QDBusArgument &arg, const KDbusImageStruct &img)
{
    arg.beginStructure();
    arg << img.width << img.height << img.data;
    arg.endStructure();
    return arg;
}

static const QDBusArgument &operator>>(const QDBusArgument &arg, KDbusImageStruct &img)
{
    arg.beginStructure();
    arg >> img.width >> img.height >> img.data;
    arg.endStructure();
    return arg;
}

static QDBusArgument &operator<<(QDBusArgument &arg, const KDbusImageVector &vec)
{
    arg.beginArray(qMetaTypeId<KDbusImageStruct>());
    for (const auto &img : vec)
        arg << img;
    arg.endArray();
    return arg;
}

static const QDBusArgument &operator>>(const QDBusArgument &arg, KDbusImageVector &vec)
{
    arg.beginArray();
    vec.clear();
    while (!arg.atEnd()) {
        KDbusImageStruct img;
        arg >> img;
        vec.append(img);
    }
    arg.endArray();
    return arg;
}

// The watcher stores/broadcasts items as the concatenation of the owner's bus
// name and the object path (e.g. ":1.67/org/blueman/sni" or ":1.7/StatusNotifierItem").
// Split at the first '/'; a bare service (no path) defaults to /StatusNotifierItem.
static void splitItemId(const QString &id, QString &service, QString &path)
{
    const int slash = id.indexOf(QLatin1Char('/'));
    if (slash < 0) {
        service = id;
        path = QStringLiteral("/StatusNotifierItem");
    } else {
        service = id.left(slash);
        path = id.mid(slash);
    }
}

// ---------------------------------------------------------------------------
// SystrayItem
// ---------------------------------------------------------------------------

SystrayItem::SystrayItem(const QString &service, const QString &path, QObject *parent)
    : QObject(parent)
    , service(service)
    , path(path)
    , m_iface(new QDBusInterface(service, path, ITEM_IFACE,
                                  QDBusConnection::sessionBus(), this))
{
    readProperties();

    // SNI items don't emit org.freedesktop.DBus.Properties.PropertiesChanged;
    // they emit their own signals on org.kde.StatusNotifierItem. Re-read all
    // properties on any of them.
    static const char *const kSignals[] = {"NewIcon", "NewOverlayIcon",
        "NewAttentionIcon", "NewToolTip", "NewTitle", "NewStatus"};
    for (const char *sig : kSignals)
        QDBusConnection::sessionBus().connect(service, path, ITEM_IFACE,
            QString::fromLatin1(sig), this, SLOT(readProperties()));
}

SystrayItem::~SystrayItem()
{
    static const char *const kSignals[] = {"NewIcon", "NewOverlayIcon",
        "NewAttentionIcon", "NewToolTip", "NewTitle", "NewStatus"};
    for (const char *sig : kSignals)
        QDBusConnection::sessionBus().disconnect(service, path, ITEM_IFACE,
            QString::fromLatin1(sig), this, SLOT(readProperties()));
}

void SystrayItem::readProperties()
{
    if (!m_iface || !m_iface->isValid())
        return;

    iconName = m_iface->property("IconName").toString();
    iconThemePath = m_iface->property("IconThemePath").toString();
    overlayIconName = m_iface->property("OverlayIconName").toString();
    status = m_iface->property("Status").toString();
    category = m_iface->property("Category").toString();
    title = m_iface->property("Title").toString();
    itemIsMenu = m_iface->property("ItemIsMenu").toBool();
    menuPath = m_iface->property("Menu").value<QDBusObjectPath>().path();
    hasMenu = !menuPath.isEmpty();
    // The menu client outlives single property reads (it caches the layout and
    // listens for updates), so build it once and only rebuild if the item moves
    // its menu somewhere else.
    if (hasMenu && (!m_menu || m_menu->objectName() != menuPath)) {
        delete m_menu;
        m_menu = new DBusMenuClient(service, menuPath, this);
        m_menu->setObjectName(menuPath);
    } else if (!hasMenu && m_menu) {
        delete m_menu;
        m_menu = nullptr;
    }

    QVariant tooltipVar = m_iface->property("Tooltip");
    if (tooltipVar.isValid() && tooltipVar.canConvert<QVariantMap>()) {
        QVariantMap map = tooltipVar.toMap();
        tooltipTitle = map.value(QStringLiteral("title"), map.value(QStringLiteral("Title"))).toString();
        tooltipSub = map.value(QStringLiteral("description"), map.value(QStringLiteral("Description"))).toString();
    } else if (tooltipVar.isValid() && tooltipVar.canConvert<QDBusArgument>()) {
        QDBusArgument arg = tooltipVar.value<QDBusArgument>();
        arg.beginStructure();
        QString s1, s2, s3;
        arg >> s1 >> s2;
        if (arg.currentType() == QDBusArgument::VariantType) {
            QDBusVariant dv; arg >> dv;
        } else if (arg.currentType() == QDBusArgument::ArrayType) {
            arg.beginArray(); arg.endArray();
        }
        arg >> s3;
        arg.endStructure();
        tooltipTitle = s1.isEmpty() ? s3 : s1;
    } else {
        tooltipTitle = m_iface->property("Title").toString();
    }

    if (iconName.isEmpty()) {
        iconPixmap = readPixmapProperty(QStringLiteral("IconPixmap"));
        if (iconPixmap.isNull()) // some trays only set the attention icon
            iconPixmap = readPixmapProperty(QStringLiteral("AttentionIconPixmap"));
        iconWidth = iconPixmap.width();
        iconHeight = iconPixmap.height();
    } else {
        iconPixmap = QPixmap();
        iconWidth = iconHeight = 0;
    }

    ++iconSerial;
    emit changed();
}

QPixmap SystrayItem::readPixmapProperty(const QString &name) const
{
    if (!m_iface)
        return {};
    // With the meta types registered (see SystrayHost ctor), QDBusInterface
    // demarshals the a(iiay) property straight into KDbusImageVector.
    const KDbusImageVector images =
        m_iface->property(name.toLatin1().constData()).value<KDbusImageVector>();

    // Pick the largest frame with a plausible ARGB32 payload.
    const KDbusImageStruct *best = nullptr;
    for (const KDbusImageStruct &img : images) {
        if (img.width > 0 && img.height > 0
            && img.data.size() == img.width * img.height * 4
            && (!best || img.width * img.height > best->width * best->height)) {
            best = &img;
        }
    }
    if (!best)
        return {};

    // SNI pixmaps are ARGB32 in network (big-endian) byte order; convert to the
    // native QImage layout.
    QImage out(best->width, best->height, QImage::Format_ARGB32);
    const uchar *src = reinterpret_cast<const uchar *>(best->data.constData());
    for (int y = 0; y < best->height; ++y) {
        QRgb *dst = reinterpret_cast<QRgb *>(out.scanLine(y));
        for (int x = 0; x < best->width; ++x)
            dst[x] = qFromBigEndian<quint32>(src + (y * best->width + x) * 4);
    }
    return QPixmap::fromImage(out);
}

QPixmap SystrayItem::decodePixmap(const QVariant &variant)
{
    if (!variant.isValid() || !variant.canConvert<QDBusArgument>())
        return {};
    QDBusArgument arg = variant.value<QDBusArgument>();
    arg.beginArray();
    QPixmap result;
    while (!arg.atEnd()) {
        arg.beginStructure();
        int w = 0, h = 0;
        QByteArray data;
        arg >> w >> h >> data;
        arg.endStructure();
        if (w > 0 && h > 0 && data.size() == w * h * 4) {
            QImage img(reinterpret_cast<const uchar *>(data.constData()), w, h, QImage::Format_ARGB32);
            result = QPixmap::fromImage(img.copy());
            break;
        }
    }
    arg.endArray();
    return result;
}

void SystrayItem::refresh()
{
    readProperties();
}

void SystrayItem::activate(int x, int y)
{
    if (!m_iface || !m_iface->isValid())
        return;
    // Asynchronous on purpose, and the reply *is* inspected: an item whose
    // Activate is missing or fails is the signal to show its menu instead, which
    // is the only sensible thing a left click can do there. A blocking call
    // would also freeze the dock while the item opens its window.
    auto *watcher = new QDBusPendingCallWatcher(
        m_iface->asyncCall(QStringLiteral("Activate"), x, y), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher] {
        watcher->deleteLater();
        if (QDBusPendingReply<> reply = *watcher; reply.isError())
            emit activateFailed();
    });
}

void SystrayItem::secondaryActivate(int x, int y)
{
    if (m_iface && m_iface->isValid())
        m_iface->call(QStringLiteral("SecondaryActivate"), x, y);
}

void SystrayItem::contextMenu(int x, int y)
{
    if (m_iface && m_iface->isValid())
        m_iface->call(QStringLiteral("ContextMenu"), x, y);
}

// ---------------------------------------------------------------------------
// SystrayHost
// ---------------------------------------------------------------------------

SystrayHost::SystrayHost(QObject *parent)
    : QObject(parent)
{
    // Register the SNI pixmap types before any item property is read.
    qDBusRegisterMetaType<KDbusImageStruct>();
    qDBusRegisterMetaType<KDbusImageVector>();

    m_watcher.setConnection(QDBusConnection::sessionBus());
    ensureWatcher();
    connect(&m_watcher, &QDBusServiceWatcher::serviceUnregistered,
            this, &SystrayHost::onServiceUnregistered);
}

SystrayHost::~SystrayHost()
{
    if (!m_hostService.isEmpty() && !m_watcherService.isEmpty()) {
        QDBusMessage msg = QDBusMessage::createMethodCall(m_watcherService, WATCHER_PATH,
            m_watcherService, QStringLiteral("UnregisterStatusNotifierHost"));
        msg.setArguments({m_hostService});
        QDBusConnection::sessionBus().call(msg, QDBus::NoBlock);
    }
    for (SystrayItem *item : std::as_const(m_items))
        item->deleteLater();
    m_items.clear();
}

void SystrayHost::ensureWatcher()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    QDBusConnectionInterface *dbus = bus.interface();
    const bool kdeExists = dbus->isServiceRegistered(KDE_WATCHER_SERVICE);
    const bool fdoExists = dbus->isServiceRegistered(FDO_WATCHER_SERVICE);

    if (kdeExists) {
        m_watcherService = KDE_WATCHER_SERVICE;
    } else if (fdoExists) {
        m_watcherService = FDO_WATCHER_SERVICE;
    } else {
        // No watcher present (e.g. bare wlroots): become one ourselves, under
        // both bus names so KDE- and freedesktop-style clients both find us.
        bus.registerObject(WATCHER_PATH, this, QDBusConnection::ExportAllSlots);
        bus.registerService(KDE_WATCHER_SERVICE);
        bus.registerService(FDO_WATCHER_SERVICE);
        m_watcherService = KDE_WATCHER_SERVICE;
    }
    qInfo() << "kdock: systray using watcher" << m_watcherService
            << "(kde:" << kdeExists << "fdo:" << fdoExists << ")";

    m_hostService = QStringLiteral("org.kde.StatusNotifierHost-%1")
                        .arg(QCoreApplication::applicationPid());
    bus.registerService(m_hostService);
    bus.registerObject(QStringLiteral("/StatusNotifierHost"), this,
                        QDBusConnection::ExportAdaptors);

    QDBusMessage msg = QDBusMessage::createMethodCall(m_watcherService, WATCHER_PATH,
        m_watcherService, QStringLiteral("RegisterStatusNotifierHost"));
    msg.setArguments({m_hostService});
    QDBusPendingReply<> reply = bus.asyncCall(msg);

    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(reply, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, reply]() {
        watcher->deleteLater();
        if (reply.isError()) {
            qWarning() << "kdock: failed to register systray host:" << reply.error().message();
            return;
        }
        m_active = true;
        qInfo() << "kdock: systray host registered successfully";
        connectWatcherSignals();
        // RegisteredStatusNotifierItems is a *property* of the real watcher
        // (our own scriptable method only mattered when we were the watcher),
        // so read it via org.freedesktop.DBus.Properties.Get.
        QDBusMessage listMsg = QDBusMessage::createMethodCall(m_watcherService, WATCHER_PATH,
            QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"));
        listMsg.setArguments({m_watcherService, QStringLiteral("RegisteredStatusNotifierItems")});
        QDBusPendingReply<QDBusVariant> listReply = QDBusConnection::sessionBus().asyncCall(listMsg);
        QDBusPendingCallWatcher *listWatcher = new QDBusPendingCallWatcher(listReply, this);
        connect(listWatcher, &QDBusPendingCallWatcher::finished, this, [this, listWatcher, listReply]() {
            listWatcher->deleteLater();
            if (listReply.isError()) {
                qWarning() << "kdock: failed to get registered systray items:" << listReply.error().message();
                return;
            }
            const QStringList ids = listReply.value().variant().toStringList();
            qInfo() << "kdock: systray initial items count:" << ids.size();
            for (const QString &id : ids) {
                qInfo() << "kdock: systray initial item:" << id;
                QString svc, pth;
                splitItemId(id, svc, pth);
                addItem(svc, pth);
            }
        });
    });
}

void SystrayHost::connectWatcherSignals()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    bool ok1 = bus.connect(m_watcherService, WATCHER_PATH, m_watcherService,
                QStringLiteral("StatusNotifierItemRegistered"),
                this, SLOT(onItemRegistered(QString)));
    bool ok2 = bus.connect(m_watcherService, WATCHER_PATH, m_watcherService,
                QStringLiteral("StatusNotifierItemUnregistered"),
                this, SLOT(onItemUnregistered(QString)));
    if (!ok1)
        qWarning() << "kdock: failed to connect StatusNotifierItemRegistered signal";
    if (!ok2)
        qWarning() << "kdock: failed to connect StatusNotifierItemUnregistered signal";
    qInfo() << "kdock: systray watcher signals connected: registered=" << ok1 << "unregistered=" << ok2;
}

void SystrayHost::onItemRegistered(const QString &id)
{
    qInfo() << "kdock: systray item registered:" << id;
    QString svc, pth;
    splitItemId(id, svc, pth);
    addItem(svc, pth);
}

void SystrayHost::onItemUnregistered(const QString &id)
{
    qInfo() << "kdock: systray item unregistered:" << id;
    QString svc, pth;
    splitItemId(id, svc, pth);
    removeItem(svc);
}


void SystrayHost::RegisterStatusNotifierItem(const QString &serviceOrPath)
{
    QString svc = serviceOrPath;
    QString pth = QStringLiteral("/StatusNotifierItem");
    if (serviceOrPath.startsWith(QLatin1Char('/'))) {
        pth = serviceOrPath;
        if (const QDBusContext *ctx = dynamic_cast<const QDBusContext *>(this))
            svc = message().service();
    }
    if (svc.isEmpty())
        return;
    addItem(svc, pth);
    // Broadcast to other hosts the concatenated "service+path" id, as real
    // watchers do (KDE-style clients expect this form).
    QDBusMessage sig = QDBusMessage::createSignal(WATCHER_PATH, KDE_WATCHER_SERVICE,
        QStringLiteral("StatusNotifierItemRegistered"));
    sig.setArguments({svc + pth});
    QDBusConnection::sessionBus().send(sig);
}

void SystrayHost::RegisterStatusNotifierHost(const QString &service)
{
    QDBusMessage sig = QDBusMessage::createSignal(WATCHER_PATH, KDE_WATCHER_SERVICE,
        QStringLiteral("StatusNotifierHostRegistered"));
    sig.setArguments({service});
    QDBusConnection::sessionBus().send(sig);
}

QStringList SystrayHost::RegisteredStatusNotifierItems() const
{
    QStringList ids;
    for (const SystrayItem *item : m_items)
        ids.append(item->service);
    return ids;
}

bool SystrayHost::IsStatusNotifierHostRegistered() const
{
    return !m_hostService.isEmpty();
}

void SystrayHost::addItem(const QString &service, const QString &path)
{
    QString svc = service;
    QString pth = path;
    if (svc.isEmpty() && pth.startsWith(QLatin1Char('/'))) {
        if (const QDBusContext *ctx = dynamic_cast<const QDBusContext *>(this))
            svc = message().service();
    }
    if (svc.isEmpty()) {
        qWarning() << "kdock: systray addItem rejected: empty service";
        return;
    }
    if (indexOfService(svc) >= 0) {
        qInfo() << "kdock: systray item already known:" << svc;
        return;
    }
    m_watcher.addWatchedService(svc);
    auto *item = new SystrayItem(svc, pth, this);
    connect(item, &SystrayItem::changed, this, [this, item] {
        int idx = m_items.indexOf(item);
        if (idx >= 0) emit itemChanged(idx);
    });
    m_items.append(item);
    qInfo() << "kdock: systray item added:" << svc << "index=" << (m_items.size() - 1);
    emit itemAdded(m_items.size() - 1);
}

void SystrayHost::removeItem(const QString &service)
{
    int idx = indexOfService(service);
    if (idx < 0) {
        qInfo() << "kdock: systray item not found for removal:" << service;
        return;
    }
    m_watcher.removeWatchedService(service);
    m_items.takeAt(idx)->deleteLater();
    qInfo() << "kdock: systray item removed:" << service << "index=" << idx;
    emit itemRemoved(idx);
}

int SystrayHost::indexOfService(const QString &service) const
{
    for (int i = 0; i < m_items.size(); ++i)
        if (m_items.at(i)->service == service)
            return i;
    return -1;
}

void SystrayHost::onServiceUnregistered(const QString &service)
{
    removeItem(service);
}

SystrayItem *SystrayHost::itemAt(int index) const
{
    if (index < 0 || index >= m_items.size()) return nullptr;
    return m_items.at(index);
}

QPixmap SystrayHost::iconPixmapForService(const QString &service) const
{
    const int idx = indexOfService(service);
    return idx < 0 ? QPixmap() : m_items.at(idx)->iconPixmap;
}

QPixmap SystrayHost::menuIconPixmapForService(const QString &service, int itemId) const
{
    const int idx = indexOfService(service);
    if (idx < 0)
        return {};
    DBusMenuClient *menu = m_items.at(idx)->menu();
    return menu ? menu->iconData(itemId) : QPixmap();
}

void SystrayHost::activateItem(int index, int x, int y)
{
    SystrayItem *item = itemAt(index);
    if (item) item->activate(x, y);
}

void SystrayHost::secondaryActivateItem(int index, int x, int y)
{
    SystrayItem *item = itemAt(index);
    if (item) item->secondaryActivate(x, y);
}

void SystrayHost::showMenu(int index, int x, int y)
{
    SystrayItem *item = itemAt(index);
    if (item) item->contextMenu(x, y);
}

void SystrayHost::refreshItem(int index)
{
    SystrayItem *item = itemAt(index);
    if (item) item->refresh();
}
