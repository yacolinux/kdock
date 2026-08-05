// The right-hand pane of the Redes tab: the form that edits one NetworkManager
// connection. Pages follow KDE's network KCM — General, Wi-Fi, Wi-Fi security,
// Ethernet, IPv4, IPv6 — and map straight onto NM setting groups (see
// networksettings.h for the conversion of the two IP groups).
//
// WPA/WPA2/WPA3-Personal, WEP and open networks are covered; 802.1X (Enterprise)
// is deliberately out of scope, and a connection that uses it is loaded
// read-only so saving cannot silently drop its 802-1x group.

#pragma once

#include "networksettings.h"

#include <QWidget>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTabWidget;
class QTableWidget;

// One IP family's page. Two instances live in the editor (v4 and v6).
class IpConfigPage : public QWidget
{
    Q_OBJECT
public:
    explicit IpConfigPage(bool v6, QWidget *parent = nullptr);

    void setConfig(const NetworkSettings::IpConfig &config);
    NetworkSettings::IpConfig config() const;

private:
    void addAddressRow(const NetworkSettings::IpAddress &address);
    void updateEnabled();

    bool m_v6;
    QComboBox *m_method;
    QTableWidget *m_addresses;
    QPushButton *m_addAddress;
    QPushButton *m_removeAddress;
    QLineEdit *m_gateway;
    QLineEdit *m_dns;
    QLineEdit *m_search;
    QCheckBox *m_ignoreAutoDns;
    QCheckBox *m_required;
    QPushButton *m_routesButton;
    // Held between the routes dialog and save.
    QList<NetworkSettings::IpRoute> m_routes;
    bool m_ignoreAutoRoutes = false;
    bool m_neverDefault = false;
};

class ConnectionEditor : public QWidget
{
    Q_OBJECT
public:
    explicit ConnectionEditor(QWidget *parent = nullptr);

    // Feed the form. `secrets` carries the Wi-Fi psk when there is one, and
    // `devices` fills the interface combo.
    void load(const NMSettingsMap &settings, const NMSettingsMap &secrets,
              const QList<NetworkSettings::Device> &devices, bool isNew);
    // The settings map to hand to Update()/AddConnection(). Groups the editor
    // does not know about are preserved from what load() was given.
    NMSettingsMap collect() const;

    bool isDirty() const { return m_dirty; }
    void setDirty(bool dirty);

signals:
    void applyRequested();
    void discardRequested();
    void dirtyChanged(bool dirty);

private:
    void markDirty();
    void rebuildSecurityFields();
    // Arms Aplicar/Descartar on every edit inside one page. Called per page
    // because the pages are built parentless (they only get a parent when
    // load() puts them in the tab widget), so a single findChildren() sweep
    // over the editor would miss all of them — and then editing an IP address
    // would leave Aplicar greyed out.
    void watchPage(QWidget *root);

    QTabWidget *m_tabs;
    // General
    QLineEdit *m_id;
    QCheckBox *m_autoconnect;
    QSpinBox *m_priority;
    QComboBox *m_interface;
    QCheckBox *m_allUsers;
    // Wi-Fi
    QWidget *m_wifiPage;
    QLineEdit *m_ssid;
    QComboBox *m_wifiMode;
    QLineEdit *m_bssid;
    QSpinBox *m_wifiMtu;
    // Wi-Fi security
    QWidget *m_securityPage;
    QComboBox *m_security;
    QLineEdit *m_password;
    QCheckBox *m_showPassword;
    // Ethernet
    QWidget *m_ethernetPage;
    QLineEdit *m_clonedMac;
    QSpinBox *m_ethernetMtu;
    // IP
    IpConfigPage *m_ipv4;
    IpConfigPage *m_ipv6;

    QPushButton *m_apply;
    QPushButton *m_discard;

    NMSettingsMap m_original;
    QString m_type;
    bool m_dirty = false;
    // 802.1X connections are shown but not saved (see the header comment).
    bool m_readOnly = false;
};
