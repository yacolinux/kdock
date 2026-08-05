#include "nmdbus.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusVariant>

namespace NM {

const QString Service = QStringLiteral("org.freedesktop.NetworkManager");
const QString Path = QStringLiteral("/org/freedesktop/NetworkManager");
const QString Iface = QStringLiteral("org.freedesktop.NetworkManager");
const QString SettingsPath = QStringLiteral("/org/freedesktop/NetworkManager/Settings");
const QString SettingsIface = QStringLiteral("org.freedesktop.NetworkManager.Settings");
const QString ConnIface = QStringLiteral("org.freedesktop.NetworkManager.Settings.Connection");
const QString ActiveIface = QStringLiteral("org.freedesktop.NetworkManager.Connection.Active");
const QString DeviceIface = QStringLiteral("org.freedesktop.NetworkManager.Device");
const QString WirelessIface = QStringLiteral("org.freedesktop.NetworkManager.Device.Wireless");
const QString WiredIface = QStringLiteral("org.freedesktop.NetworkManager.Device.Wired");
const QString ApIface = QStringLiteral("org.freedesktop.NetworkManager.AccessPoint");
const QString Ip4ConfigIface = QStringLiteral("org.freedesktop.NetworkManager.IP4Config");
const QString Ip6ConfigIface = QStringLiteral("org.freedesktop.NetworkManager.IP6Config");
const QString PropsIface = QStringLiteral("org.freedesktop.DBus.Properties");

namespace {
// NM80211ApFlags / NM80211ApSecurityFlags.
constexpr uint ApFlagPrivacy = 0x1;
constexpr uint SecKeyMgmtPsk = 0x100;
constexpr uint SecKeyMgmt8021X = 0x200;
constexpr uint SecKeyMgmtSae = 0x400;
constexpr uint SecKeyMgmtOwe = 0x800;
} // namespace

void registerTypes()
{
    static bool done = false;
    if (done)
        return;
    done = true;
    qDBusRegisterMetaType<NMSettingsMap>();
    // aa{sv}: address-data / route-data.
    qDBusRegisterMetaType<QList<QVariantMap>>();
}

QVariantMap getAll(const QString &path, const QString &iface)
{
    QDBusMessage call = QDBusMessage::createMethodCall(Service, path, PropsIface,
                                                       QStringLiteral("GetAll"));
    call.setArguments({iface});
    const QDBusMessage reply = QDBusConnection::systemBus().call(call);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
        return {};
    QVariantMap map;
    // Bound to a const reference: the non-const overloads of QDBusArgument's
    // begin*() are the *writing* ones (see the trap in CLAUDE.md).
    const QDBusArgument arg = reply.arguments().constFirst().value<QDBusArgument>();
    arg >> map;
    return map;
}

QVariant getProp(const QString &path, const QString &iface, const QString &prop)
{
    QDBusMessage call = QDBusMessage::createMethodCall(Service, path, PropsIface,
                                                       QStringLiteral("Get"));
    call.setArguments({iface, prop});
    const QDBusMessage reply = QDBusConnection::systemBus().call(call);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
        return {};
    return reply.arguments().constFirst().value<QDBusVariant>().variant();
}

QList<QDBusObjectPath> objectPaths(const QVariant &value)
{
    QList<QDBusObjectPath> paths;
    if (value.canConvert<QList<QDBusObjectPath>>())
        return value.value<QList<QDBusObjectPath>>();
    if (value.canConvert<QDBusArgument>()) {
        const QDBusArgument arg = value.value<QDBusArgument>();
        arg >> paths;
    }
    return paths;
}

NMSettingsMap connectionSettings(const QString &connPath)
{
    QDBusMessage call = QDBusMessage::createMethodCall(Service, connPath, ConnIface,
                                                       QStringLiteral("GetSettings"));
    const QDBusMessage reply = QDBusConnection::systemBus().call(call);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
        return {};
    NMSettingsMap settings;
    const QDBusArgument arg = reply.arguments().constFirst().value<QDBusArgument>();
    arg >> settings;
    return settings;
}

NMSettingsMap connectionSecrets(const QString &connPath, const QString &group)
{
    QDBusMessage call = QDBusMessage::createMethodCall(Service, connPath, ConnIface,
                                                       QStringLiteral("GetSecrets"));
    call.setArguments({group});
    const QDBusMessage reply = QDBusConnection::systemBus().call(call);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
        return {};
    NMSettingsMap secrets;
    const QDBusArgument arg = reply.arguments().constFirst().value<QDBusArgument>();
    arg >> secrets;
    return secrets;
}

QString ssidToString(const QByteArray &ssid)
{
    return QString::fromUtf8(ssid);
}

QByteArray ssidToBytes(const QString &ssid)
{
    return ssid.toUtf8();
}

QString apKeyMgmt(uint flags, uint wpaFlags, uint rsnFlags)
{
    // A transition-mode AP advertises both PSK and SAE; PSK is chosen because
    // every client understands it.
    if ((rsnFlags | wpaFlags) & SecKeyMgmtPsk)
        return QStringLiteral("wpa-psk");
    if (rsnFlags & SecKeyMgmtSae)
        return QStringLiteral("sae");
    if ((rsnFlags | wpaFlags) & SecKeyMgmt8021X)
        return QStringLiteral("wpa-eap"); // not offered by the editor
    if ((rsnFlags | wpaFlags) & SecKeyMgmtOwe)
        return QStringLiteral("owe");
    if ((flags & ApFlagPrivacy) && wpaFlags == 0 && rsnFlags == 0)
        return QStringLiteral("none"); // WEP
    return {};
}

QString apSecurityLabel(uint flags, uint wpaFlags, uint rsnFlags)
{
    const bool psk = (rsnFlags | wpaFlags) & SecKeyMgmtPsk;
    const bool sae = rsnFlags & SecKeyMgmtSae;
    if (psk && sae)
        return QStringLiteral("WPA2/WPA3");
    if (sae)
        return QStringLiteral("WPA3");
    if (rsnFlags & SecKeyMgmtPsk)
        return QStringLiteral("WPA2");
    if (wpaFlags & SecKeyMgmtPsk)
        return QStringLiteral("WPA");
    if ((rsnFlags | wpaFlags) & SecKeyMgmt8021X)
        return QStringLiteral("802.1X");
    if ((rsnFlags | wpaFlags) & SecKeyMgmtOwe)
        return QStringLiteral("OWE");
    if ((flags & ApFlagPrivacy) && wpaFlags == 0 && rsnFlags == 0)
        return QStringLiteral("WEP");
    return {};
}

QString callMethod(const QString &path, const QString &iface, const QString &method,
                   const QVariantList &args, QVariant *reply)
{
    QDBusMessage call = QDBusMessage::createMethodCall(Service, path, iface, method);
    call.setArguments(args);
    const QDBusMessage answer = QDBusConnection::systemBus().call(call);
    if (answer.type() == QDBusMessage::ErrorMessage)
        return answer.errorMessage().isEmpty() ? answer.errorName() : answer.errorMessage();
    if (reply && !answer.arguments().isEmpty())
        *reply = answer.arguments().constFirst();
    return {};
}

} // namespace NM
