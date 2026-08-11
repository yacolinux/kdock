// Current conditions and forecast for the active city, exposed to QML as
// "weather". Shared by the dock widget, the control panel's Clima section and
// the kdock-weather window, which is why it lives in src/ and is compiled into
// the three binaries — same arrangement as NetworkControl.
//
// The provider is **Open-Meteo** (api.open-meteo.com): plain HTTPS + JSON, no
// API key and no registration, plus a geocoding endpoint that turns a typed city
// name into coordinates. KDE's own weather applet cannot be reused here: its
// data comes from the Plasma weather *ions*, which are KDE Frameworks, and this
// project links none.
//
// Everything is asked for in the provider's own units (°C and km/h) and
// converted for display, so the cache does not depend on the user's choice of
// units: changing from m/s to mph is a repaint, not a request.
//
// Test seam, off unless set: **KDOCK_WEATHER_FIXTURE** points at a recorded
// forecast response, which is used instead of the network (and neither the cache
// nor the refresh timer are touched). It is what makes the parsing, the WMO code
// mapping and the formatting testable without internet — CI has none.

#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QVariantList>

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;
class WeatherConfig;

class WeatherControl : public QObject
{
    Q_OBJECT
    // A city is configured (without one there is nothing to show but an invitation
    // to pick one).
    Q_PROPERTY(bool configured READ configured NOTIFY changed)
    // …and there is data to draw, from the network or from the cache.
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(bool loading READ loading NOTIFY changed)
    // True while what is on screen comes from a cache older than the refresh
    // interval: the UI dims it instead of blanking, which is what makes a
    // network outage readable rather than looking broken.
    Q_PROPERTY(bool stale READ stale NOTIFY changed)
    Q_PROPERTY(QString errorText READ errorText NOTIFY changed)

    Q_PROPERTY(QString cityLabel READ cityLabel NOTIFY changed)
    Q_PROPERTY(QString tempText READ tempText NOTIFY changed)
    Q_PROPERTY(QString feelsLikeText READ feelsLikeText NOTIFY changed)
    Q_PROPERTY(QString conditionText READ conditionText NOTIFY changed)
    Q_PROPERTY(QString iconName READ iconName NOTIFY changed)
    Q_PROPERTY(QString windText READ windText NOTIFY changed)
    // Degrees the wind comes FROM; the arrow points the other way.
    Q_PROPERTY(int windDirection READ windDirection NOTIFY changed)
    Q_PROPERTY(QString updatedText READ updatedText NOTIFY changed)

public:
    explicit WeatherControl(WeatherConfig *config, QObject *parent = nullptr);

    bool configured() const;
    bool available() const { return !m_data.isEmpty(); }
    bool loading() const { return m_loading; }
    bool stale() const;
    QString errorText() const { return m_error; }

    QString cityLabel() const;
    QString tempText() const;
    QString feelsLikeText() const;
    QString conditionText() const;
    QString iconName() const;
    QString windText() const;
    int windDirection() const;
    QString updatedText() const;

    // One map per day: dayLabel, dateLabel, iconName, conditionText,
    // precipProbability (int, -1 when unknown), maxText, minText.
    Q_INVOKABLE QVariantList forecast() const;
    // The rows of the Detalles view: label + text. Same six the KDE applet
    // shows (dew point, pressure, visibility, humidity, gust).
    Q_INVOKABLE QVariantList details() const;

    // force = ignore the cache and ask now (the Actualizar button).
    Q_INVOKABLE void refresh(bool force = false);
    // Geocoding for the settings dialog: answers with citiesFound().
    Q_INVOKABLE void searchCity(const QString &name);

    // Everything above as text, for kdock-weather --dump.
    QString dump() const;

    // --- pure helpers, unit-testable without a network or a config ----------
    // WMO weather code -> themed icon name. Breeze has day/night variants for
    // the clear/cloudy end of the table only.
    static QString iconForCode(int code, bool day);
    static QString textForCode(int code);

signals:
    void changed();
    // [{ name, admin1, country, countryCode, lat, lon, timezone }]
    void citiesFound(const QVariantList &cities);
    void searchFailed(const QString &message);

private:
    void onConfigChanged();
    void applyResponse(const QJsonObject &data, const QDateTime &fetched);
    bool loadCache();
    void saveCache();
    void scheduleNext(bool afterFailure);
    // Formatting, all of it dependent on the configured units.
    QString formatTemp(double celsius, bool withUnit = true) const;
    QString formatWind(double kmh) const;

    WeatherConfig *m_config;
    QNetworkAccessManager *m_net = nullptr;
    QNetworkReply *m_reply = nullptr;
    QNetworkReply *m_searchReply = nullptr;
    QTimer *m_timer = nullptr;

    QJsonObject m_data;      // the provider's response, as received
    QDateTime m_fetched;     // when it was received
    double m_lat = 0, m_lon = 0; // coordinates m_data belongs to
    bool m_loading = false;
    QString m_error;
    // Grows 1, 2, 4… refresh intervals while the network keeps failing; a
    // provider that is down must not turn into one request per second.
    int m_failures = 0;
};
