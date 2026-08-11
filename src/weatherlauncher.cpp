#include "weatherlauncher.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

namespace {
const auto kBinary = QStringLiteral("kdock-weather");
const auto kService = QStringLiteral("org.kdock.Weather");
const auto kPath = QStringLiteral("/Weather");

void callWeather(const QString &method, const QVariantList &args = {})
{
    QDBusMessage msg = QDBusMessage::createMethodCall(kService, kPath, kService, method);
    if (!args.isEmpty())
        msg.setArguments(args);
    // asyncCall: a wedged instance must not block the dock for the D-Bus default
    // of 25 s.
    QDBusConnection::sessionBus().asyncCall(msg);
}
} // namespace

QString WeatherLauncher::binaryPath()
{
    // Next to kdock itself (the installed case), then the build tree layout
    // (build/kdock + build/weather/kdock-weather), then $PATH.
    const QString dir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        dir + QLatin1Char('/') + kBinary,
        dir + QStringLiteral("/weather/") + kBinary,
    };
    for (const QString &candidate : candidates) {
        const QFileInfo info(candidate);
        if (info.isFile() && info.isExecutable())
            return info.absoluteFilePath();
    }
    return QStandardPaths::findExecutable(kBinary);
}

bool WeatherLauncher::installed()
{
    return !binaryPath().isEmpty();
}

bool WeatherLauncher::running()
{
    auto *iface = QDBusConnection::sessionBus().interface();
    return iface && iface->isServiceRegistered(kService);
}

bool WeatherLauncher::start(const QStringList &args)
{
    const QString binary = binaryPath();
    if (binary.isEmpty())
        return false;
    // startDetached: the window is the user's, not a child of this dock, and it
    // has to survive a dock restart while it is open. The child inherits an
    // environment main.cpp already cleaned of QT_WAYLAND_SHELL_INTEGRATION.
    return QProcess::startDetached(binary, args);
}

void WeatherLauncher::toggle(const QString &screenName)
{
    if (running()) {
        callWeather(QStringLiteral("toggle"), {screenName});
        return;
    }
    QStringList args{QStringLiteral("--toggle")};
    if (!screenName.isEmpty())
        args << QStringLiteral("--screen") << screenName;
    start(args);
}

void WeatherLauncher::openSettings()
{
    if (running())
        callWeather(QStringLiteral("showSettings"));
    else
        start({QStringLiteral("--settings")});
}
