#include "networksettingswidget.h"

#include "connectioneditor.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QStackedWidget>
#include <QTimer>
#include <QTreeWidget>
#include <QUuid>
#include <QVBoxLayout>

namespace {
constexpr int RoleKind = Qt::UserRole;     // "device" / "conn"
constexpr int RolePath = Qt::UserRole + 1; // NM object path

QIcon iconForConnection(const NetworkSettings::Connection &conn)
{
    return QIcon::fromTheme(conn.isWifi() ? QStringLiteral("network-wireless")
                                          : QStringLiteral("network-wired"));
}
} // namespace

NetworkSettingsWidget::NetworkSettingsWidget(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    auto *splitter = new QSplitter(Qt::Horizontal, this);
    layout->addWidget(splitter, 1);

    // --- left: devices + connections ---
    auto *left = new QWidget(splitter);
    auto *leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);

    m_tree = new QTreeWidget(left);
    m_tree->setHeaderHidden(true);
    m_tree->setRootIsDecorated(false);
    m_tree->setIndentation(12);
    leftLayout->addWidget(m_tree);

    auto *buttons = new QHBoxLayout;
    m_addButton = new QPushButton(QIcon::fromTheme(QStringLiteral("list-add")), tr("Agregar"),
                                  left);
    auto *addMenu = new QMenu(m_addButton);
    addMenu->addAction(QIcon::fromTheme(QStringLiteral("network-wireless")), tr("Wi-Fi"), this,
                       [this] { addConnection(QStringLiteral("802-11-wireless")); });
    addMenu->addAction(QIcon::fromTheme(QStringLiteral("network-wired")), tr("Ethernet"), this,
                       [this] { addConnection(QStringLiteral("802-3-ethernet")); });
    m_addButton->setMenu(addMenu);
    m_deleteButton = new QPushButton(QIcon::fromTheme(QStringLiteral("list-remove")),
                                     tr("Borrar"), left);
    buttons->addWidget(m_addButton);
    buttons->addWidget(m_deleteButton);
    leftLayout->addLayout(buttons);

    m_toggleButton = new QPushButton(tr("Conectar"), left);
    leftLayout->addWidget(m_toggleButton);

    // --- right: device info or the editor ---
    m_stack = new QStackedWidget(splitter);
    m_placeholder = new QLabel(tr("Elegí un dispositivo o una conexión de la lista."), m_stack);
    m_placeholder->setAlignment(Qt::AlignCenter);
    m_placeholder->setWordWrap(true);
    m_devicePage = new QLabel(m_stack);
    m_devicePage->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_devicePage->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_devicePage->setMargin(12);
    m_editor = new ConnectionEditor(m_stack);
    m_stack->addWidget(m_placeholder);
    m_stack->addWidget(m_devicePage);
    m_stack->addWidget(m_editor);

    splitter->addWidget(left);
    splitter->addWidget(m_stack);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    // Stretch factors alone leave the list too narrow for "wlp0s20f3 — Wi-Fi
    // (conectado)", which then elides to uselessness.
    left->setMinimumWidth(300);
    splitter->setSizes({320, 640});

    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    layout->addWidget(m_status);

    connect(m_tree, &QTreeWidget::currentItemChanged, this,
            [this] { selectionChanged(); });
    connect(m_deleteButton, &QPushButton::clicked, this, &NetworkSettingsWidget::deleteSelected);
    connect(m_toggleButton, &QPushButton::clicked, this, &NetworkSettingsWidget::toggleSelected);
    connect(m_editor, &ConnectionEditor::applyRequested, this,
            &NetworkSettingsWidget::applyEditor);
    connect(m_editor, &ConnectionEditor::discardRequested, this, [this] {
        if (m_editingNew) {
            m_editingNew = false;
            m_editingPath.clear();
            m_stack->setCurrentWidget(m_placeholder);
            reload();
        } else {
            showConnection(m_editingPath);
        }
    });
    connect(m_editor, &ConnectionEditor::dirtyChanged, this,
            [this] { updateButtons(); });

    // NM fires a burst of PropertiesChanged during any activation; rebuilding
    // the tree on each one would fight the user's selection.
    m_reloadTimer = new QTimer(this);
    m_reloadTimer->setSingleShot(true);
    m_reloadTimer->setInterval(700);
    connect(m_reloadTimer, &QTimer::timeout, this, [this] {
        // Never pull the rug from under a half-finished edit.
        if (m_editor->isDirty()) {
            m_reloadTimer->start();
            return;
        }
        reload();
    });
    connect(&m_backend, &NetworkSettings::changed, this, [this] { m_reloadTimer->start(); });

    reload();
}

void NetworkSettingsWidget::reload()
{
    const QString keepKind = m_tree->currentItem()
                                 ? m_tree->currentItem()->data(0, RoleKind).toString()
                                 : QString();
    const QString keepPath = m_tree->currentItem()
                                 ? m_tree->currentItem()->data(0, RolePath).toString()
                                 : QString();

    m_devices = m_backend.devices();
    m_connections = m_backend.connections();

    const QSignalBlocker block(m_tree);
    m_tree->clear();

    auto *deviceRoot = new QTreeWidgetItem(m_tree, {tr("Dispositivos")});
    deviceRoot->setFlags(Qt::ItemIsEnabled);
    QFont bold = deviceRoot->font(0);
    bold.setBold(true);
    deviceRoot->setFont(0, bold);
    for (const NetworkSettings::Device &device : m_devices) {
        // The icon already says which kind of device it is, so the row spends
        // its width on the name and the state instead of repeating the type.
        auto *item = new QTreeWidgetItem(
            deviceRoot, {QStringLiteral("%1 — %2").arg(device.iface,
                                                       NetworkSettings::stateLabel(device.state))});
        item->setToolTip(0, NetworkSettings::typeLabel(device.type));
        item->setIcon(0, QIcon::fromTheme(device.isWifi() ? QStringLiteral("network-wireless")
                                                          : QStringLiteral("network-wired")));
        item->setData(0, RoleKind, QStringLiteral("device"));
        item->setData(0, RolePath, device.path);
    }

    auto *connRoot = new QTreeWidgetItem(m_tree, {tr("Conexiones")});
    connRoot->setFlags(Qt::ItemIsEnabled);
    connRoot->setFont(0, bold);
    for (const NetworkSettings::Connection &conn : m_connections) {
        auto *item = new QTreeWidgetItem(connRoot, {conn.id});
        item->setIcon(0, iconForConnection(conn));
        item->setData(0, RoleKind, QStringLiteral("conn"));
        item->setData(0, RolePath, conn.path);
        if (conn.active) {
            QFont font = item->font(0);
            font.setBold(true);
            item->setFont(0, font);
        }
    }
    m_tree->expandAll();

    // Restore the selection the rebuild threw away.
    if (!keepPath.isEmpty()) {
        for (QTreeWidgetItem *item : m_tree->findItems(QString(), Qt::MatchContains
                                                                      | Qt::MatchRecursive)) {
            if (item->data(0, RolePath).toString() == keepPath
                && item->data(0, RoleKind).toString() == keepKind) {
                m_tree->setCurrentItem(item);
                break;
            }
        }
    }
    updateButtons();
}

QString NetworkSettingsWidget::selectedConnPath() const
{
    QTreeWidgetItem *item = m_tree->currentItem();
    if (!item || item->data(0, RoleKind).toString() != QLatin1String("conn"))
        return {};
    return item->data(0, RolePath).toString();
}

void NetworkSettingsWidget::selectionChanged()
{
    QTreeWidgetItem *item = m_tree->currentItem();
    m_status->clear();
    if (!item) {
        m_stack->setCurrentWidget(m_placeholder);
        updateButtons();
        return;
    }
    const QString kind = item->data(0, RoleKind).toString();
    const QString path = item->data(0, RolePath).toString();
    if (kind == QLatin1String("device")) {
        for (const NetworkSettings::Device &device : m_devices) {
            if (device.path == path) {
                showDevice(device);
                break;
            }
        }
    } else if (kind == QLatin1String("conn")) {
        showConnection(path);
    } else {
        m_stack->setCurrentWidget(m_placeholder);
    }
    updateButtons();
}

void NetworkSettingsWidget::showDevice(const NetworkSettings::Device &device)
{
    QStringList lines;
    lines << tr("<b>%1</b>").arg(device.iface);
    lines << tr("Tipo: %1").arg(NetworkSettings::typeLabel(device.type));
    lines << tr("Estado: %1").arg(NetworkSettings::stateLabel(device.state));
    if (!device.driver.isEmpty())
        lines << tr("Controlador: %1").arg(device.driver);
    if (!device.hwAddress.isEmpty())
        lines << tr("Dirección MAC: %1").arg(device.hwAddress);
    const QStringList ip = m_backend.deviceIpSummary(device);
    if (!ip.isEmpty())
        lines << QString() << ip;
    else
        lines << QString() << tr("Sin configuración IP activa.");
    m_devicePage->setText(lines.join(QStringLiteral("<br>")));
    m_stack->setCurrentWidget(m_devicePage);
}

void NetworkSettingsWidget::showConnection(const QString &connPath)
{
    if (connPath.isEmpty())
        return;
    const NMSettingsMap settings = m_backend.settings(connPath);
    NMSettingsMap secrets;
    if (settings.contains(QStringLiteral("802-11-wireless-security"))) {
        // System-owned secrets come back straight away; agent-owned ones (a
        // password kept in KWallet by plasma-nm) come back empty, and the field
        // is simply left blank.
        secrets = m_backend.secrets(connPath, QStringLiteral("802-11-wireless-security"));
    }
    m_editingPath = connPath;
    m_editingNew = false;
    m_editor->load(settings, secrets, m_devices, false);
    m_stack->setCurrentWidget(m_editor);
}

void NetworkSettingsWidget::addConnection(const QString &type)
{
    const bool wifi = type == QLatin1String("802-11-wireless");

    NMSettingsMap settings;
    settings[QStringLiteral("connection")] = QVariantMap{
        {QStringLiteral("id"), wifi ? tr("Wi-Fi nueva") : tr("Ethernet nueva")},
        {QStringLiteral("type"), type},
        {QStringLiteral("uuid"), QUuid::createUuid().toString(QUuid::WithoutBraces)},
        {QStringLiteral("autoconnect"), true},
    };
    if (wifi) {
        settings[QStringLiteral("802-11-wireless")] =
            QVariantMap{{QStringLiteral("mode"), QStringLiteral("infrastructure")}};
    } else {
        settings[QStringLiteral("802-3-ethernet")] = QVariantMap{};
    }
    settings[QStringLiteral("ipv4")] = QVariantMap{{QStringLiteral("method"),
                                                    QStringLiteral("auto")}};
    settings[QStringLiteral("ipv6")] = QVariantMap{{QStringLiteral("method"),
                                                    QStringLiteral("auto")}};

    m_editingPath.clear();
    m_editingNew = true;
    m_editor->load(settings, {}, m_devices, true);
    m_stack->setCurrentWidget(m_editor);
    m_status->setText(tr("Conexión nueva: completá los datos y tocá Aplicar para guardarla."));
    updateButtons();
}

void NetworkSettingsWidget::applyEditor()
{
    const NMSettingsMap settings = m_editor->collect();
    const QString id = settings.value(QStringLiteral("connection"))
                           .value(QStringLiteral("id")).toString();
    if (id.isEmpty()) {
        m_status->setText(tr("La conexión necesita un nombre."));
        return;
    }

    QString error;
    if (m_editingNew) {
        QString newPath;
        error = m_backend.addConnection(settings, &newPath);
        if (error.isEmpty()) {
            m_editingNew = false;
            m_editingPath = newPath;
        }
    } else {
        error = m_backend.updateConnection(m_editingPath, settings);
    }

    if (error.isEmpty()) {
        m_status->setText(tr("Guardada: %1").arg(id));
        m_editor->setDirty(false);
        reload();
        // Re-read from NM so the form shows what was actually stored (NM
        // normalizes: it drops the gateway when never-default is on, for one).
        if (!m_editingPath.isEmpty())
            showConnection(m_editingPath);
    } else {
        m_status->setText(tr("NetworkManager rechazó los cambios: %1").arg(error));
    }
}

void NetworkSettingsWidget::deleteSelected()
{
    const QString path = selectedConnPath();
    if (path.isEmpty())
        return;
    QString id = path;
    for (const NetworkSettings::Connection &conn : m_connections) {
        if (conn.path == path)
            id = conn.id;
    }
    if (QMessageBox::question(this, tr("Borrar conexión"),
                              tr("¿Borrar «%1»? No se puede deshacer.").arg(id))
        != QMessageBox::Yes)
        return;
    const QString error = m_backend.deleteConnection(path);
    m_status->setText(error.isEmpty() ? tr("Borrada: %1").arg(id)
                                      : tr("No se pudo borrar: %1").arg(error));
    m_stack->setCurrentWidget(m_placeholder);
    reload();
}

void NetworkSettingsWidget::toggleSelected()
{
    QTreeWidgetItem *item = m_tree->currentItem();
    if (!item)
        return;
    const QString path = item->data(0, RolePath).toString();
    const QString kind = item->data(0, RoleKind).toString();

    if (kind == QLatin1String("device")) {
        for (const NetworkSettings::Device &device : m_devices) {
            if (device.path != path)
                continue;
            if (!device.activeConnPath.isEmpty()) {
                const QString error = m_backend.deactivate(device.activeConnPath);
                if (!error.isEmpty())
                    m_status->setText(error);
            }
            break;
        }
        return;
    }

    for (const NetworkSettings::Connection &conn : m_connections) {
        if (conn.path != path)
            continue;
        QString error;
        if (conn.active) {
            error = m_backend.deactivate(conn.activePath);
        } else {
            // Bind to the device the connection names, if it names one.
            QString devicePath;
            for (const NetworkSettings::Device &device : m_devices) {
                if (device.isWifi() == conn.isWifi()
                    && (conn.iface.isEmpty() || conn.iface == device.iface)) {
                    devicePath = device.path;
                    break;
                }
            }
            error = m_backend.activate(conn.path, devicePath);
        }
        m_status->setText(error);
        break;
    }
}

void NetworkSettingsWidget::updateButtons()
{
    const QString connPath = selectedConnPath();
    m_deleteButton->setEnabled(!connPath.isEmpty());

    QTreeWidgetItem *item = m_tree->currentItem();
    const QString kind = item ? item->data(0, RoleKind).toString() : QString();
    bool active = false;
    if (kind == QLatin1String("conn")) {
        for (const NetworkSettings::Connection &conn : m_connections)
            if (conn.path == connPath)
                active = conn.active;
    } else if (kind == QLatin1String("device")) {
        for (const NetworkSettings::Device &device : m_devices)
            if (device.path == item->data(0, RolePath).toString())
                active = !device.activeConnPath.isEmpty();
    }
    m_toggleButton->setEnabled(kind == QLatin1String("conn")
                               || (kind == QLatin1String("device") && active));
    m_toggleButton->setText(active ? tr("Desconectar") : tr("Conectar"));
}
