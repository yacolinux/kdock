// WeatherControl: el parseo de la respuesta del proveedor, el mapeo de códigos
// WMO a íconos y el formateo por unidades.
//
// Todo corre contra un JSON grabado (tests/fixtures/weather/corrientes.json) a
// través de la costura KDOCK_WEATHER_FIXTURE, así que el test es
// determinístico y **no necesita internet** — CI no tiene.
//
// Los casos no son un barrido de la clase: son las cosas que rompen callado. El
// mapeo de íconos sobre todo, que se ve como "el clima muestra el ícono
// equivocado" y ninguna corrida de arnés lo delata.

#include "sandbox.h"
#include "weatherconfig.h"
#include "weathercontrol.h"

#include <QSignalSpy>
#include <QTest>

class TestWeather : public QObject
{
    Q_OBJECT

private:
    // La ruta llega por el entorno (tests/CMakeLists.txt) y no por un -D: el
    // repo tiene un '#' en la ruta y CMake descarta callado cualquier
    // definición que lo contenga.
    static QString fixturePath()
    {
        return QString::fromLocal8Bit(qgetenv("KDOCK_FIXTURES"))
               + QStringLiteral("/weather/corrientes.json");
    }

    // Una ciudad cualquiera: con el fixture puesto, las coordenadas no se usan
    // para nada más que para que configured() diga que sí.
    static QVariantMap corrientes()
    {
        QVariantMap c;
        c[QStringLiteral("name")] = QStringLiteral("Ciudad de Corrientes");
        c[QStringLiteral("admin1")] = QStringLiteral("Provincia de Corrientes");
        c[QStringLiteral("country")] = QStringLiteral("Argentina");
        c[QStringLiteral("countryCode")] = QStringLiteral("AR");
        c[QStringLiteral("lat")] = -27.46784;
        c[QStringLiteral("lon")] = -58.8344;
        return c;
    }

private slots:
    void initTestCase()
    {
        qputenv("KDOCK_WEATHER_FIXTURE", QFile::encodeName(fixturePath()));
        QVERIFY2(QFile::exists(fixturePath()), qPrintable(fixturePath()));
    }

    void withoutACityThereIsNothingToShow()
    {
        WeatherConfig config;
        WeatherControl weather(&config);
        QVERIFY(!weather.configured());
        // Y no se cae: la mini-app abre el selector de ciudad en este estado.
        QVERIFY(weather.forecast().isEmpty());
        QVERIFY(weather.details().isEmpty());
    }

    void readsTheFixture()
    {
        WeatherConfig config;
        config.addCity(corrientes());
        WeatherControl weather(&config);
        QVERIFY(weather.configured());

        QSignalSpy spy(&weather, &WeatherControl::changed);
        weather.refresh(true);
        QVERIFY(weather.available());

        // 12.0 °C en el fixture; el número se redondea y lleva su unidad.
        QVERIFY2(weather.tempText().contains(QStringLiteral("12")),
                 qPrintable(weather.tempText()));
        QVERIFY(weather.tempText().contains(QStringLiteral("°C")));
        // weather_code 3 = nublado, is_day 1.
        QCOMPARE(weather.iconName(), QStringLiteral("weather-many-clouds"));
        QCOMPARE(weather.conditionText(), WeatherControl::textForCode(3));
        QCOMPARE(weather.windDirection(), 129);
        QVERIFY(!spy.isEmpty());
    }

    void forecastHasOneRowPerDayWithItsProbability()
    {
        WeatherConfig config;
        config.addCity(corrientes());
        WeatherControl weather(&config);
        weather.refresh(true);

        const QVariantList days = weather.forecast();
        QCOMPARE(days.size(), 7);

        // El primero es hoy y se rotula distinto de los demás.
        const QVariantMap today = days.first().toMap();
        QVERIFY(!today.value(QStringLiteral("dayLabel")).toString().isEmpty());
        QCOMPARE(today.value(QStringLiteral("precipProbability")).toInt(), 53);
        QVERIFY(today.value(QStringLiteral("maxText")).toString().contains(QStringLiteral("15")));
        QVERIFY(today.value(QStringLiteral("minText")).toString().contains(QStringLiteral("12")));

        // Un día de tormenta del fixture (code 95) tiene que traer su ícono.
        const QVariantMap storm = days.at(5).toMap();
        QCOMPARE(storm.value(QStringLiteral("iconName")).toString(),
                 QStringLiteral("weather-storm"));
    }

    void detailsCarryTheRowsTheKdeAppletShows()
    {
        WeatherConfig config;
        config.addCity(corrientes());
        WeatherControl weather(&config);
        weather.refresh(true);

        QStringList labels;
        for (const QVariant &v : weather.details())
            labels << v.toMap().value(QStringLiteral("label")).toString();

        // Humedad, presión y ráfaga son las que vienen del bloque "current";
        // visibilidad es la que se busca por hora y es la fácil de perder.
        QVERIFY(!labels.isEmpty());
        QCOMPARE(labels.size(), weather.details().size());
        bool hasVisibility = false;
        for (const QVariant &v : weather.details()) {
            if (v.toMap().value(QStringLiteral("label")).toString()
                == WeatherControl::tr("Visibilidad"))
                hasVisibility = true;
        }
        QVERIFY2(hasVisibility, "la visibilidad sale del array horario, no de current");
    }

    void unitsAreADisplayChoice()
    {
        WeatherConfig config;
        config.addCity(corrientes());
        WeatherControl weather(&config);
        weather.refresh(true);

        // El proveedor contesta siempre en °C y km/h; la unidad es de dibujo, y
        // por eso cambiarla no vuelve a pedir nada.
        config.setWindUnit(WeatherConfig::KilometersPerHour);
        QVERIFY2(weather.windText().contains(QStringLiteral("km/h")),
                 qPrintable(weather.windText()));
        config.setWindUnit(WeatherConfig::MetersPerSecond);
        QVERIFY(weather.windText().contains(QStringLiteral("m/s")));

        config.setFahrenheit(true);
        QVERIFY2(weather.tempText().contains(QStringLiteral("°F")),
                 qPrintable(weather.tempText()));
        // 12 °C son 53,6 °F.
        QVERIFY(weather.tempText().contains(QStringLiteral("54")));
    }

    void nightVariantsOnlyExistForTheClearEndOfTheTable()
    {
        // Breeze tiene -night para despejado y nubes, y no para lluvia/nieve/
        // tormenta: pedir "weather-showers-night" da un ícono roto.
        QCOMPARE(WeatherControl::iconForCode(0, true), QStringLiteral("weather-clear"));
        QCOMPARE(WeatherControl::iconForCode(0, false), QStringLiteral("weather-clear-night"));
        QCOMPARE(WeatherControl::iconForCode(1, false), QStringLiteral("weather-few-clouds-night"));
        QCOMPARE(WeatherControl::iconForCode(2, false), QStringLiteral("weather-clouds-night"));
        // De acá para abajo el día no cambia nada.
        QCOMPARE(WeatherControl::iconForCode(3, false), QStringLiteral("weather-many-clouds"));
        QCOMPARE(WeatherControl::iconForCode(61, false), QStringLiteral("weather-showers"));
        QCOMPARE(WeatherControl::iconForCode(95, false), QStringLiteral("weather-storm"));
        QCOMPARE(WeatherControl::iconForCode(45, true), QStringLiteral("weather-fog"));
        QCOMPARE(WeatherControl::iconForCode(75, true), QStringLiteral("weather-snow"));
        // Un código que el proveedor no documenta no puede dejar la URL vacía:
        // QML dibujaría un hueco y parecería un bug de layout.
        QCOMPARE(WeatherControl::iconForCode(1234, true),
                 QStringLiteral("weather-none-available"));
        QVERIFY(!WeatherControl::textForCode(1234).isEmpty());
    }

    void everyWmoCodeOfTheTableHasAnIconAndAText()
    {
        // El barrido barato: cualquier código que el proveedor pueda mandar
        // tiene que resolver a *algo* dibujable.
        const QList<int> codes = {0, 1, 2, 3, 45, 48, 51, 53, 55, 56, 57, 61, 63, 65,
                                  66, 67, 71, 73, 75, 77, 80, 81, 82, 85, 86, 95, 96, 99};
        for (int code : codes) {
            const QString icon = WeatherControl::iconForCode(code, true);
            QVERIFY2(icon.startsWith(QStringLiteral("weather-")), qPrintable(icon));
            QVERIFY2(icon != QStringLiteral("weather-none-available"),
                     qPrintable(QStringLiteral("código %1 sin ícono propio").arg(code)));
            QVERIFY(!WeatherControl::textForCode(code).isEmpty());
        }
    }

    void cityLabelIsNameProvinceCountryCode()
    {
        QCOMPARE(WeatherConfig::cityLabel(corrientes()),
                 QStringLiteral("Ciudad de Corrientes, Provincia de Corrientes, AR"));
        QVERIFY(WeatherConfig::cityLabel({}).isEmpty());
    }

    void addingTheSameCityTwiceDoesNotDuplicateIt()
    {
        WeatherConfig config;
        config.addCity(corrientes());
        const int before = config.cities().size();
        // El geocodificador devuelve la misma ciudad con otro nombre según el
        // idioma de la consulta: la deduplicación es por coordenadas.
        QVariantMap again = corrientes();
        again[QStringLiteral("name")] = QStringLiteral("Corrientes");
        config.addCity(again);
        QCOMPARE(config.cities().size(), before);
    }
};

KDOCK_TEST_MAIN(TestWeather)
#include "tst_weather.moc"
