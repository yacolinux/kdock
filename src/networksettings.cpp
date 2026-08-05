#include "networksettings.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusReply>

namespace {

// Reads an aa{sv} property (address-data / route-data) into a list of maps.
QList<QVariantMap> mapList(const QVariant &value)
{
    QList<QVariantMap> out;
    if (value.canConvert<QDBusArgument>()) {
        const QDBusArgument arg = value.value<QDBusArgument>();
        arg >> out;
        return out;
    }
    const QVariantList list = value.toList();
    for (const QVariant &v : list)
        out.append(v.toMap());
    return out;
}

QStringList stringList(const QVariant &value)
{
    if (value.canConvert<QStringList>())
        return value.toStringList();
    QStringList out;
    if (value.canConvert<QDBusArgument>()) {
        const QDBusArgument arg = value.value<QDBusArgument>();
        arg >> out;
    }
    return out;
}

} // namespace

NetworkSettings::NetworkSettings(QObject *parent)
    : QObject(parent)
{
    NM::registerTypes();

    // Any NM change (a new connection, a device that came up, an activation)
    // shows up as a PropertiesChanged somewhere under the NM service.
    QDBusConnection::systemBus().connect(NM::Service, QString(), NM::PropsIface,
                                         QStringLiteral("PropertiesChanged"),
                                         this, SIGNAL(changed()));
    QDBusConnection::systemBus().connect(NM::Service, NM::SettingsPath, NM::SettingsIface,
                                         QStringLiteral("NewConnection"), this, SIGNAL(changed()));
    QDBusConnection::systemBus().connect(NM::Service, NM::SettingsPath, NM::SettingsIface,
                                         QStringLiteral("ConnectionRemoved"), this,
                                         SIGNAL(changed()));
}

QList<NetworkSettings::Device> NetworkSettings::devices() const
{
    QList<Device> out;
    const QList<QDBusObjectPath> paths =
        NM::objectPaths(NM::getProp(NM::Path, NM::Iface, QStringLiteral("Devices")));
    for (const QDBusObjectPath &p : paths) {
        const QVariantMap props = NM::getAll(p.path(), NM::DeviceIface);
        const uint type = props.value(QStringLiteral("DeviceType")).toUInt();
        // Only the two kinds the editor can configure.
        if (type != NM::DeviceEthernet && type != NM::DeviceWifi)
            continue;
        Device d;
        d.path = p.path();
        d.iface = props.value(QStringLiteral("Interface")).toString();
        d.driver = props.value(QStringLiteral("Driver")).toString();
        d.type = type;
        d.state = props.value(QStringLiteral("State")).toUInt();
        d.hwAddress = props.value(QStringLiteral("HwAddress")).toString();
        const QString active =
            props.value(QStringLiteral("ActiveConnection")).value<QDBusObjectPath>().path();
        if (!active.isEmpty() && active != QLatin1String("/"))
            d.activeConnPath = active;
        out.append(d);
    }
    std::sort(out.begin(), out.end(), [](const Device &a, const Device &b) {
        if (a.type != b.type)
            return a.type == NM::DeviceWifi; // Wi-Fi first
        return a.iface < b.iface;
    });
    return out;
}

QList<NetworkSettings::Connection> NetworkSettings::connections() const
{
    QList<Connection> out;

    QMap<QString, QString> activeBySettings;
    {
        const QList<QDBusObjectPath> actives =
            NM::objectPaths(NM::getProp(NM::Path, NM::Iface, QStringLiteral("ActiveConnections")));
        for (const QDBusObjectPath &a : actives) {
            const QString conn =
                NM::getProp(a.path(), NM::ActiveIface, QStringLiteral("Connection"))
                    .value<QDBusObjectPath>().path();
            if (!conn.isEmpty())
                activeBySettings.insert(conn, a.path());
        }
    }

    QDBusMessage call = QDBusMessage::createMethodCall(
        NM::Service, NM::SettingsPath, NM::SettingsIface, QStringLiteral("ListConnections"));
    QDBusReply<QList<QDBusObjectPath>> reply = QDBusConnection::systemBus().call(call);
    if (!reply.isValid())
        return out;

    for (const QDBusObjectPath &connPath : reply.value()) {
        const NMSettingsMap s = NM::connectionSettings(connPath.path());
        const QVariantMap conn = s.value(QStringLiteral("connection"));
        const QString type = conn.value(QStringLiteral("type")).toString();
        // The editor covers Wi-Fi and Ethernet; NM's housekeeping connections
        // (loopback, bridges) are not editable here.
        if (type != QLatin1String("802-11-wireless") && type != QLatin1String("802-3-ethernet"))
            continue;
        Connection c;
        c.path = connPath.path();
        c.id = conn.value(QStringLiteral("id")).toString();
        c.uuid = conn.value(QStringLiteral("uuid")).toString();
        c.type = type;
        c.iface = conn.value(QStringLiteral("interface-name")).toString();
        c.autoconnect = conn.value(QStringLiteral("autoconnect"), true).toBool();
        c.activePath = activeBySettings.value(connPath.path());
        c.active = !c.activePath.isEmpty();
        out.append(c);
    }

    std::sort(out.begin(), out.end(), [](const Connection &a, const Connection &b) {
        if (a.active != b.active)
            return a.active;
        return a.id.localeAwareCompare(b.id) < 0;
    });
    return out;
}

NMSettingsMap NetworkSettings::settings(const QString &connPath) const
{
    return NM::connectionSettings(connPath);
}

NMSettingsMap NetworkSettings::secrets(const QString &connPath, const QString &group) const
{
    return NM::connectionSecrets(connPath, group);
}

QString NetworkSettings::addConnection(const NMSettingsMap &settings, QString *newPath)
{
    QVariant reply;
    const QString error = NM::callMethod(NM::SettingsPath, NM::SettingsIface,
                                         QStringLiteral("AddConnection"),
                                         {QVariant::fromValue(settings)}, &reply);
    if (error.isEmpty() && newPath)
        *newPath = reply.value<QDBusObjectPath>().path();
    return error;
}

QString NetworkSettings::updateConnection(const QString &connPath, const NMSettingsMap &settings)
{
    return NM::callMethod(connPath, NM::ConnIface, QStringLiteral("Update"),
                          {QVariant::fromValue(settings)});
}

QString NetworkSettings::deleteConnection(const QString &connPath)
{
    return NM::callMethod(connPath, NM::ConnIface, QStringLiteral("Delete"), {});
}

QString NetworkSettings::activate(const QString &connPath, const QString &devicePath)
{
    return NM::callMethod(
        NM::Path, NM::Iface, QStringLiteral("ActivateConnection"),
        {QVariant::fromValue(QDBusObjectPath(connPath)),
         QVariant::fromValue(QDBusObjectPath(devicePath.isEmpty() ? QStringLiteral("/")
                                                                  : devicePath)),
         QVariant::fromValue(QDBusObjectPath(QStringLiteral("/")))});
}

QString NetworkSettings::deactivate(const QString &activePath)
{
    return NM::callMethod(NM::Path, NM::Iface, QStringLiteral("DeactivateConnection"),
                          {QVariant::fromValue(QDBusObjectPath(activePath))});
}

QStringList NetworkSettings::deviceIpSummary(const Device &device) const
{
    QStringList lines;
    const QString ip4 = NM::getProp(device.path, NM::DeviceIface, QStringLiteral("Ip4Config"))
                            .value<QDBusObjectPath>().path();
    if (ip4.isEmpty() || ip4 == QLatin1String("/"))
        return lines;

    const QVariantMap props = NM::getAll(ip4, NM::Ip4ConfigIface);
    const QList<QVariantMap> addresses = mapList(props.value(QStringLiteral("AddressData")));
    for (const QVariantMap &a : addresses) {
        lines.append(QObject::tr("Dirección: %1/%2")
                         .arg(a.value(QStringLiteral("address")).toString())
                         .arg(a.value(QStringLiteral("prefix")).toUInt()));
    }
    const QString gateway = props.value(QStringLiteral("Gateway")).toString();
    if (!gateway.isEmpty())
        lines.append(QObject::tr("Puerta de enlace: %1").arg(gateway));

    QStringList servers;
    const QList<QVariantMap> dns = mapList(props.value(QStringLiteral("NameserverData")));
    for (const QVariantMap &d : dns)
        servers.append(d.value(QStringLiteral("address")).toString());
    if (!servers.isEmpty())
        lines.append(QObject::tr("DNS: %1").arg(servers.join(QStringLiteral(", "))));

    return lines;
}

NetworkSettings::IpConfig NetworkSettings::ipFromSettings(const QVariantMap &group)
{
    IpConfig config;
    if (group.isEmpty())
        return config;

    config.method = group.value(QStringLiteral("method"), QStringLiteral("auto")).toString();
    config.gateway = group.value(QStringLiteral("gateway")).toString();
    config.ignoreAutoDns = group.value(QStringLiteral("ignore-auto-dns")).toBool();
    config.ignoreAutoRoutes = group.value(QStringLiteral("ignore-auto-routes")).toBool();
    config.neverDefault = group.value(QStringLiteral("never-default")).toBool();
    config.mayFail = group.value(QStringLiteral("may-fail"), true).toBool();
    config.searchDomains = stringList(group.value(QStringLiteral("dns-search")));
    // NM 1.x exposes both the legacy numeric "dns" (au / aay) and the string
    // form "dns-data"; the string form is the one worth reading.
    config.dns = stringList(group.value(QStringLiteral("dns-data")));

    for (const QVariantMap &a : mapList(group.value(QStringLiteral("address-data")))) {
        IpAddress address;
        address.address = a.value(QStringLiteral("address")).toString();
        address.prefix = int(a.value(QStringLiteral("prefix")).toUInt());
        if (!address.address.isEmpty())
            config.addresses.append(address);
    }
    for (const QVariantMap &r : mapList(group.value(QStringLiteral("route-data")))) {
        IpRoute route;
        route.dest = r.value(QStringLiteral("dest")).toString();
        route.prefix = int(r.value(QStringLiteral("prefix")).toUInt());
        route.nextHop = r.value(QStringLiteral("next-hop")).toString();
        route.metric = r.contains(QStringLiteral("metric"))
                           ? int(r.value(QStringLiteral("metric")).toUInt())
                           : -1;
        if (!route.dest.isEmpty())
            config.routes.append(route);
    }
    return config;
}

QVariantMap NetworkSettings::ipToSettings(const IpConfig &config, bool v6)
{
    QVariantMap group;
    group[QStringLiteral("method")] = config.method;

    QList<QVariantMap> addresses;
    for (const IpAddress &a : config.addresses) {
        if (a.address.isEmpty())
            continue;
        addresses.append({{QStringLiteral("address"), a.address},
                          {QStringLiteral("prefix"), uint(a.prefix)}});
    }
    group[QStringLiteral("address-data")] = QVariant::fromValue(addresses);

    QList<QVariantMap> routes;
    for (const IpRoute &r : config.routes) {
        if (r.dest.isEmpty())
            continue;
        QVariantMap route{{QStringLiteral("dest"), r.dest},
                          {QStringLiteral("prefix"), uint(r.prefix)}};
        if (!r.nextHop.isEmpty())
            route.insert(QStringLiteral("next-hop"), r.nextHop);
        if (r.metric >= 0)
            route.insert(QStringLiteral("metric"), uint(r.metric));
        routes.append(route);
    }
    group[QStringLiteral("route-data")] = QVariant::fromValue(routes);

    if (!config.gateway.isEmpty())
        group[QStringLiteral("gateway")] = config.gateway;
    group[QStringLiteral("dns-data")] = config.dns;
    group[QStringLiteral("dns-search")] = config.searchDomains;
    group[QStringLiteral("ignore-auto-dns")] = config.ignoreAutoDns;
    group[QStringLiteral("ignore-auto-routes")] = config.ignoreAutoRoutes;
    group[QStringLiteral("never-default")] = config.neverDefault;
    group[QStringLiteral("may-fail")] = config.mayFail;

    Q_UNUSED(v6);
    return group;
}

QString NetworkSettings::stateLabel(uint state)
{
    if (state >= 100)
        return QObject::tr("conectado");
    if (state >= 90)
        return QObject::tr("comprobando conectividad");
    if (state >= 40)
        return QObject::tr("conectando");
    if (state >= NM::DeviceDisconnected)
        return QObject::tr("desconectado");
    if (state >= NM::DeviceUnavailable)
        return QObject::tr("no disponible");
    return QObject::tr("sin gestionar");
}

QString NetworkSettings::typeLabel(uint type)
{
    return type == NM::DeviceWifi ? QObject::tr("Wi-Fi") : QObject::tr("Ethernet");
}
