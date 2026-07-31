#include "dbusmenu.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMetaType>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#include <QDateTime>

namespace {

const auto MENU_IFACE = QStringLiteral("com.canonical.dbusmenu");
// Long enough for a busy app to answer, short enough that a hung one does not
// leave the user waiting after a click.
constexpr int kCallTimeoutMs = 2000;

// Menu labels carry GTK/Qt mnemonics ("_Dispositivos…", "&Quit"). QtQuick
// Controls does not interpret either, so the marker would show up literally.
QString stripMnemonic(QString label)
{
    for (const QChar marker : {QLatin1Char('_'), QLatin1Char('&')}) {
        int i = 0;
        while ((i = label.indexOf(marker, i)) != -1) {
            // A doubled marker is an escaped literal one.
            label.remove(i, 1);
            if (i < label.size() && label.at(i) == marker)
                ++i;
        }
    }
    return label;
}

} // namespace

QDBusArgument &operator<<(QDBusArgument &arg, const DBusMenuLayoutItem &item)
{
    arg.beginStructure();
    arg << item.id << item.properties;
    arg.beginArray(qMetaTypeId<QDBusVariant>());
    for (const DBusMenuLayoutItem &child : item.children)
        arg << QDBusVariant(QVariant::fromValue(child));
    arg.endArray();
    arg.endStructure();
    return arg;
}

const QDBusArgument &operator>>(const QDBusArgument &arg, DBusMenuLayoutItem &item)
{
    arg.beginStructure();
    arg >> item.id >> item.properties;
    arg.beginArray();
    while (!arg.atEnd()) {
        // Each child is an `av` element: a variant wrapping the same struct, so
        // the demarshaller recurses through the inner QDBusArgument.
        QDBusVariant wrapper;
        arg >> wrapper;
        const QVariant inner = wrapper.variant();
        if (!inner.canConvert<QDBusArgument>())
            continue;
        DBusMenuLayoutItem child;
        inner.value<QDBusArgument>() >> child;
        item.children.append(child);
    }
    arg.endArray();
    arg.endStructure();
    return arg;
}

DBusMenuClient::DBusMenuClient(const QString &service, const QString &path, QObject *parent)
    : QObject(parent)
    , m_service(service)
    , m_path(path)
{
    // Before any call: see the warning on the operators in the header.
    qDBusRegisterMetaType<DBusMenuLayoutItem>();

    m_iface = new QDBusInterface(service, path, MENU_IFACE,
                                 QDBusConnection::sessionBus(), this);
    m_iface->setTimeout(kCallTimeoutMs);

    // An item may rebuild its menu at any time (blueman rewrites it on every
    // device change). Both signals only say *what* changed; re-reading the whole
    // tree is cheap enough for menus this size.
    QDBusConnection::sessionBus().connect(service, path, MENU_IFACE,
        QStringLiteral("LayoutUpdated"), this, SLOT(onLayoutUpdated()));
    QDBusConnection::sessionBus().connect(service, path, MENU_IFACE,
        QStringLiteral("ItemsPropertiesUpdated"), this, SLOT(onItemsPropertiesUpdated()));
}

DBusMenuClient::~DBusMenuClient()
{
    QDBusConnection::sessionBus().disconnect(m_service, m_path, MENU_IFACE,
        QStringLiteral("LayoutUpdated"), this, SLOT(onLayoutUpdated()));
    QDBusConnection::sessionBus().disconnect(m_service, m_path, MENU_IFACE,
        QStringLiteral("ItemsPropertiesUpdated"), this, SLOT(onItemsPropertiesUpdated()));
}

void DBusMenuClient::requestLayout()
{
    if (!m_iface || !m_iface->isValid()) {
        emit layoutFailed();
        return;
    }
    if (m_pending) // a reply is already on its way; it will emit for both callers
        return;
    m_pending = true;

    // Depth -1 = the whole tree in one round trip. Lazily-filled submenus still
    // need their AboutToShow (see aboutToShow()), but most items answer in full.
    auto *watcher = new QDBusPendingCallWatcher(
        m_iface->asyncCall(QStringLiteral("GetLayout"), 0, -1, QStringList()), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, watcher] {
        watcher->deleteLater();
        m_pending = false;
        QDBusPendingReply<uint, DBusMenuLayoutItem> reply = *watcher;
        if (reply.isError()) {
            m_ready = false;
            emit layoutFailed();
            return;
        }
        m_root = reply.argumentAt<1>();
        ++m_serial;
        m_ready = true;
        emit layoutReady();
    });
}

QVariantList DBusMenuClient::tree() const
{
    return m_ready ? childrenToVariant(m_root) : QVariantList();
}

QVariantList DBusMenuClient::childrenToVariant(const DBusMenuLayoutItem &node) const
{
    QVariantList out;
    for (const DBusMenuLayoutItem &child : node.children) {
        // Hidden entries are dropped rather than passed with visible=false: a
        // QML Menu still reserves layout for an invisible item.
        if (!child.properties.value(QStringLiteral("visible"), true).toBool())
            continue;
        out.append(nodeToVariant(child));
    }
    return out;
}

QVariantMap DBusMenuClient::nodeToVariant(const DBusMenuLayoutItem &node) const
{
    const QVariantMap &p = node.properties;
    const QString toggleType = p.value(QStringLiteral("toggle-type")).toString();

    QVariantMap m;
    m[QStringLiteral("id")] = node.id;
    m[QStringLiteral("label")] = stripMnemonic(p.value(QStringLiteral("label")).toString());
    m[QStringLiteral("separator")] =
        p.value(QStringLiteral("type")).toString() == QStringLiteral("separator");
    m[QStringLiteral("enabled")] = p.value(QStringLiteral("enabled"), true).toBool();
    m[QStringLiteral("submenu")] =
        p.value(QStringLiteral("children-display")).toString() == QStringLiteral("submenu");
    m[QStringLiteral("checkType")] = toggleType;
    // toggle-state is 0/1, or -1 for "indeterminate", which reads as unchecked.
    m[QStringLiteral("checked")] =
        p.value(QStringLiteral("toggle-state")).toInt() == 1;
    m[QStringLiteral("iconName")] = p.value(QStringLiteral("icon-name")).toString();
    m[QStringLiteral("hasIconData")] =
        !p.value(QStringLiteral("icon-data")).toByteArray().isEmpty();
    m[QStringLiteral("serial")] = m_serial;
    m[QStringLiteral("children")] = childrenToVariant(node);
    return m;
}

const DBusMenuLayoutItem *DBusMenuClient::findNode(const DBusMenuLayoutItem &node, int id) const
{
    if (node.id == id)
        return &node;
    for (const DBusMenuLayoutItem &child : node.children) {
        if (const DBusMenuLayoutItem *hit = findNode(child, id))
            return hit;
    }
    return nullptr;
}

QPixmap DBusMenuClient::iconData(int id) const
{
    const DBusMenuLayoutItem *node = m_ready ? findNode(m_root, id) : nullptr;
    if (!node)
        return {};
    const QByteArray data = node->properties.value(QStringLiteral("icon-data")).toByteArray();
    if (data.isEmpty())
        return {};
    QPixmap pm;
    pm.loadFromData(data); // PNG blob; format is sniffed
    return pm;
}

void DBusMenuClient::trigger(int id)
{
    sendEvent(id, QStringLiteral("clicked"));
}

void DBusMenuClient::setOpen(bool open)
{
    sendEvent(0, open ? QStringLiteral("opened") : QStringLiteral("closed"));
}

void DBusMenuClient::sendEvent(int id, const QString &eventId)
{
    if (!m_iface || !m_iface->isValid())
        return;
    // Fire and forget: the item's handler may take as long as it likes (it often
    // opens a window), and nothing here depends on the reply.
    m_iface->asyncCall(QStringLiteral("Event"), id, eventId,
                       QVariant::fromValue(QDBusVariant(QString())),
                       static_cast<uint>(QDateTime::currentSecsSinceEpoch()));
}

void DBusMenuClient::aboutToShow(int id)
{
    if (!m_iface || !m_iface->isValid())
        return;
    auto *watcher = new QDBusPendingCallWatcher(
        m_iface->asyncCall(QStringLiteral("AboutToShow"), id), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher] {
        watcher->deleteLater();
        QDBusPendingReply<bool> reply = *watcher;
        // A true reply means the item just populated something: re-read.
        if (!reply.isError() && reply.value())
            requestLayout();
    });
}

void DBusMenuClient::onLayoutUpdated()
{
    emit layoutInvalidated();
}

void DBusMenuClient::onItemsPropertiesUpdated()
{
    emit layoutInvalidated();
}
