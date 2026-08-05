#include "connectioneditor.h"

#include "routesdialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {
// key-mgmt values, in the order the security combo lists them.
const char *kSecNone = "";
const char *kSecWpaPsk = "wpa-psk";
const char *kSecSae = "sae";
const char *kSecWep = "none";
} // namespace

// ---------------------------------------------------------------------------

IpConfigPage::IpConfigPage(bool v6, QWidget *parent)
    : QWidget(parent)
    , m_v6(v6)
{
    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    // One form for the whole page: two QFormLayouts stacked in the same column
    // size their label columns independently and the result looks ragged.
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    m_method = new QComboBox(this);
    if (v6) {
        m_method->addItem(tr("Automático"), QStringLiteral("auto"));
        m_method->addItem(tr("Automático (solo direcciones)"), QStringLiteral("auto"));
        m_method->addItem(tr("DHCP"), QStringLiteral("dhcp"));
        m_method->addItem(tr("Manual"), QStringLiteral("manual"));
        m_method->addItem(tr("Solo enlace local"), QStringLiteral("link-local"));
        m_method->addItem(tr("Ignorar"), QStringLiteral("ignore"));
    } else {
        m_method->addItem(tr("Automático (DHCP)"), QStringLiteral("auto"));
        m_method->addItem(tr("Automático (solo direcciones)"), QStringLiteral("auto"));
        m_method->addItem(tr("Manual"), QStringLiteral("manual"));
        m_method->addItem(tr("Solo enlace local"), QStringLiteral("link-local"));
        m_method->addItem(tr("Compartida con otros equipos"), QStringLiteral("shared"));
        m_method->addItem(tr("Desactivada"), QStringLiteral("disabled"));
    }
    form->addRow(tr("Método:"), m_method);

    auto *addressBox = new QGroupBox(tr("Direcciones"), this);
    auto *addressLayout = new QVBoxLayout(addressBox);
    m_addresses = new QTableWidget(0, 2, addressBox);
    m_addresses->setHorizontalHeaderLabels({tr("Dirección"), tr("Prefijo")});
    m_addresses->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_addresses->verticalHeader()->setVisible(false);
    m_addresses->setMaximumHeight(130);
    addressLayout->addWidget(m_addresses);

    auto *addressButtons = new QHBoxLayout;
    m_addAddress = new QPushButton(QIcon::fromTheme(QStringLiteral("list-add")), tr("Agregar"),
                                   addressBox);
    m_removeAddress = new QPushButton(QIcon::fromTheme(QStringLiteral("list-remove")),
                                      tr("Quitar"), addressBox);
    addressButtons->addWidget(m_addAddress);
    addressButtons->addWidget(m_removeAddress);
    addressButtons->addStretch();
    addressLayout->addLayout(addressButtons);
    form->addRow(addressBox);

    connect(m_addAddress, &QPushButton::clicked, this, [this, v6] {
        NetworkSettings::IpAddress address;
        address.prefix = v6 ? 64 : 24;
        addAddressRow(address);
    });
    connect(m_removeAddress, &QPushButton::clicked, this, [this] {
        const int row = m_addresses->currentRow();
        if (row >= 0)
            m_addresses->removeRow(row);
    });

    m_gateway = new QLineEdit(this);
    m_gateway->setPlaceholderText(v6 ? QStringLiteral("fe80::1") : QStringLiteral("192.168.0.1"));
    form->addRow(tr("Puerta de enlace:"), m_gateway);

    m_dns = new QLineEdit(this);
    m_dns->setPlaceholderText(v6 ? QStringLiteral("2606:4700:4700::1111, …")
                                 : QStringLiteral("8.8.8.8, 1.1.1.1"));
    form->addRow(tr("Servidores DNS:"), m_dns);

    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(tr("casa.lan, ejemplo.com"));
    form->addRow(tr("Dominios de búsqueda:"), m_search);

    m_ignoreAutoDns = new QCheckBox(tr("Ignorar los DNS obtenidos automáticamente"), this);
    form->addRow(QString(), m_ignoreAutoDns);

    m_required = new QCheckBox(tr("Requerir esta configuración IP"),
                               this);
    form->addRow(QString(), m_required);

    m_routesButton = new QPushButton(tr("Rutas…"), this);
    form->addRow(QString(), m_routesButton);
    layout->addLayout(form);
    layout->addStretch(1);

    connect(m_routesButton, &QPushButton::clicked, this, [this] {
        NetworkSettings::IpConfig current = config();
        RoutesDialog dialog(current, m_v6, this);
        if (dialog.exec() != QDialog::Accepted)
            return;
        m_routes = dialog.routes();
        m_ignoreAutoRoutes = dialog.ignoreAutoRoutes();
        m_neverDefault = dialog.neverDefault();
        updateEnabled();
    });
    connect(m_method, &QComboBox::currentIndexChanged, this, [this] { updateEnabled(); });
}

void IpConfigPage::addAddressRow(const NetworkSettings::IpAddress &address)
{
    const int row = m_addresses->rowCount();
    m_addresses->insertRow(row);
    m_addresses->setItem(row, 0, new QTableWidgetItem(address.address));
    m_addresses->setItem(row, 1, new QTableWidgetItem(QString::number(address.prefix)));
}

void IpConfigPage::updateEnabled()
{
    const QString method = m_method->currentData().toString();
    const bool manual = method == QLatin1String("manual");
    const bool off = method == QLatin1String("ignore") || method == QLatin1String("disabled");

    m_addresses->setEnabled(manual);
    m_addAddress->setEnabled(manual);
    m_removeAddress->setEnabled(manual);
    // NM drops ipv4.gateway whenever never-default is on, so the field says so
    // instead of losing what the user typed on save.
    m_gateway->setEnabled(manual && !m_neverDefault);
    m_gateway->setToolTip(m_neverDefault
                              ? tr("Desactivada porque la conexión se usa solo para los "
                                   "recursos de su red (ver Rutas…).")
                              : QString());
    m_dns->setEnabled(!off);
    m_search->setEnabled(!off);
    m_ignoreAutoDns->setEnabled(!off && !manual);
    m_routesButton->setEnabled(!off);
}

void IpConfigPage::setConfig(const NetworkSettings::IpConfig &config)
{
    // "Automatic (addresses only)" is auto + ignore-auto-dns; it shares the
    // "auto" method value with the plain automatic entry.
    int index = m_method->findData(config.method);
    if (config.method == QLatin1String("auto") && config.ignoreAutoDns)
        index = 1;
    m_method->setCurrentIndex(index >= 0 ? index : 0);

    m_addresses->setRowCount(0);
    for (const NetworkSettings::IpAddress &address : config.addresses)
        addAddressRow(address);

    m_gateway->setText(config.gateway);
    m_dns->setText(config.dns.join(QStringLiteral(", ")));
    m_search->setText(config.searchDomains.join(QStringLiteral(", ")));
    m_ignoreAutoDns->setChecked(config.ignoreAutoDns);
    m_required->setChecked(!config.mayFail);
    m_routes = config.routes;
    m_ignoreAutoRoutes = config.ignoreAutoRoutes;
    m_neverDefault = config.neverDefault;
    updateEnabled();
}

NetworkSettings::IpConfig IpConfigPage::config() const
{
    NetworkSettings::IpConfig config;
    config.method = m_method->currentData().toString();

    for (int row = 0; row < m_addresses->rowCount(); ++row) {
        const QTableWidgetItem *addressItem = m_addresses->item(row, 0);
        const QTableWidgetItem *prefixItem = m_addresses->item(row, 1);
        if (!addressItem || addressItem->text().trimmed().isEmpty())
            continue;
        NetworkSettings::IpAddress address;
        address.address = addressItem->text().trimmed();
        bool ok = false;
        const int prefix = prefixItem ? prefixItem->text().toInt(&ok) : 0;
        address.prefix = ok ? prefix : (m_v6 ? 64 : 24);
        config.addresses.append(address);
    }

    config.gateway = m_gateway->text().trimmed();
    const auto split = [](const QString &text) {
        QStringList out;
        const QStringList parts = text.split(QRegularExpression(QStringLiteral("[,\\s]+")),
                                             Qt::SkipEmptyParts);
        for (const QString &part : parts)
            out.append(part.trimmed());
        return out;
    };
    config.dns = split(m_dns->text());
    config.searchDomains = split(m_search->text());
    // The second combo entry is "automatic, addresses only".
    config.ignoreAutoDns = m_ignoreAutoDns->isChecked() || m_method->currentIndex() == 1;
    config.mayFail = !m_required->isChecked();
    config.routes = m_routes;
    config.ignoreAutoRoutes = m_ignoreAutoRoutes;
    config.neverDefault = m_neverDefault;
    return config;
}

// ---------------------------------------------------------------------------

ConnectionEditor::ConnectionEditor(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    m_tabs = new QTabWidget(this);
    layout->addWidget(m_tabs);

    // --- General ---
    auto *general = new QWidget;
    auto *generalForm = new QFormLayout(general);
    m_id = new QLineEdit(general);
    generalForm->addRow(tr("Nombre:"), m_id);
    m_autoconnect = new QCheckBox(tr("Conectar automáticamente cuando esté disponible"), general);
    generalForm->addRow(QString(), m_autoconnect);
    m_priority = new QSpinBox(general);
    m_priority->setRange(-999, 999);
    m_priority->setToolTip(tr("Entre varias conexiones disponibles gana la de mayor prioridad."));
    generalForm->addRow(tr("Prioridad:"), m_priority);
    m_interface = new QComboBox(general);
    generalForm->addRow(tr("Interfaz:"), m_interface);
    m_allUsers = new QCheckBox(tr("Todos los usuarios pueden usar esta conexión"), general);
    generalForm->addRow(QString(), m_allUsers);
    m_tabs->addTab(general, tr("General"));

    // --- Wi-Fi ---
    m_wifiPage = new QWidget;
    auto *wifiForm = new QFormLayout(m_wifiPage);
    m_ssid = new QLineEdit(m_wifiPage);
    wifiForm->addRow(tr("SSID:"), m_ssid);
    m_wifiMode = new QComboBox(m_wifiPage);
    m_wifiMode->addItem(tr("Cliente (infraestructura)"), QStringLiteral("infrastructure"));
    m_wifiMode->addItem(tr("Punto de acceso"), QStringLiteral("ap"));
    m_wifiMode->addItem(tr("Ad-hoc"), QStringLiteral("adhoc"));
    wifiForm->addRow(tr("Modo:"), m_wifiMode);
    m_bssid = new QLineEdit(m_wifiPage);
    m_bssid->setPlaceholderText(tr("Opcional: fijar un punto de acceso concreto"));
    wifiForm->addRow(tr("BSSID:"), m_bssid);
    m_wifiMtu = new QSpinBox(m_wifiPage);
    m_wifiMtu->setRange(0, 10000);
    m_wifiMtu->setSpecialValueText(tr("automático"));
    wifiForm->addRow(tr("MTU:"), m_wifiMtu);

    // --- Wi-Fi security ---
    m_securityPage = new QWidget;
    auto *securityForm = new QFormLayout(m_securityPage);
    m_security = new QComboBox(m_securityPage);
    m_security->addItem(tr("Ninguna"), QLatin1String(kSecNone));
    m_security->addItem(tr("WPA/WPA2 Personal"), QLatin1String(kSecWpaPsk));
    m_security->addItem(tr("WPA3 Personal"), QLatin1String(kSecSae));
    m_security->addItem(tr("WEP (heredado)"), QLatin1String(kSecWep));
    securityForm->addRow(tr("Seguridad:"), m_security);
    m_password = new QLineEdit(m_securityPage);
    m_password->setEchoMode(QLineEdit::Password);
    securityForm->addRow(tr("Contraseña:"), m_password);
    m_showPassword = new QCheckBox(tr("Mostrar contraseña"), m_securityPage);
    securityForm->addRow(QString(), m_showPassword);
    connect(m_showPassword, &QCheckBox::toggled, this, [this](bool on) {
        m_password->setEchoMode(on ? QLineEdit::Normal : QLineEdit::Password);
    });
    connect(m_security, &QComboBox::currentIndexChanged, this,
            [this] { rebuildSecurityFields(); });

    // --- Ethernet ---
    m_ethernetPage = new QWidget;
    auto *ethernetForm = new QFormLayout(m_ethernetPage);
    m_clonedMac = new QLineEdit(m_ethernetPage);
    m_clonedMac->setPlaceholderText(tr("Opcional: MAC clonada (o random / stable)"));
    ethernetForm->addRow(tr("MAC clonada:"), m_clonedMac);
    m_ethernetMtu = new QSpinBox(m_ethernetPage);
    m_ethernetMtu->setRange(0, 10000);
    m_ethernetMtu->setSpecialValueText(tr("automático"));
    ethernetForm->addRow(tr("MTU:"), m_ethernetMtu);

    // --- IP ---
    m_ipv4 = new IpConfigPage(false);
    m_ipv6 = new IpConfigPage(true);

    auto *buttons = new QHBoxLayout;
    buttons->addStretch();
    m_discard = new QPushButton(QIcon::fromTheme(QStringLiteral("document-revert")),
                                tr("Descartar"), this);
    m_apply = new QPushButton(QIcon::fromTheme(QStringLiteral("dialog-ok-apply")), tr("Aplicar"),
                              this);
    buttons->addWidget(m_discard);
    buttons->addWidget(m_apply);
    layout->addLayout(buttons);

    connect(m_apply, &QPushButton::clicked, this, &ConnectionEditor::applyRequested);
    connect(m_discard, &QPushButton::clicked, this, &ConnectionEditor::discardRequested);

    for (QWidget *widget : {general, m_wifiPage, m_securityPage, m_ethernetPage,
                            static_cast<QWidget *>(m_ipv4), static_cast<QWidget *>(m_ipv6)})
        watchPage(widget);

    setDirty(false);
}

void ConnectionEditor::watchPage(QWidget *root)
{
    for (QWidget *widget : root->findChildren<QWidget *>()) {
        // textEdited (not textChanged): load() fills the fields itself and that
        // must not count as an edit.
        if (auto *edit = qobject_cast<QLineEdit *>(widget))
            connect(edit, &QLineEdit::textEdited, this, [this] { markDirty(); });
        else if (auto *check = qobject_cast<QCheckBox *>(widget))
            connect(check, &QCheckBox::toggled, this, [this] { markDirty(); });
        else if (auto *combo = qobject_cast<QComboBox *>(widget))
            connect(combo, &QComboBox::currentIndexChanged, this, [this] { markDirty(); });
        else if (auto *spin = qobject_cast<QSpinBox *>(widget))
            connect(spin, &QSpinBox::valueChanged, this, [this] { markDirty(); });
        else if (auto *table = qobject_cast<QTableWidget *>(widget))
            connect(table, &QTableWidget::cellChanged, this, [this] { markDirty(); });
    }
    // The routes dialog changes state the page holds outside any widget.
    for (QPushButton *button : root->findChildren<QPushButton *>()) {
        if (button->text().startsWith(QLatin1String("Rutas")))
            connect(button, &QPushButton::clicked, this, [this] { markDirty(); });
    }
}

void ConnectionEditor::markDirty()
{
    if (!m_dirty)
        setDirty(true);
}

void ConnectionEditor::setDirty(bool dirty)
{
    m_dirty = dirty;
    m_apply->setEnabled(dirty && !m_readOnly);
    m_discard->setEnabled(dirty);
    emit dirtyChanged(dirty);
}

void ConnectionEditor::rebuildSecurityFields()
{
    const QString keyMgmt = m_security->currentData().toString();
    const bool secured = !keyMgmt.isEmpty();
    m_password->setEnabled(secured);
    m_showPassword->setEnabled(secured);
}

void ConnectionEditor::load(const NMSettingsMap &settings, const NMSettingsMap &secrets,
                            const QList<NetworkSettings::Device> &devices, bool isNew)
{
    m_original = settings;
    const QVariantMap connection = settings.value(QStringLiteral("connection"));
    m_type = connection.value(QStringLiteral("type")).toString();
    const bool wifi = m_type == QLatin1String("802-11-wireless");
    m_readOnly = settings.contains(QStringLiteral("802-1x"));

    // Rebuild the page set for this connection's type.
    while (m_tabs->count() > 1)
        m_tabs->removeTab(1);
    // Never setVisible() a page by hand: inside the tab widget's stack that
    // paints it on top of the current one (two forms overlapping letter by
    // letter, which reads as a font bug). addTab() handles visibility.
    if (wifi) {
        m_tabs->addTab(m_wifiPage, tr("Wi-Fi"));
        m_tabs->addTab(m_securityPage, tr("Seguridad"));
    } else {
        m_tabs->addTab(m_ethernetPage, tr("Ethernet"));
    }
    m_tabs->addTab(m_ipv4, tr("IPv4"));
    m_tabs->addTab(m_ipv6, tr("IPv6"));

    m_id->setText(connection.value(QStringLiteral("id")).toString());
    m_autoconnect->setChecked(connection.value(QStringLiteral("autoconnect"), true).toBool());
    m_priority->setValue(connection.value(QStringLiteral("autoconnect-priority")).toInt());
    m_allUsers->setChecked(
        connection.value(QStringLiteral("permissions")).toStringList().isEmpty());

    m_interface->clear();
    m_interface->addItem(tr("Cualquiera del tipo adecuado"), QString());
    for (const NetworkSettings::Device &device : devices) {
        if (device.isWifi() != wifi)
            continue;
        m_interface->addItem(QStringLiteral("%1 (%2)").arg(device.iface, device.driver),
                             device.iface);
    }
    const QString iface = connection.value(QStringLiteral("interface-name")).toString();
    const int ifaceIndex = m_interface->findData(iface);
    m_interface->setCurrentIndex(ifaceIndex >= 0 ? ifaceIndex : 0);

    if (wifi) {
        const QVariantMap wireless = settings.value(QStringLiteral("802-11-wireless"));
        m_ssid->setText(NM::ssidToString(wireless.value(QStringLiteral("ssid")).toByteArray()));
        const int modeIndex =
            m_wifiMode->findData(wireless.value(QStringLiteral("mode"),
                                                QStringLiteral("infrastructure")));
        m_wifiMode->setCurrentIndex(modeIndex >= 0 ? modeIndex : 0);
        m_bssid->setText(wireless.value(QStringLiteral("bssid")).toString());
        m_wifiMtu->setValue(int(wireless.value(QStringLiteral("mtu")).toUInt()));

        const QVariantMap security = settings.value(QStringLiteral("802-11-wireless-security"));
        const QString keyMgmt = security.value(QStringLiteral("key-mgmt")).toString();
        int securityIndex = m_security->findData(keyMgmt);
        if (securityIndex < 0)
            securityIndex = 0;
        m_security->setCurrentIndex(securityIndex);

        const QVariantMap storedSecrets =
            secrets.value(QStringLiteral("802-11-wireless-security"));
        m_password->setText(keyMgmt == QLatin1String(kSecWep)
                                ? storedSecrets.value(QStringLiteral("wep-key0")).toString()
                                : storedSecrets.value(QStringLiteral("psk")).toString());
        rebuildSecurityFields();
    } else {
        const QVariantMap wired = settings.value(QStringLiteral("802-3-ethernet"));
        m_clonedMac->setText(wired.value(QStringLiteral("cloned-mac-address")).toString());
        m_ethernetMtu->setValue(int(wired.value(QStringLiteral("mtu")).toUInt()));
    }

    m_ipv4->setConfig(NetworkSettings::ipFromSettings(settings.value(QStringLiteral("ipv4"))));
    m_ipv6->setConfig(NetworkSettings::ipFromSettings(settings.value(QStringLiteral("ipv6"))));

    m_tabs->setCurrentIndex(0);
    setDirty(isNew);
    if (m_readOnly) {
        m_apply->setEnabled(false);
        m_apply->setToolTip(tr("Esta conexión usa WPA-Enterprise (802.1X), que este editor no "
                               "sabe editar. Se muestra pero no se guarda."));
    } else {
        m_apply->setToolTip(QString());
    }
}

NMSettingsMap ConnectionEditor::collect() const
{
    // Start from what was loaded so groups this editor does not model (proxy,
    // 802-1x, per-connection metered flags…) survive a save.
    NMSettingsMap settings = m_original;

    QVariantMap connection = settings.value(QStringLiteral("connection"));
    connection[QStringLiteral("id")] = m_id->text().trimmed();
    connection[QStringLiteral("type")] = m_type;
    connection[QStringLiteral("autoconnect")] = m_autoconnect->isChecked();
    connection[QStringLiteral("autoconnect-priority")] = m_priority->value();
    const QString iface = m_interface->currentData().toString();
    if (iface.isEmpty())
        connection.remove(QStringLiteral("interface-name"));
    else
        connection[QStringLiteral("interface-name")] = iface;
    if (m_allUsers->isChecked())
        connection[QStringLiteral("permissions")] = QStringList();
    else
        connection[QStringLiteral("permissions")] =
            QStringList{QStringLiteral("user:%1:").arg(qEnvironmentVariable("USER"))};
    // Written by NM itself; sending it back is harmless but pointless.
    connection.remove(QStringLiteral("timestamp"));
    settings[QStringLiteral("connection")] = connection;

    if (m_type == QLatin1String("802-11-wireless")) {
        QVariantMap wireless = settings.value(QStringLiteral("802-11-wireless"));
        wireless[QStringLiteral("ssid")] = NM::ssidToBytes(m_ssid->text());
        wireless[QStringLiteral("mode")] = m_wifiMode->currentData().toString();
        if (m_bssid->text().trimmed().isEmpty())
            wireless.remove(QStringLiteral("bssid"));
        else
            wireless[QStringLiteral("bssid")] = m_bssid->text().trimmed();
        if (m_wifiMtu->value() > 0)
            wireless[QStringLiteral("mtu")] = uint(m_wifiMtu->value());
        else
            wireless.remove(QStringLiteral("mtu"));
        // NM keeps a list of BSSIDs it has seen; it is state, not settings.
        wireless.remove(QStringLiteral("seen-bssids"));

        const QString keyMgmt = m_security->currentData().toString();
        if (keyMgmt.isEmpty()) {
            wireless.remove(QStringLiteral("security"));
            settings.remove(QStringLiteral("802-11-wireless-security"));
        } else {
            wireless[QStringLiteral("security")] = QStringLiteral("802-11-wireless-security");
            QVariantMap security = settings.value(QStringLiteral("802-11-wireless-security"));
            security[QStringLiteral("key-mgmt")] = keyMgmt;
            const QString password = m_password->text();
            if (keyMgmt == QLatin1String(kSecWep)) {
                security.remove(QStringLiteral("psk"));
                security.remove(QStringLiteral("psk-flags"));
                if (!password.isEmpty()) {
                    security[QStringLiteral("wep-key0")] = password;
                    security[QStringLiteral("wep-key-type")] = 2u; // passphrase
                    security[QStringLiteral("auth-alg")] = QStringLiteral("open");
                }
            } else {
                security.remove(QStringLiteral("wep-key0"));
                security.remove(QStringLiteral("wep-key-type"));
                security.remove(QStringLiteral("auth-alg"));
                if (!password.isEmpty()) {
                    security[QStringLiteral("psk")] = password;
                    // 0 = NM stores the secret itself: no secret agent needed.
                    security[QStringLiteral("psk-flags")] = 0u;
                }
            }
            settings[QStringLiteral("802-11-wireless-security")] = security;
        }
        settings[QStringLiteral("802-11-wireless")] = wireless;
    } else {
        QVariantMap wired = settings.value(QStringLiteral("802-3-ethernet"));
        if (m_clonedMac->text().trimmed().isEmpty())
            wired.remove(QStringLiteral("cloned-mac-address"));
        else
            wired[QStringLiteral("cloned-mac-address")] = m_clonedMac->text().trimmed();
        if (m_ethernetMtu->value() > 0)
            wired[QStringLiteral("mtu")] = uint(m_ethernetMtu->value());
        else
            wired.remove(QStringLiteral("mtu"));
        settings[QStringLiteral("802-3-ethernet")] = wired;
    }

    settings[QStringLiteral("ipv4")] = NetworkSettings::ipToSettings(m_ipv4->config(), false);
    settings[QStringLiteral("ipv6")] = NetworkSettings::ipToSettings(m_ipv6->config(), true);
    // The legacy numeric forms would compete with address-data/route-data.
    for (const QString &group : {QStringLiteral("ipv4"), QStringLiteral("ipv6")}) {
        QVariantMap ip = settings.value(group);
        ip.remove(QStringLiteral("addresses"));
        ip.remove(QStringLiteral("routes"));
        ip.remove(QStringLiteral("dns"));
        settings[group] = ip;
    }
    return settings;
}
