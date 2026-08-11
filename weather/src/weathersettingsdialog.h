// The weather's own settings panel (Qt Widgets), opened from the window's
// button, from the dock widget's right click, or with --settings.
//
// Same split as the other accessories: kdock only knows whether to show the
// widget; the cities and the units are configured here, by the process that
// owns them. The city search goes through the provider's geocoding endpoint
// (WeatherControl::searchCity), so no list of places ships with kdock.

#pragma once

#include <QDialog>
#include <QVariantList>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;
class WeatherConfig;
class WeatherControl;

class WeatherSettingsDialog : public QDialog
{
    Q_OBJECT
public:
    WeatherSettingsDialog(WeatherConfig *config, WeatherControl *weather,
                          QWidget *parent = nullptr);

private:
    void reloadCities();
    void onSearchResults(const QVariantList &cities);

    WeatherConfig *m_config;
    WeatherControl *m_weather;

    QListWidget *m_cities = nullptr;
    QLineEdit *m_search = nullptr;
    QListWidget *m_results = nullptr;
    QLabel *m_searchStatus = nullptr;
    QPushButton *m_addButton = nullptr;
    // The results of the last search, in the same order as m_results.
    QVariantList m_found;
};
