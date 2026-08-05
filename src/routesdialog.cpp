#include "routesdialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

RoutesDialog::RoutesDialog(const NetworkSettings::IpConfig &config, bool v6, QWidget *parent)
    : QDialog(parent)
    , m_v6(v6)
{
    setWindowTitle(v6 ? tr("Rutas IPv6") : tr("Rutas IPv4"));
    auto *layout = new QVBoxLayout(this);

    m_table = new QTableWidget(0, 4, this);
    m_table->setHorizontalHeaderLabels({tr("Destino"), tr("Prefijo"), tr("Siguiente salto"),
                                        tr("Métrica")});
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_table->verticalHeader()->setVisible(false);
    layout->addWidget(m_table);

    for (const NetworkSettings::IpRoute &route : config.routes)
        addRow(route);

    auto *buttons = new QHBoxLayout;
    auto *add = new QPushButton(QIcon::fromTheme(QStringLiteral("list-add")), tr("Agregar"), this);
    auto *remove = new QPushButton(QIcon::fromTheme(QStringLiteral("list-remove")), tr("Quitar"),
                                   this);
    buttons->addWidget(add);
    buttons->addWidget(remove);
    buttons->addStretch();
    layout->addLayout(buttons);

    connect(add, &QPushButton::clicked, this, [this, v6] {
        NetworkSettings::IpRoute route;
        route.prefix = v6 ? 64 : 24;
        addRow(route);
    });
    connect(remove, &QPushButton::clicked, this, [this] {
        const int row = m_table->currentRow();
        if (row >= 0)
            m_table->removeRow(row);
    });

    m_ignoreAuto = new QCheckBox(tr("Ignorar las rutas obtenidas automáticamente"), this);
    m_ignoreAuto->setChecked(config.ignoreAutoRoutes);
    layout->addWidget(m_ignoreAuto);

    m_neverDefault = new QCheckBox(
        tr("Usar esta conexión solo para los recursos de su red"), this);
    m_neverDefault->setChecked(config.neverDefault);
    // NM clears ipv4.gateway when never-default is set, so the editor says so
    // instead of letting a typed gateway vanish on save.
    m_neverDefault->setToolTip(tr("NetworkManager descarta la puerta de enlace cuando esta "
                                  "opción está activa."));
    layout->addWidget(m_neverDefault);

    auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(box);

    resize(560, 340);
}

void RoutesDialog::addRow(const NetworkSettings::IpRoute &route)
{
    const int row = m_table->rowCount();
    m_table->insertRow(row);
    m_table->setItem(row, 0, new QTableWidgetItem(route.dest));
    m_table->setItem(row, 1, new QTableWidgetItem(QString::number(route.prefix)));
    m_table->setItem(row, 2, new QTableWidgetItem(route.nextHop));
    m_table->setItem(row, 3,
                     new QTableWidgetItem(route.metric >= 0 ? QString::number(route.metric)
                                                            : QString()));
}

QList<NetworkSettings::IpRoute> RoutesDialog::routes() const
{
    QList<NetworkSettings::IpRoute> out;
    for (int row = 0; row < m_table->rowCount(); ++row) {
        const auto text = [this, row](int column) {
            const QTableWidgetItem *item = m_table->item(row, column);
            return item ? item->text().trimmed() : QString();
        };
        NetworkSettings::IpRoute route;
        route.dest = text(0);
        if (route.dest.isEmpty())
            continue;
        bool ok = false;
        const int prefix = text(1).toInt(&ok);
        route.prefix = ok ? prefix : (m_v6 ? 64 : 24);
        route.nextHop = text(2);
        const int metric = text(3).toInt(&ok);
        route.metric = ok ? metric : -1;
        out.append(route);
    }
    return out;
}

bool RoutesDialog::ignoreAutoRoutes() const
{
    return m_ignoreAuto->isChecked();
}

bool RoutesDialog::neverDefault() const
{
    return m_neverDefault->isChecked();
}
