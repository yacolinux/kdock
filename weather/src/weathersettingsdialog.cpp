#include "weathersettingsdialog.h"

#include "weatherconfig.h"
#include "weathercontrol.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

WeatherSettingsDialog::WeatherSettingsDialog(WeatherConfig *config, WeatherControl *weather,
                                             QWidget *parent)
    : QDialog(parent)
    , m_config(config)
    , m_weather(weather)
{
    setWindowTitle(tr("Configuración del clima"));
    resize(560, 620);

    auto *root = new QVBoxLayout(this);

    // --- Ciudades guardadas ------------------------------------------------
    {
        auto *box = new QGroupBox(tr("Ciudades"), this);
        auto *layout = new QVBoxLayout(box);

        m_cities = new QListWidget(box);
        m_cities->setSelectionMode(QAbstractItemView::SingleSelection);
        layout->addWidget(m_cities);

        auto *row = new QHBoxLayout;
        auto *activate = new QPushButton(tr("Usar esta"), box);
        auto *remove = new QPushButton(tr("Quitar"), box);
        row->addWidget(activate);
        row->addWidget(remove);
        row->addStretch();
        layout->addLayout(row);

        connect(activate, &QPushButton::clicked, this, [this] {
            if (m_cities->currentRow() >= 0)
                m_config->setActiveCity(m_cities->currentRow());
            reloadCities();
        });
        connect(remove, &QPushButton::clicked, this, [this] {
            if (m_cities->currentRow() >= 0)
                m_config->removeCity(m_cities->currentRow());
            reloadCities();
        });
        // Double click is the obvious gesture for "use this one".
        connect(m_cities, &QListWidget::itemDoubleClicked, this, [this] {
            if (m_cities->currentRow() >= 0)
                m_config->setActiveCity(m_cities->currentRow());
            reloadCities();
        });

        root->addWidget(box);
    }

    // --- Buscador ----------------------------------------------------------
    {
        auto *box = new QGroupBox(tr("Agregar una ciudad"), this);
        auto *layout = new QVBoxLayout(box);

        auto *row = new QHBoxLayout;
        m_search = new QLineEdit(box);
        m_search->setPlaceholderText(tr("Nombre de la ciudad…"));
        m_search->setClearButtonEnabled(true);
        auto *searchButton = new QPushButton(tr("Buscar"), box);
        row->addWidget(m_search, 1);
        row->addWidget(searchButton);
        layout->addLayout(row);

        m_searchStatus = new QLabel(box);
        m_searchStatus->setWordWrap(true);
        layout->addWidget(m_searchStatus);

        m_results = new QListWidget(box);
        m_results->setMinimumHeight(140);
        layout->addWidget(m_results);

        m_addButton = new QPushButton(tr("Agregar la seleccionada"), box);
        m_addButton->setEnabled(false);
        layout->addWidget(m_addButton);

        const auto doSearch = [this] {
            if (m_search->text().trimmed().isEmpty())
                return;
            m_results->clear();
            m_found.clear();
            m_addButton->setEnabled(false);
            m_searchStatus->setText(tr("Buscando…"));
            m_weather->searchCity(m_search->text());
        };
        connect(searchButton, &QPushButton::clicked, this, doSearch);
        connect(m_search, &QLineEdit::returnPressed, this, doSearch);
        connect(m_weather, &WeatherControl::citiesFound,
                this, &WeatherSettingsDialog::onSearchResults);
        connect(m_weather, &WeatherControl::searchFailed, this, [this](const QString &message) {
            m_searchStatus->setText(tr("No se pudo buscar: %1").arg(message));
        });
        connect(m_results, &QListWidget::currentRowChanged, this, [this](int row) {
            m_addButton->setEnabled(row >= 0);
        });

        const auto addCurrent = [this] {
            const int row = m_results->currentRow();
            if (row < 0 || row >= m_found.size())
                return;
            m_config->addCity(m_found.at(row).toMap());
            reloadCities();
            m_searchStatus->setText(tr("Agregada y activada."));
        };
        connect(m_addButton, &QPushButton::clicked, this, addCurrent);
        connect(m_results, &QListWidget::itemDoubleClicked, this, addCurrent);

        root->addWidget(box);
    }

    // --- Unidades y actualización -------------------------------------------
    {
        auto *box = new QGroupBox(tr("Unidades y actualización"), this);
        auto *form = new QFormLayout(box);

        auto *temp = new QComboBox(box);
        temp->addItem(tr("Celsius (°C)"), false);
        temp->addItem(tr("Fahrenheit (°F)"), true);
        temp->setCurrentIndex(m_config->fahrenheit() ? 1 : 0);
        connect(temp, &QComboBox::currentIndexChanged, this,
                [this](int i) { m_config->setFahrenheit(i == 1); });
        form->addRow(tr("Temperatura:"), temp);

        auto *wind = new QComboBox(box);
        wind->addItem(tr("Metros por segundo (m/s)"), int(WeatherConfig::MetersPerSecond));
        wind->addItem(tr("Kilómetros por hora (km/h)"), int(WeatherConfig::KilometersPerHour));
        wind->addItem(tr("Millas por hora (mph)"), int(WeatherConfig::MilesPerHour));
        wind->setCurrentIndex(m_config->windUnit());
        connect(wind, &QComboBox::currentIndexChanged, this,
                [this](int i) { m_config->setWindUnit(i); });
        form->addRow(tr("Viento:"), wind);

        auto *days = new QSpinBox(box);
        days->setRange(3, 16);
        days->setValue(m_config->forecastDays());
        days->setSuffix(tr(" días"));
        connect(days, &QSpinBox::valueChanged, this,
                [this](int v) { m_config->setForecastDays(v); });
        form->addRow(tr("Pronóstico:"), days);

        auto *refresh = new QSpinBox(box);
        refresh->setRange(10, 720);
        refresh->setSingleStep(10);
        refresh->setValue(m_config->refreshMinutes());
        refresh->setSuffix(tr(" min"));
        refresh->setToolTip(tr("Cada cuánto se le pregunta al servicio. Entre pedido y pedido "
                               "se muestra lo último que se recibió, guardado en disco."));
        connect(refresh, &QSpinBox::valueChanged, this,
                [this](int v) { m_config->setRefreshMinutes(v); });
        form->addRow(tr("Actualizar cada:"), refresh);

        auto *credit = new QLabel(tr("Los datos son de Open-Meteo.com (sin cuenta ni clave)."), box);
        credit->setWordWrap(true);
        form->addRow(credit);

        root->addWidget(box);
    }

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    // Qt labels its standard buttons from *its own* catalogs, i.e. in the system
    // locale, which would leave a Spanish "Cerrar" in a dialog the user asked to
    // see in another language. Setting the text puts it back on our layer.
    buttons->button(QDialogButtonBox::Close)->setText(tr("Cerrar"));
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
    root->addWidget(buttons);

    reloadCities();
}

void WeatherSettingsDialog::reloadCities()
{
    m_cities->clear();
    const QVariantList cities = m_config->cities();
    for (int i = 0; i < cities.size(); ++i) {
        const QString label = WeatherConfig::cityLabel(cities.at(i).toMap());
        m_cities->addItem(i == m_config->activeCity()
                              ? tr("%1   ← en uso").arg(label)
                              : label);
    }
    if (m_config->activeCity() < m_cities->count())
        m_cities->setCurrentRow(m_config->activeCity());
}

void WeatherSettingsDialog::onSearchResults(const QVariantList &cities)
{
    m_found = cities;
    m_results->clear();
    for (const QVariant &v : cities) {
        const QVariantMap c = v.toMap();
        // The country's full name here (not the code the title bar uses): the
        // point of this list is telling apart the six places called the same.
        QStringList parts{c.value(QStringLiteral("name")).toString()};
        if (!c.value(QStringLiteral("admin1")).toString().isEmpty())
            parts << c.value(QStringLiteral("admin1")).toString();
        if (!c.value(QStringLiteral("country")).toString().isEmpty())
            parts << c.value(QStringLiteral("country")).toString();
        m_results->addItem(parts.join(QStringLiteral(", ")));
    }
    m_searchStatus->setText(cities.isEmpty()
                                ? tr("No se encontró ninguna ciudad con ese nombre.")
                                : tr("%1 resultados.").arg(cities.size()));
    if (!cities.isEmpty())
        m_results->setCurrentRow(0);
}
