// kdock's small bridge to kdock-clipboard. The clipboard history itself is
// deliberately not linked into kdock: a broken history UI or clipboard offer
// must not be able to take the dock down with it.

#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

class ClipboardLauncher : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;

    // WeatherLauncher-style toggle. The standalone window is a normal toplevel
    // and lets the compositor place it, as it did before anchor hints existed.
    Q_INVOKABLE void toggle(const QString &screenName = QString());

    static QString binaryPath();
    static bool installed();
    static bool running();
    static void quitRunning();

private:
    static bool start(const QStringList &args = {});
};
