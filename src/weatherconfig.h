// Everything the weather knows about itself: the saved cities, which one is
// active, the display units and how often to refresh.
//
// Its own file (`weather.conf`, next to previews.conf / tilemenu.conf /
// controlmanager.conf) rather than a group in the shared kdock.conf, for the
// same reason the other accessories have one: the settings belong to
// kdock-weather, which owns their dialog, and the dock only reads them.
//
// **Three processes read this file and none of them tells the others**
// (kdock, kdock-controlmanager and kdock-weather each build their own
// WeatherControl), so the class watches the file and emits changed(). Without
// that, changing city in the mini-app would not reach the dock until it
// restarts. A QFileSystemWatcher is enough here and keeps the binaries from
// having to know about each other.

#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

class QFileSystemWatcher;

class WeatherConfig : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList cities READ cities NOTIFY changed)
    Q_PROPERTY(int activeCity READ activeCity WRITE setActiveCity NOTIFY changed)
    Q_PROPERTY(bool fahrenheit READ fahrenheit WRITE setFahrenheit NOTIFY changed)
    Q_PROPERTY(int windUnit READ windUnit WRITE setWindUnit NOTIFY changed)
    Q_PROPERTY(int refreshMinutes READ refreshMinutes WRITE setRefreshMinutes NOTIFY changed)
    Q_PROPERTY(int forecastDays READ forecastDays WRITE setForecastDays NOTIFY changed)
    // Every text of the weather window, scaled by this. 0 = the historic fixed
    // sizes, which is what fontScale() reports as 1.0 — same arrangement (and
    // the same base of 13 px) as the control panel's fontSize.
    Q_PROPERTY(int fontSize READ fontSize WRITE setFontSize NOTIFY changed)
    Q_PROPERTY(qreal fontScale READ fontScale NOTIFY changed)

public:
    // Wind is reported by the provider in km/h; these are display units only.
    enum WindUnit { MetersPerSecond = 0, KilometersPerHour = 1, MilesPerHour = 2 };
    Q_ENUM(WindUnit)

    static QString settingsFilePath();
    // Where the shared response cache lives (same directory).
    static QString cacheFilePath();

    explicit WeatherConfig(QObject *parent = nullptr);

    // One entry per saved city: name, admin1, country, countryCode, lat, lon,
    // timezone. The label the UI shows is cityLabel() below.
    QVariantList cities() const { return m_cities; }
    int activeCity() const { return m_activeCity; }
    bool fahrenheit() const { return m_fahrenheit; }
    int windUnit() const { return m_windUnit; }
    int refreshMinutes() const { return m_refreshMinutes; }
    int forecastDays() const { return m_forecastDays; }
    int fontSize() const { return m_fontSize; }
    qreal fontScale() const { return m_fontSize > 0 ? m_fontSize / 13.0 : 1.0; }

    // The active city, or an empty map when nothing is configured yet.
    QVariantMap city() const;
    // "Ciudad de Corrientes, Provincia de Corrientes, AR", the way the KDE
    // applet titles its panel.
    static QString cityLabel(const QVariantMap &city);

    Q_INVOKABLE void setActiveCity(int index);
    Q_INVOKABLE void setFahrenheit(bool on);
    Q_INVOKABLE void setWindUnit(int unit);
    Q_INVOKABLE void setRefreshMinutes(int minutes);
    Q_INVOKABLE void setForecastDays(int days);
    Q_INVOKABLE void setFontSize(int px);
    // Appends (deduplicated by coordinates) and makes it active.
    Q_INVOKABLE void addCity(const QVariantMap &city);
    Q_INVOKABLE void removeCity(int index);

signals:
    void changed();

private:
    void load();
    void save();
    // Re-reads after someone else wrote the file, and re-arms the watch: an
    // editor that replaces the file (QSettings does) drops the watch with it.
    void onFileChanged();

    QVariantList m_cities;
    int m_activeCity = 0;
    bool m_fahrenheit = false;
    int m_windUnit = MetersPerSecond;
    int m_refreshMinutes = 30;
    int m_forecastDays = 7;
    int m_fontSize = 0;
    bool m_writing = false;
    QFileSystemWatcher *m_watcher = nullptr;
};
