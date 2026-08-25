#include "systray.h"

#include "dbusmenu.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
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

#include <memory>

// The StatusNotifierItem spec is KDE-originated: in practice every watcher,
// host and item uses the org.kde.* bus names (KDE, libappindicator, Qt/GTK
// trays). The org.freedesktop.* names are essentially never implemented, so we
// speak org.kde.* and only register the freedesktop name as an extra alias when
// we have to become the watcher ourselves (bare wlroots sessions).
static const QString KDE_WATCHER_SERVICE = QStringLiteral("org.kde.StatusNotifierWatcher");
static const QString FDO_WATCHER_SERVICE = QStringLiteral("org.freedesktop.StatusNotifierWatcher");
static const QString WATCHER_PATH = QStringLiteral("/StatusNotifierWatcher");
static const QString ITEM_IFACE = QStringLiteral("org.kde.StatusNotifierItem");

// Every call to an item carries this timeout instead of the bus default of 25 s.
// The calls are asynchronous, so this only decides how long a pending reply is
// kept alive for a client that never answers — but "never answers" is the normal
// state of a tray client that is itself blocked, so the default is far too long.
static constexpr int kItemCallTimeoutMs = 4000;

// The properties the dock draws. Only used by the per-key fallback: the normal
// path is a single GetAll.
static const QStringList kItemProperties = {
    QStringLiteral("IconName"),      QStringLiteral("IconThemePath"),
    QStringLiteral("OverlayIconName"), QStringLiteral("Status"),
    QStringLiteral("Category"),      QStringLiteral("Title"),
    QStringLiteral("ItemIsMenu"),    QStringLiteral("Menu"),
    QStringLiteral("ToolTip"),       QStringLiteral("IconPixmap"),
    QStringLiteral("AttentionIconPixmap")};

// Largest usable frame of an SNI pixmap property, converted to a QPixmap.
static QPixmap pixmapFromProperty(const QVariant &value);

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
    // One async GetAll instead of a dozen blocking property reads. See the
    // comment on applyProperties() in the header for why "async" is the whole
    // point and not a refinement.
    if (m_propsPending) {
        m_propsQueued = true;
        return;
    }
    m_propsPending = true;

    QDBusMessage msg = QDBusMessage::createMethodCall(
        service, path, QStringLiteral("org.freedesktop.DBus.Properties"),
        QStringLiteral("GetAll"));
    msg.setArguments({ITEM_IFACE});
    auto *watcher = new QDBusPendingCallWatcher(
        QDBusConnection::sessionBus().asyncCall(msg, kItemCallTimeoutMs), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher] {
        watcher->deleteLater();
        m_propsPending = false;
        const QDBusPendingReply<QVariantMap> reply = *watcher;
        if (reply.isError() || reply.value().isEmpty()) {
            // Not every item implements GetAll (some GTK/appindicator trays
            // answer UnknownMethod, others an empty map); ask key by key instead
            // of giving up, which would leave the icon blank forever.
            requestPropertiesIndividually();
            return;
        }
        applyProperties(reply.value());
        if (m_propsQueued) {
            m_propsQueued = false;
            readProperties();
        }
    });
}

void SystrayItem::requestPropertiesIndividually()
{
    // Still "one round trip in flight" as far as callers are concerned, so a
    // NewIcon arriving now is remembered instead of starting a second batch.
    m_propsPending = true;
    // Shared, not raw: the watchers are children of this item, so an item that
    // goes away mid-flight destroys them without ever running the lambdas.
    auto collected = std::make_shared<QVariantMap>();
    auto left = std::make_shared<int>(kItemProperties.size());
    for (const QString &key : kItemProperties) {
        QDBusMessage msg = QDBusMessage::createMethodCall(
            service, path, QStringLiteral("org.freedesktop.DBus.Properties"),
            QStringLiteral("Get"));
        msg.setArguments({ITEM_IFACE, key});
        auto *watcher = new QDBusPendingCallWatcher(
            QDBusConnection::sessionBus().asyncCall(msg, kItemCallTimeoutMs), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this,
                [this, watcher, key, collected, left] {
            watcher->deleteLater();
            const QDBusPendingReply<QDBusVariant> reply = *watcher;
            if (!reply.isError())
                collected->insert(key, reply.value().variant());
            if (--*left > 0)
                return;
            m_propsPending = false;
            applyProperties(*collected);
            if (m_propsQueued) {
                m_propsQueued = false;
                readProperties();
            }
        });
    }
}

void SystrayItem::applyProperties(const QVariantMap &props)
{
    const auto str = [&props](const char *key) {
        return props.value(QLatin1String(key)).toString();
    };

    iconName = str("IconName");
    iconThemePath = str("IconThemePath");
    overlayIconName = str("OverlayIconName");
    status = str("Status");
    category = str("Category");
    title = str("Title");
    itemIsMenu = props.value(QStringLiteral("ItemIsMenu")).toBool();
    menuPath = props.value(QStringLiteral("Menu")).value<QDBusObjectPath>().path();
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

    // "ToolTip" is the spec's spelling. "Tooltip" is asked for as well because
    // that is the only name this code used to use — which meant the property was
    // never actually found, and the struct branch below never ran. Fixing the
    // name is what first ran it, and it announced itself with a flood of
    // "QDBusArgument: write from a read-only object": the local was not const.
    QVariant tooltipVar = props.value(QStringLiteral("ToolTip"));
    if (!tooltipVar.isValid())
        tooltipVar = props.value(QStringLiteral("Tooltip"));
    if (tooltipVar.metaType() == QMetaType::fromType<QVariantMap>()) {
        const QVariantMap map = tooltipVar.toMap();
        tooltipTitle = map.value(QStringLiteral("title"), map.value(QStringLiteral("Title"))).toString();
        tooltipSub = map.value(QStringLiteral("description"), map.value(QStringLiteral("Description"))).toString();
    } else if (tooltipVar.metaType() == QMetaType::fromType<QDBusArgument>()) {
        // const, and it matters: the non-const beginStructure()/beginArray() are
        // the *writing* overloads, and using them on a reply desyncs the
        // demarshalling until libdbus aborts the whole process.
        const QDBusArgument arg = tooltipVar.value<QDBusArgument>();
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
        tooltipTitle = title;
    }

    if (iconName.isEmpty()) {
        iconPixmap = pixmapFromProperty(props.value(QStringLiteral("IconPixmap")));
        if (iconPixmap.isNull()) // some trays only set the attention icon
            iconPixmap = pixmapFromProperty(props.value(QStringLiteral("AttentionIconPixmap")));
        iconWidth = iconPixmap.width();
        iconHeight = iconPixmap.height();
    } else {
        iconPixmap = QPixmap();
        iconWidth = iconHeight = 0;
    }

    ++iconSerial;
    emit changed();
}

static QPixmap pixmapFromProperty(const QVariant &value)
{
    // a(iiay) = array of (width, height, ARGB32-big-endian), as it came out of
    // the properties reply.
    //
    // Two things here are deliberate. The local **must be const**: the non-const
    // beginArray()/beginStructure() are the *writing* overloads, and using them
    // on a reply desyncs the demarshalling until libdbus aborts the process (it
    // announces itself first with "QDBusArgument: write from a read-only
    // object"). And the type is matched with metaType(), not canConvert(): asking
    // QVariant to convert walks the registered D-Bus converters, which marshals —
    // the same write on a read-only argument.
    KDbusImageVector images;
    if (value.metaType() == QMetaType::fromType<QDBusArgument>()) {
        const QDBusArgument arg = value.value<QDBusArgument>();
        arg >> images;
    } else if (value.metaType() == QMetaType::fromType<KDbusImageVector>()) {
        images = value.value<KDbusImageVector>();
    }

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

void SystrayItem::refresh()
{
    readProperties();
}

void SystrayItem::callItem(const QString &member, const QVariantList &args)
{
    // Fire and forget: the item's handler may take as long as it likes (it often
    // opens a window), and nothing here depends on the reply.
    QDBusMessage msg = QDBusMessage::createMethodCall(service, path, ITEM_IFACE, member);
    msg.setArguments(args);
    QDBusConnection::sessionBus().asyncCall(msg, kItemCallTimeoutMs);
}

void SystrayItem::activate(int x, int y)
{
    // Asynchronous on purpose, and the reply *is* inspected: an item whose
    // Activate is missing or fails is the signal to show its menu instead, which
    // is the only sensible thing a left click can do there. A blocking call
    // would also freeze the dock while the item opens its window.
    QDBusMessage msg = QDBusMessage::createMethodCall(service, path, ITEM_IFACE,
                                                      QStringLiteral("Activate"));
    msg.setArguments({x, y});
    auto *watcher = new QDBusPendingCallWatcher(
        QDBusConnection::sessionBus().asyncCall(msg, kItemCallTimeoutMs), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher] {
        watcher->deleteLater();
        if (QDBusPendingReply<> reply = *watcher; reply.isError())
            emit activateFailed();
    });
}

void SystrayItem::secondaryActivate(int x, int y)
{
    callItem(QStringLiteral("SecondaryActivate"), {x, y});
}

void SystrayItem::contextMenu(int x, int y)
{
    callItem(QStringLiteral("ContextMenu"), {x, y});
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
    // Only worth saying to a *foreign* watcher: when we are the watcher, the
    // bus name goes away with us anyway.
    if (!m_isWatcher && !m_hostService.isEmpty() && !m_watcherService.isEmpty()) {
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

    // Our host name + object first: valid whether we end up a host or the
    // watcher, and needed before either register call.
    m_hostService = QStringLiteral("org.kde.StatusNotifierHost-%1")
                        .arg(QCoreApplication::applicationPid());
    bus.registerService(m_hostService);
    bus.registerObject(QStringLiteral("/StatusNotifierHost"), this,
                        QDBusConnection::ExportAdaptors);

    if (!kdeExists && !fdoExists) {
        // No watcher present (LXQt, bare wlroots): become one ourselves.
        becomeWatcher();
        return;
    }

    // A watcher name is taken — but a name is not an object. On this mixed KF6
    // session kded6 owns org.kde.StatusNotifierWatcher yet serves *nothing* at
    // /StatusNotifierWatcher (its Plasma module never exports the object), which
    // left the tray permanently empty: we registered as a host against a dead
    // watcher and every item registered there too, so the list was always zero.
    // Trust the name only after its object actually answers a property read.
    m_watcherService = kdeExists ? KDE_WATCHER_SERVICE : FDO_WATCHER_SERVICE;
    qInfo() << "kdock: systray probing watcher" << m_watcherService
            << "(kde:" << kdeExists << "fdo:" << fdoExists << ")";
    QDBusMessage probe = QDBusMessage::createMethodCall(m_watcherService, WATCHER_PATH,
        QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"));
    probe.setArguments({m_watcherService, QStringLiteral("RegisteredStatusNotifierItems")});
    QDBusPendingReply<QDBusVariant> reply = bus.asyncCall(probe);
    auto *watcher = new QDBusPendingCallWatcher(reply, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, reply] {
        watcher->deleteLater();
        if (reply.isError()) {
            qWarning() << "kdock: systray watcher" << m_watcherService
                       << "holds the name but serves no object:" << reply.error().message()
                       << "— trying to revive it";
            tryReviveKdedWatcher();
            return;
        }
        // Alive: adopt it as our watcher, and seed from the ids it just gave us
        // instead of a second round trip.
        setupAsHost(reply.value().variant().toStringList());
    });
}

void SystrayHost::tryReviveKdedWatcher()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    // Ask kded6 to load its statusnotifierwatcher module. On this mixed KF6
    // session kded6 reserves org.kde.StatusNotifierWatcher but does not serve the
    // object until the module is loaded — and it keeps the name reserved even when
    // the module is unloaded, so becoming a competing watcher only ever gets us
    // the freedesktop name (a split brain: real clients use the org.kde one). The
    // fix is to revive kded's own watcher and host it.
    QDBusMessage load = QDBusMessage::createMethodCall(
        QStringLiteral("org.kde.kded6"), QStringLiteral("/kded"),
        QStringLiteral("org.kde.kded6"), QStringLiteral("loadModule"));
    load.setArguments({QStringLiteral("statusnotifierwatcher")});
    auto *loadWatcher = new QDBusPendingCallWatcher(bus.asyncCall(load), this);
    connect(loadWatcher, &QDBusPendingCallWatcher::finished, this, [this, loadWatcher] {
        loadWatcher->deleteLater();
        // Re-probe regardless of the loadModule reply: on a session with no kded6
        // (bare wlroots) the call errors and the re-probe just confirms there is
        // still nothing, so we fall through to becoming the watcher ourselves.
        QDBusConnection bus = QDBusConnection::sessionBus();
        QDBusMessage probe = QDBusMessage::createMethodCall(m_watcherService, WATCHER_PATH,
            QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"));
        probe.setArguments({m_watcherService, QStringLiteral("RegisteredStatusNotifierItems")});
        QDBusPendingReply<QDBusVariant> reply = bus.asyncCall(probe);
        auto *probeWatcher = new QDBusPendingCallWatcher(reply, this);
        connect(probeWatcher, &QDBusPendingCallWatcher::finished, this, [this, probeWatcher, reply] {
            probeWatcher->deleteLater();
            if (reply.isError()) {
                qWarning() << "kdock: could not revive" << m_watcherService
                           << "(no kded module to load):" << reply.error().message()
                           << "— becoming the watcher ourselves";
                becomeWatcher();
                return;
            }
            qInfo() << "kdock: revived kded's statusnotifierwatcher; hosting it";
            setupAsHost(reply.value().variant().toStringList());
        });
    });
}

void SystrayHost::becomeWatcher()
{
    if (m_isWatcher)
        return;
    QDBusConnection bus = QDBusConnection::sessionBus();
    // Export the object under BOTH interface names: exporting only one while
    // talking to ourselves through the other is what used to break the whole
    // tray here — see SniWatcherAdaptor.
    new SniWatcherKdeAdaptor(this, &m_watcherObject);
    new SniWatcherFdoAdaptor(this, &m_watcherObject);
    bus.registerObject(WATCHER_PATH, &m_watcherObject, QDBusConnection::ExportAdaptors);
    // Grab both names. KDE may be squatted by a dead kded6 watcher we cannot
    // evict at runtime (it never asked for AllowReplacement); FDO is normally
    // free. Own whichever we can and point m_watcherService at it.
    const bool gotKde = bus.registerService(KDE_WATCHER_SERVICE);
    const bool gotFdo = bus.registerService(FDO_WATCHER_SERVICE);
    m_watcherService = gotKde ? KDE_WATCHER_SERVICE
                       : gotFdo ? FDO_WATCHER_SERVICE
                                : KDE_WATCHER_SERVICE;
    m_isWatcher = true;
    // Registering with ourselves over the bus would be a round trip whose only
    // outcomes are "it worked" and "the tray is dead": nothing to await. Items
    // reach us directly through registerItem(), and there is no initial list —
    // we have just come up.
    m_active = true;
    emit watcherHostRegistered(m_hostService);
    qInfo() << "kdock: systray host registered (we are the watcher) kde:" << gotKde
            << "fdo:" << gotFdo;
    if (!gotKde) {
        qWarning() << "kdock: could NOT take org.kde.StatusNotifierWatcher (held by another "
                      "process, likely a dead kded6 module). Tray clients that prefer that "
                      "name may not appear until it is freed (disable kded6's "
                      "statusnotifierwatcher module and restart the session).";
    }
}

void SystrayHost::setupAsHost(const QStringList &initialIds)
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    m_active = true;
    qInfo() << "kdock: systray adopting watcher" << m_watcherService
            << "with" << initialIds.size() << "initial item(s)";
    // Connect signals before seeding so an item that registers during setup is
    // not missed.
    connectWatcherSignals();
    for (const QString &id : initialIds) {
        QString svc, pth;
        splitItemId(id, svc, pth);
        addItem(svc, pth);
    }
    // Tell the watcher we host, so items know to publish. Best effort: the probe
    // already proved the object is alive, and the signals + seed above are what
    // actually populate us, but a well-behaved watcher wants to hear it.
    QDBusMessage msg = QDBusMessage::createMethodCall(m_watcherService, WATCHER_PATH,
        m_watcherService, QStringLiteral("RegisterStatusNotifierHost"));
    msg.setArguments({m_hostService});
    QDBusPendingReply<> reply = bus.asyncCall(msg);
    auto *watcher = new QDBusPendingCallWatcher(reply, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, reply] {
        watcher->deleteLater();
        if (reply.isError()) {
            // The watcher's object answered the probe but vanished before this
            // call (crash, restart race). Take over ourselves.
            qWarning() << "kdock: RegisterStatusNotifierHost failed:" << reply.error().message()
                       << "— becoming the watcher ourselves";
            becomeWatcher();
        }
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


void SystrayHost::registerItem(const QString &serviceOrPath, const QString &caller)
{
    QString svc = serviceOrPath;
    QString pth = QStringLiteral("/StatusNotifierItem");
    if (serviceOrPath.startsWith(QLatin1Char('/'))) {
        pth = serviceOrPath;
        svc = caller;
    }
    if (svc.isEmpty())
        return;
    addItem(svc, pth);
    // Broadcast the concatenated "service+path" id, as real watchers do, so
    // another host on this session sees the item too.
    emit watcherItemRegistered(svc + pth);
}

void SystrayHost::registerHost(const QString &service)
{
    emit watcherHostRegistered(service);
}

QStringList SystrayHost::registeredItemIds() const
{
    QStringList ids;
    ids.reserve(m_items.size());
    for (const SystrayItem *item : m_items)
        ids.append(item->service + item->path);
    return ids;
}

// ---- the two watcher interfaces -------------------------------------------

SniWatcherAdaptor::SniWatcherAdaptor(SystrayHost *host, QObject *parent)
    : QDBusAbstractAdaptor(parent)
    , m_host(host)
{
    setAutoRelaySignals(false);
    connect(host, &SystrayHost::watcherItemRegistered,
            this, &SniWatcherAdaptor::StatusNotifierItemRegistered);
    connect(host, &SystrayHost::watcherItemUnregistered,
            this, &SniWatcherAdaptor::StatusNotifierItemUnregistered);
    connect(host, &SystrayHost::watcherHostRegistered,
            this, [this](const QString &) { emit StatusNotifierHostRegistered(); });
}

QStringList SniWatcherAdaptor::registeredItems() const
{
    return m_host ? m_host->registeredItemIds() : QStringList();
}

bool SniWatcherAdaptor::hostRegistered() const
{
    return m_host && m_host->active();
}

void SniWatcherAdaptor::RegisterStatusNotifierItem(const QString &serviceOrPath,
                                                   const QDBusMessage &msg)
{
    if (!m_host)
        return;
    // Reply now, work later. The client on the other end usually registers with
    // a *blocking* call and stays inside it until we answer, so anything we do
    // here happens while it cannot answer us back. Building the item is a queued
    // call so the reply for this message is already on the wire by then; the
    // item's own reads are asynchronous too (see SystrayItem), which is what
    // stops the two processes from waiting on each other for the 25 s the bus
    // allows. Measured before the fix: 24 s of frozen dock per start.
    const QString caller = msg.service();
    QMetaObject::invokeMethod(m_host, [host = m_host, serviceOrPath, caller] {
        host->registerItem(serviceOrPath, caller);
    }, Qt::QueuedConnection);
}

void SniWatcherAdaptor::RegisterStatusNotifierHost(const QString &service)
{
    if (m_host)
        m_host->registerHost(service);
}

void SniWatcherAdaptor::UnregisterStatusNotifierHost(const QString &service)
{
    Q_UNUSED(service);
    // We are the only host we know of, and we go away with the process. Exists
    // so that a well-behaved host (including our own destructor) does not get
    // an error back for saying goodbye.
}

void SystrayHost::addItem(const QString &service, const QString &path)
{
    const QString svc = service;
    const QString pth = path;
    // The caller resolves the service now (registerItem() takes it from the
    // D-Bus message): this used to reach for QDBusContext::message() here, which
    // is only valid while a call is being dispatched to *this* object and is
    // not, now that the two watcher interfaces live on adaptors.
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
    SystrayItem *item = m_items.takeAt(idx);
    const QString id = item->service + item->path;
    item->deleteLater();
    qInfo() << "kdock: systray item removed:" << service << "index=" << idx;
    emit itemRemoved(idx);
    // Tell the session the item is gone. Only meaningful while we are the
    // watcher; with a foreign one this is its job and the signal goes nowhere,
    // because nothing is connected to the adaptors that were never created.
    if (m_isWatcher)
        emit watcherItemUnregistered(id);
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
