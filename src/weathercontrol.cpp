#include "weathercontrol.h"

#include "weatherconfig.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocale>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrlQuery>

namespace {

constexpr const char *kForecastHost = "https://api.open-meteo.com/v1/forecast";
constexpr const char *kGeocodeHost = "https://geocoding-api.open-meteo.com/v1/search";
// Open-Meteo asks for an identifiable client; and a request that never answers
// must not leave the UI spinning forever.
constexpr int kTimeoutMs = 15000;

// The fields the three surfaces need, in the provider's own units.
const char *kCurrentFields = "temperature_2m,apparent_temperature,relative_humidity_2m,"
                             "dew_point_2m,pressure_msl,wind_speed_10m,wind_direction_10m,"
                             "wind_gusts_10m,weather_code,is_day";
const char *kDailyFields = "weather_code,temperature_2m_max,temperature_2m_min,"
                           "precipitation_probability_max,sunrise,sunset";

double jnum(const QJsonObject &o, const char *key, double def = 0.0)
{
    const QJsonValue v = o.value(QLatin1String(key));
    return v.isDouble() ? v.toDouble() : def;
}

QJsonArray jarr(const QJsonObject &o, const char *group, const char *key)
{
    return o.value(QLatin1String(group)).toObject().value(QLatin1String(key)).toArray();
}

} // namespace

WeatherControl::WeatherControl(WeatherConfig *config, QObject *parent)
    : QObject(parent)
    , m_config(config)
{
    m_net = new QNetworkAccessManager(this);
    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, [this] { refresh(true); });

    if (m_config) {
        connect(m_config, &WeatherConfig::changed, this, &WeatherControl::onConfigChanged);
    }

    loadCache();
    // Showing the cache first and asking later is what gives the dock widget a
    // temperature in its very first frame after a restart.
    QTimer::singleShot(0, this, [this] { refresh(false); });
}

bool WeatherControl::configured() const
{
    return m_config && !m_config->city().isEmpty();
}

bool WeatherControl::stale() const
{
    if (m_data.isEmpty() || !m_fetched.isValid())
        return false;
    const int minutes = m_config ? m_config->refreshMinutes() : 30;
    return m_fetched.secsTo(QDateTime::currentDateTime()) > minutes * 60 + 300;
}

void WeatherControl::onConfigChanged()
{
    // A different city invalidates what is on screen; a unit change does not.
    const QVariantMap city = m_config->city();
    const double lat = city.value(QStringLiteral("lat")).toDouble();
    const double lon = city.value(QStringLiteral("lon")).toDouble();
    if (!qFuzzyCompare(lat + 1, m_lat + 1) || !qFuzzyCompare(lon + 1, m_lon + 1)) {
        m_data = {};
        m_fetched = {};
        refresh(true);
    }
    emit changed();
}

// ---------------------------------------------------------------- network --

void WeatherControl::refresh(bool force)
{
    if (!configured() || m_reply)
        return;

    // Test seam: a recorded response instead of the network. Deliberately does
    // not touch the cache or the timer, so a test run leaves nothing behind.
    const QByteArray fixture = qgetenv("KDOCK_WEATHER_FIXTURE");
    if (!fixture.isEmpty()) {
        QFile f(QString::fromLocal8Bit(fixture));
        if (f.open(QIODevice::ReadOnly)) {
            applyResponse(QJsonDocument::fromJson(f.readAll()).object(),
                          QDateTime::currentDateTime());
        } else {
            m_error = tr("No se pudo leer %1").arg(QString::fromLocal8Bit(fixture));
            emit changed();
        }
        return;
    }

    if (!force && !m_data.isEmpty() && !stale()) {
        scheduleNext(false);
        return;
    }

    const QVariantMap city = m_config->city();
    QUrl url(QString::fromLatin1(kForecastHost));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("latitude"),
                   QString::number(city.value(QStringLiteral("lat")).toDouble(), 'f', 4));
    q.addQueryItem(QStringLiteral("longitude"),
                   QString::number(city.value(QStringLiteral("lon")).toDouble(), 'f', 4));
    q.addQueryItem(QStringLiteral("current"), QString::fromLatin1(kCurrentFields));
    q.addQueryItem(QStringLiteral("daily"), QString::fromLatin1(kDailyFields));
    q.addQueryItem(QStringLiteral("hourly"), QStringLiteral("visibility"));
    q.addQueryItem(QStringLiteral("forecast_days"), QString::number(m_config->forecastDays()));
    q.addQueryItem(QStringLiteral("timezone"), QStringLiteral("auto"));
    url.setQuery(q);

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("kdock-weather/1.0"));
    req.setTransferTimeout(kTimeoutMs);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);

    m_loading = true;
    emit changed();

    m_reply = m_net->get(req);
    connect(m_reply, &QNetworkReply::finished, this, [this, city] {
        QNetworkReply *reply = m_reply;
        m_reply = nullptr;
        m_loading = false;
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            ++m_failures;
            m_error = reply->errorString();
            // Whatever was on screen stays there, marked stale: a blank widget
            // is a worse answer to "the wifi dropped" than an old temperature.
            emit changed();
            scheduleNext(true);
            return;
        }

        const QJsonObject data = QJsonDocument::fromJson(reply->readAll()).object();
        if (data.isEmpty() || !data.contains(QStringLiteral("current"))) {
            ++m_failures;
            m_error = tr("Respuesta del servicio del clima ilegible");
            emit changed();
            scheduleNext(true);
            return;
        }

        m_failures = 0;
        m_error.clear();
        m_lat = city.value(QStringLiteral("lat")).toDouble();
        m_lon = city.value(QStringLiteral("lon")).toDouble();
        applyResponse(data, QDateTime::currentDateTime());
        saveCache();
        scheduleNext(false);
    });
}

void WeatherControl::applyResponse(const QJsonObject &data, const QDateTime &fetched)
{
    m_data = data;
    m_fetched = fetched;
    emit changed();
}

void WeatherControl::scheduleNext(bool afterFailure)
{
    const int minutes = m_config ? m_config->refreshMinutes() : 30;
    // 1, 2, 4, 8… intervals, capped: a provider that is down must not become a
    // request loop (same rule as the pactl respawn backoff).
    const int factor = afterFailure ? qMin(1 << qMin(m_failures, 4), 16) : 1;
    m_timer->start(minutes * 60 * 1000 * factor);
}

void WeatherControl::searchCity(const QString &name)
{
    if (name.trimmed().isEmpty())
        return;
    if (m_searchReply) {
        m_searchReply->abort();
        m_searchReply = nullptr;
    }

    QUrl url(QString::fromLatin1(kGeocodeHost));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("name"), name.trimmed());
    q.addQueryItem(QStringLiteral("count"), QStringLiteral("15"));
    q.addQueryItem(QStringLiteral("language"), QLocale::system().name().left(2));
    q.addQueryItem(QStringLiteral("format"), QStringLiteral("json"));
    url.setQuery(q);

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("kdock-weather/1.0"));
    req.setTransferTimeout(kTimeoutMs);

    m_searchReply = m_net->get(req);
    connect(m_searchReply, &QNetworkReply::finished, this, [this] {
        QNetworkReply *reply = m_searchReply;
        m_searchReply = nullptr;
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit searchFailed(reply->errorString());
            return;
        }
        const QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
        QVariantList out;
        for (const QJsonValue &v : root.value(QStringLiteral("results")).toArray()) {
            const QJsonObject o = v.toObject();
            QVariantMap city;
            city[QStringLiteral("name")] = o.value(QStringLiteral("name")).toString();
            city[QStringLiteral("admin1")] = o.value(QStringLiteral("admin1")).toString();
            city[QStringLiteral("country")] = o.value(QStringLiteral("country")).toString();
            city[QStringLiteral("countryCode")] = o.value(QStringLiteral("country_code")).toString();
            city[QStringLiteral("lat")] = o.value(QStringLiteral("latitude")).toDouble();
            city[QStringLiteral("lon")] = o.value(QStringLiteral("longitude")).toDouble();
            city[QStringLiteral("timezone")] = o.value(QStringLiteral("timezone")).toString();
            out.append(city);
        }
        emit citiesFound(out);
    });
}

// ------------------------------------------------------------------ cache --

bool WeatherControl::loadCache()
{
    QFile f(WeatherConfig::cacheFilePath());
    if (!f.open(QIODevice::ReadOnly))
        return false;
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    const QVariantMap city = m_config ? m_config->city() : QVariantMap();
    if (city.isEmpty())
        return false;
    // A cache for another city is not this city's weather.
    if (qAbs(jnum(root, "lat") - city.value(QStringLiteral("lat")).toDouble()) > 0.01
        || qAbs(jnum(root, "lon") - city.value(QStringLiteral("lon")).toDouble()) > 0.01)
        return false;

    m_lat = jnum(root, "lat");
    m_lon = jnum(root, "lon");
    m_fetched = QDateTime::fromSecsSinceEpoch(qint64(jnum(root, "fetched")));
    m_data = root.value(QStringLiteral("data")).toObject();
    return !m_data.isEmpty();
}

void WeatherControl::saveCache()
{
    QJsonObject root;
    root[QStringLiteral("lat")] = m_lat;
    root[QStringLiteral("lon")] = m_lon;
    root[QStringLiteral("fetched")] = double(m_fetched.toSecsSinceEpoch());
    root[QStringLiteral("data")] = m_data;
    QFile f(WeatherConfig::cacheFilePath());
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

// --------------------------------------------------------------- mapping --

QString WeatherControl::iconForCode(int code, bool day)
{
    const QString night = day ? QString() : QStringLiteral("-night");
    switch (code) {
    case 0:  return QStringLiteral("weather-clear") + night;
    case 1:  return QStringLiteral("weather-few-clouds") + night;
    case 2:  return QStringLiteral("weather-clouds") + night;
    case 3:  return QStringLiteral("weather-many-clouds");
    case 45:
    case 48: return QStringLiteral("weather-fog");
    case 51:
    case 53:
    case 55: return QStringLiteral("weather-showers-scattered");
    case 56:
    case 57: return QStringLiteral("weather-freezing-rain");
    case 61:
    case 63:
    case 65: return QStringLiteral("weather-showers");
    case 66:
    case 67: return QStringLiteral("weather-freezing-rain");
    case 71:
    case 73:
    case 75: return QStringLiteral("weather-snow");
    case 77: return QStringLiteral("weather-snow-scattered");
    case 80:
    case 81: return QStringLiteral("weather-showers-scattered");
    case 82: return QStringLiteral("weather-showers");
    case 85:
    case 86: return QStringLiteral("weather-snow");
    case 95: return QStringLiteral("weather-storm");
    case 96:
    case 99: return QStringLiteral("weather-hail");
    default: break;
    }
    return QStringLiteral("weather-none-available");
}

QString WeatherControl::textForCode(int code)
{
    switch (code) {
    case 0:  return tr("Despejado");
    case 1:  return tr("Mayormente despejado");
    case 2:  return tr("Parcialmente nublado");
    case 3:  return tr("Nublado");
    case 45: return tr("Niebla");
    case 48: return tr("Niebla con escarcha");
    case 51: return tr("Llovizna débil");
    case 53: return tr("Llovizna");
    case 55: return tr("Llovizna intensa");
    case 56:
    case 57: return tr("Llovizna helada");
    case 61: return tr("Lluvia débil");
    case 63: return tr("Lluvia");
    case 65: return tr("Lluvia intensa");
    case 66:
    case 67: return tr("Lluvia helada");
    case 71: return tr("Nevada débil");
    case 73: return tr("Nevada");
    case 75: return tr("Nevada intensa");
    case 77: return tr("Granos de nieve");
    case 80: return tr("Chaparrones aislados");
    case 81: return tr("Chaparrones");
    case 82: return tr("Chaparrones fuertes");
    case 85:
    case 86: return tr("Chaparrones de nieve");
    case 95: return tr("Tormenta");
    case 96:
    case 99: return tr("Tormenta con granizo");
    default: break;
    }
    return tr("Sin datos");
}

// ------------------------------------------------------------ formatting --

QString WeatherControl::formatTemp(double celsius, bool withUnit) const
{
    const bool f = m_config && m_config->fahrenheit();
    const double value = f ? celsius * 9.0 / 5.0 + 32.0 : celsius;
    const QString number = QLocale::system().toString(qRound(value));
    if (!withUnit)
        return number + QStringLiteral("°");
    return number + (f ? QStringLiteral(" °F") : QStringLiteral(" °C"));
}

QString WeatherControl::formatWind(double kmh) const
{
    const int unit = m_config ? m_config->windUnit() : WeatherConfig::MetersPerSecond;
    switch (unit) {
    case WeatherConfig::KilometersPerHour:
        return QLocale::system().toString(kmh, 'f', 1) + QStringLiteral(" km/h");
    case WeatherConfig::MilesPerHour:
        return QLocale::system().toString(kmh / 1.609344, 'f', 1) + QStringLiteral(" mph");
    default:
        return QLocale::system().toString(kmh / 3.6, 'f', 1) + QStringLiteral(" m/s");
    }
}

// -------------------------------------------------------------- accessors --

QString WeatherControl::cityLabel() const
{
    return m_config ? WeatherConfig::cityLabel(m_config->city()) : QString();
}

QString WeatherControl::tempText() const
{
    if (m_data.isEmpty())
        return {};
    return formatTemp(jnum(m_data.value(QStringLiteral("current")).toObject(), "temperature_2m"));
}

QString WeatherControl::feelsLikeText() const
{
    if (m_data.isEmpty())
        return {};
    const QJsonObject cur = m_data.value(QStringLiteral("current")).toObject();
    return formatTemp(jnum(cur, "apparent_temperature"));
}

QString WeatherControl::conditionText() const
{
    if (m_data.isEmpty())
        return {};
    const QJsonObject cur = m_data.value(QStringLiteral("current")).toObject();
    return textForCode(int(jnum(cur, "weather_code", -1)));
}

QString WeatherControl::iconName() const
{
    if (m_data.isEmpty())
        return QStringLiteral("weather-none-available");
    const QJsonObject cur = m_data.value(QStringLiteral("current")).toObject();
    return iconForCode(int(jnum(cur, "weather_code", -1)), jnum(cur, "is_day", 1) > 0.5);
}

QString WeatherControl::windText() const
{
    if (m_data.isEmpty())
        return {};
    return formatWind(jnum(m_data.value(QStringLiteral("current")).toObject(), "wind_speed_10m"));
}

int WeatherControl::windDirection() const
{
    if (m_data.isEmpty())
        return 0;
    return int(jnum(m_data.value(QStringLiteral("current")).toObject(), "wind_direction_10m"));
}

QString WeatherControl::updatedText() const
{
    if (!m_fetched.isValid())
        return {};
    return QLocale::system().toString(m_fetched.time(), QLocale::ShortFormat);
}

QVariantList WeatherControl::forecast() const
{
    QVariantList out;
    if (m_data.isEmpty())
        return out;

    const QJsonArray days = jarr(m_data, "daily", "time");
    const QJsonArray codes = jarr(m_data, "daily", "weather_code");
    const QJsonArray maxs = jarr(m_data, "daily", "temperature_2m_max");
    const QJsonArray mins = jarr(m_data, "daily", "temperature_2m_min");
    const QJsonArray precip = jarr(m_data, "daily", "precipitation_probability_max");

    const QLocale locale = QLocale::system();
    const QDate today = QDate::currentDate();
    for (int i = 0; i < days.size(); ++i) {
        const QDate date = QDate::fromString(days.at(i).toString(), Qt::ISODate);
        QVariantMap day;
        day[QStringLiteral("dayLabel")] = date == today
                                              ? tr("Hoy")
                                              : locale.dayName(date.dayOfWeek(), QLocale::ShortFormat);
        day[QStringLiteral("dateLabel")] = locale.toString(date, QStringLiteral("d MMM"));
        const int code = i < codes.size() ? int(codes.at(i).toDouble(-1)) : -1;
        // A forecast day is drawn with its daytime icon: the whole day is the
        // unit, not the moment it is being looked at.
        day[QStringLiteral("iconName")] = iconForCode(code, true);
        day[QStringLiteral("conditionText")] = textForCode(code);
        day[QStringLiteral("precipProbability")] =
            i < precip.size() && !precip.at(i).isNull() ? int(precip.at(i).toDouble()) : -1;
        day[QStringLiteral("maxText")] =
            i < maxs.size() ? formatTemp(maxs.at(i).toDouble(), false) : QString();
        day[QStringLiteral("minText")] =
            i < mins.size() ? formatTemp(mins.at(i).toDouble(), false) : QString();
        out.append(day);
    }
    return out;
}

QVariantList WeatherControl::details() const
{
    QVariantList out;
    if (m_data.isEmpty())
        return out;
    const QJsonObject cur = m_data.value(QStringLiteral("current")).toObject();
    const QLocale locale = QLocale::system();

    const auto row = [&out](const QString &label, const QString &text) {
        if (text.isEmpty())
            return;
        QVariantMap m;
        m[QStringLiteral("label")] = label;
        m[QStringLiteral("text")] = text;
        out.append(m);
    };

    row(tr("Sensación térmica"), feelsLikeText());
    row(tr("Punto de rocío"), formatTemp(jnum(cur, "dew_point_2m")));
    row(tr("Humedad"), locale.toString(int(jnum(cur, "relative_humidity_2m")))
                           + QStringLiteral(" %"));
    row(tr("Presión"), locale.toString(jnum(cur, "pressure_msl"), 'f', 1)
                           + QStringLiteral(" hPa"));
    row(tr("Ráfaga"), formatWind(jnum(cur, "wind_gusts_10m")));

    // Visibility is hourly-only in this API, so the row is the value for the
    // hour the observation belongs to (matched by timestamp, not by position:
    // the hourly array starts at midnight *local*).
    const QJsonArray times = jarr(m_data, "hourly", "time");
    const QJsonArray vis = jarr(m_data, "hourly", "visibility");
    const QString hour = cur.value(QStringLiteral("time")).toString().left(13);
    for (int i = 0; i < times.size() && i < vis.size(); ++i) {
        if (!times.at(i).toString().startsWith(hour))
            continue;
        const double metres = vis.at(i).toDouble();
        row(tr("Visibilidad"), metres >= 1000
                                   ? locale.toString(metres / 1000.0, 'f', 1) + QStringLiteral(" km")
                                   : locale.toString(int(metres)) + QStringLiteral(" m"));
        break;
    }

    // Sunrise/sunset are not in the KDE applet's Details, but they are the two
    // numbers people look for right after the temperature.
    const QJsonArray sunrise = jarr(m_data, "daily", "sunrise");
    const QJsonArray sunset = jarr(m_data, "daily", "sunset");
    const auto timeOf = [&locale](const QJsonArray &a) {
        if (a.isEmpty())
            return QString();
        const QDateTime dt = QDateTime::fromString(a.at(0).toString(), Qt::ISODate);
        return dt.isValid() ? locale.toString(dt.time(), QLocale::ShortFormat) : QString();
    };
    row(tr("Amanecer"), timeOf(sunrise));
    row(tr("Atardecer"), timeOf(sunset));

    return out;
}

QString WeatherControl::dump() const
{
    QString out;
    if (!configured())
        return QStringLiteral("(sin ciudad configurada: kdock-weather --settings)\n");

    out += QStringLiteral("== %1 ==\n").arg(cityLabel());
    if (m_data.isEmpty()) {
        out += m_error.isEmpty() ? QStringLiteral("  (sin datos todavía)\n")
                                 : QStringLiteral("  error: %1\n").arg(m_error);
        return out;
    }

    out += QStringLiteral("  %1  %2   viento %3 desde %4°   (actualizado %5%6)\n")
               .arg(tempText(), conditionText(), windText())
               .arg(windDirection())
               .arg(updatedText(), stale() ? QStringLiteral(", vencido") : QString());
    out += QStringLiteral("  ícono: %1\n\n").arg(iconName());

    out += QStringLiteral("== pronóstico ==\n");
    for (const QVariant &v : forecast()) {
        const QVariantMap d = v.toMap();
        out += QStringLiteral("  %1  %2  %3  %4/%5  %6\n")
                   .arg(d.value(QStringLiteral("dayLabel")).toString().leftJustified(6),
                        d.value(QStringLiteral("dateLabel")).toString().leftJustified(8))
                   .arg(d.value(QStringLiteral("precipProbability")).toInt() >= 0
                            ? QStringLiteral("%1 %").arg(
                                  d.value(QStringLiteral("precipProbability")).toInt(), 3)
                            : QStringLiteral("   -"))
                   .arg(d.value(QStringLiteral("maxText")).toString(),
                        d.value(QStringLiteral("minText")).toString(),
                        d.value(QStringLiteral("conditionText")).toString());
    }

    out += QStringLiteral("\n== detalles ==\n");
    for (const QVariant &v : details()) {
        const QVariantMap d = v.toMap();
        out += QStringLiteral("  %1 %2\n")
                   .arg(d.value(QStringLiteral("label")).toString().leftJustified(20),
                        d.value(QStringLiteral("text")).toString());
    }
    return out;
}
