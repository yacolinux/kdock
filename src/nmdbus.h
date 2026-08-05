// Shared plumbing for the NetworkManager D-Bus API (system bus), used by both
// the dock's network widget (NetworkControl) and the Settings → Redes editor
// (NetworkSettings). No NM library is linked: everything is raw D-Bus.
//
// The two metatypes registered here are mandatory for *writing* connection
// settings: a{sa{sv}} (the whole settings map) and aa{sv} (address-data /
// route-data). Reading works through QDBusArgument's stream operators, but
// QtDBus refuses to marshal an unregistered nested container, so an Update()
// would go out with the wrong signature.

#pragma once

#include <QDBusObjectPath>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVariantMap>

// a{sa{sv}}: setting group ("connection", "ipv4", …) -> key -> value.
using NMSettingsMap = QMap<QString, QVariantMap>;
Q_DECLARE_METATYPE(NMSettingsMap)

namespace NM {

// Service, well-known paths and interface names.
extern const QString Service;
extern const QString Path;
extern const QString Iface;
extern const QString SettingsPath;
extern const QString SettingsIface;
extern const QString ConnIface;
extern const QString ActiveIface;
extern const QString DeviceIface;
extern const QString WirelessIface;
extern const QString WiredIface;
extern const QString ApIface;
extern const QString Ip4ConfigIface;
extern const QString Ip6ConfigIface;
extern const QString PropsIface;

// NM device types (NMDeviceType).
enum DeviceType { DeviceEthernet = 1, DeviceWifi = 2 };
// NM device states (NMDeviceState), coarse values we act on.
enum DeviceState { DeviceUnavailable = 20, DeviceDisconnected = 30, DeviceActivated = 100 };

// Call this once before touching any connection settings.
void registerTypes();

// org.freedesktop.DBus.Properties.GetAll / Get on an NM object.
QVariantMap getAll(const QString &path, const QString &iface);
QVariant getProp(const QString &path, const QString &iface, const QString &prop);

// Unwraps an object-path array property (Devices, AccessPoints, …).
QList<QDBusObjectPath> objectPaths(const QVariant &value);

// Settings.Connection.GetSettings / GetSecrets on a saved connection.
NMSettingsMap connectionSettings(const QString &connPath);
NMSettingsMap connectionSecrets(const QString &connPath, const QString &group);

// SSIDs travel as raw bytes (ay), not as strings.
QString ssidToString(const QByteArray &ssid);
QByteArray ssidToBytes(const QString &ssid);

// Human-readable security label for an access point, from its Flags /
// WpaFlags / RsnFlags triple. Empty for an open network.
QString apSecurityLabel(uint flags, uint wpaFlags, uint rsnFlags);
// Key management the AP needs: "sae" (WPA3), "wpa-psk", "none" (WEP) or an
// empty string for an open network.
QString apKeyMgmt(uint flags, uint wpaFlags, uint rsnFlags);

// Blocking call that returns the error message on failure (empty on success).
QString callMethod(const QString &path, const QString &iface, const QString &method,
                   const QVariantList &args, QVariant *reply = nullptr);

} // namespace NM
