// kdock's whole involvement with the weather window: find the binary, toggle it
// and open its settings.
//
// kdock-weather is a separate process with its own config (weather.conf) and its
// own settings dialog, same split as the other accessories. Unlike them it is
// **not resident**: closing its window quits it, so there is nothing to preload
// and nothing for kdock::restartAll() to shut down after an install.
//
// The dock still shows the temperature by itself: the data comes from the
// WeatherControl this process owns, not from the other binary.

#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

class WeatherLauncher : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;

    // Show the window, or close it when it is already up. Starts the process the
    // first time, which is what the widget's first click does.
    Q_INVOKABLE void toggle(const QString &screenName = QString());
    Q_INVOKABLE void openSettings();

    static QString binaryPath();
    static bool installed();
    static bool running();

private:
    static bool start(const QStringList &args = {});
};
