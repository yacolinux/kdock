#include "weatherconfig.h"

#include <QDir>
#include <QFileSystemWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>

namespace {

QString configDir()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
                        + QStringLiteral("/kdock");
    QDir().mkpath(dir);
    return dir;
}

} // namespace

QString WeatherConfig::settingsFilePath()
{
    return configDir() + QStringLiteral("/weather.conf");
}

QString WeatherConfig::cacheFilePath()
{
    return configDir() + QStringLiteral("/weather-cache.json");
}

WeatherConfig::WeatherConfig(QObject *parent)
    : QObject(parent)
{
    load();

    m_watcher = new QFileSystemWatcher(this);
    // Watch the directory too: the file may not exist yet (nothing configured),
    // and QSettings replaces it on every save, which drops a file-only watch.
    m_watcher->addPath(configDir());
    if (QFile::exists(settingsFilePath()))
        m_watcher->addPath(settingsFilePath());
    connect(m_watcher, &QFileSystemWatcher::fileChanged, this, &WeatherConfig::onFileChanged);
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, &WeatherConfig::onFileChanged);
}

void WeatherConfig::onFileChanged()
{
    if (m_writing)
        return;
    // The write is not atomic from the watcher's point of view (truncate, then
    // fill), so a read fired on the first notification can see an empty file.
    QTimer::singleShot(120, this, [this] {
        if (!m_watcher->files().contains(settingsFilePath()) && QFile::exists(settingsFilePath()))
            m_watcher->addPath(settingsFilePath());
        const QVariantList before = m_cities;
        const int city = m_activeCity;
        const bool f = m_fahrenheit;
        const int wind = m_windUnit, mins = m_refreshMinutes, days = m_forecastDays;
        load();
        if (before != m_cities || city != m_activeCity || f != m_fahrenheit
            || wind != m_windUnit || mins != m_refreshMinutes || days != m_forecastDays)
            emit changed();
    });
}

void WeatherConfig::load()
{
    QSettings s(settingsFilePath(), QSettings::IniFormat);

    // The city list is one JSON string and not a group per city on purpose:
    // QSettings maps only the FIRST '/' of a key to a section, so a key built
    // from data (a city name, with its commas and accents) lands in the escaping
    // rules instead of in a section. See CLAUDE.md.
    m_cities.clear();
    const QJsonDocument doc =
        QJsonDocument::fromJson(s.value(QStringLiteral("cities")).toString().toUtf8());
    for (const QJsonValue &v : doc.array())
        m_cities.append(v.toObject().toVariantMap());

    m_activeCity = qBound(0, s.value(QStringLiteral("activeCity"), 0).toInt(),
                          qMax(0, int(m_cities.size()) - 1));
    m_fahrenheit = s.value(QStringLiteral("fahrenheit"), false).toBool();
    m_windUnit = qBound(0, s.value(QStringLiteral("windUnit"), int(MetersPerSecond)).toInt(), 2);
    m_refreshMinutes = qBound(10, s.value(QStringLiteral("refreshMinutes"), 30).toInt(), 720);
    m_forecastDays = qBound(3, s.value(QStringLiteral("forecastDays"), 7).toInt(), 16);
}

void WeatherConfig::save()
{
    m_writing = true;
    {
        QSettings s(settingsFilePath(), QSettings::IniFormat);
        QJsonArray arr;
        for (const QVariant &v : std::as_const(m_cities))
            arr.append(QJsonObject::fromVariantMap(v.toMap()));
        s.setValue(QStringLiteral("cities"),
                   QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
        s.setValue(QStringLiteral("activeCity"), m_activeCity);
        s.setValue(QStringLiteral("fahrenheit"), m_fahrenheit);
        s.setValue(QStringLiteral("windUnit"), m_windUnit);
        s.setValue(QStringLiteral("refreshMinutes"), m_refreshMinutes);
        s.setValue(QStringLiteral("forecastDays"), m_forecastDays);
    }
    // Long enough for our own write to land and its notification to arrive, so
    // this process does not re-read what it just wrote.
    QTimer::singleShot(250, this, [this] { m_writing = false; });
    emit changed();
}

QVariantMap WeatherConfig::city() const
{
    if (m_activeCity < 0 || m_activeCity >= m_cities.size())
        return {};
    return m_cities.at(m_activeCity).toMap();
}

QString WeatherConfig::cityLabel(const QVariantMap &city)
{
    if (city.isEmpty())
        return {};
    QStringList parts;
    parts << city.value(QStringLiteral("name")).toString();
    const QString admin = city.value(QStringLiteral("admin1")).toString();
    if (!admin.isEmpty())
        parts << admin;
    const QString country = city.value(QStringLiteral("countryCode")).toString();
    if (!country.isEmpty())
        parts << country;
    return parts.join(QStringLiteral(", "));
}

void WeatherConfig::setActiveCity(int index)
{
    if (index < 0 || index >= m_cities.size() || index == m_activeCity)
        return;
    m_activeCity = index;
    save();
}

void WeatherConfig::setFahrenheit(bool on)
{
    if (on == m_fahrenheit)
        return;
    m_fahrenheit = on;
    save();
}

void WeatherConfig::setWindUnit(int unit)
{
    unit = qBound(0, unit, 2);
    if (unit == m_windUnit)
        return;
    m_windUnit = unit;
    save();
}

void WeatherConfig::setRefreshMinutes(int minutes)
{
    minutes = qBound(10, minutes, 720);
    if (minutes == m_refreshMinutes)
        return;
    m_refreshMinutes = minutes;
    save();
}

void WeatherConfig::setForecastDays(int days)
{
    days = qBound(3, days, 16);
    if (days == m_forecastDays)
        return;
    m_forecastDays = days;
    save();
}

void WeatherConfig::addCity(const QVariantMap &city)
{
    if (city.isEmpty() || !city.contains(QStringLiteral("lat")))
        return;
    // Deduplicate by coordinates: the same place comes back from the geocoder
    // with slightly different labels depending on the query language.
    for (int i = 0; i < m_cities.size(); ++i) {
        const QVariantMap c = m_cities.at(i).toMap();
        if (qAbs(c.value(QStringLiteral("lat")).toDouble()
                 - city.value(QStringLiteral("lat")).toDouble()) < 0.01
            && qAbs(c.value(QStringLiteral("lon")).toDouble()
                    - city.value(QStringLiteral("lon")).toDouble()) < 0.01) {
            m_activeCity = i;
            save();
            return;
        }
    }
    m_cities.append(city);
    m_activeCity = int(m_cities.size()) - 1;
    save();
}

void WeatherConfig::removeCity(int index)
{
    if (index < 0 || index >= m_cities.size())
        return;
    m_cities.removeAt(index);
    m_activeCity = qBound(0, m_activeCity > index ? m_activeCity - 1 : m_activeCity,
                          qMax(0, int(m_cities.size()) - 1));
    save();
}
