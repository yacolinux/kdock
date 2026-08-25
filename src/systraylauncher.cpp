#include "systraylauncher.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QFileInfo>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>

namespace {
const auto kBinary = QStringLiteral("kdock-systray");
const auto kService = QStringLiteral("org.kdock.Systray");
const auto kPath = QStringLiteral("/Systray");

QString settingsFilePath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
           + QStringLiteral("/kdock/systray.conf");
}

void callSystray(const QString &method, const QVariantList &args = {})
{
    QDBusMessage msg = QDBusMessage::createMethodCall(kService, kPath, kService, method);
    if (!args.isEmpty())
        msg.setArguments(args);
    // asyncCall: a wedged instance must not block the dock for the D-Bus default
    // of 25 s.
    QDBusConnection::sessionBus().asyncCall(msg);
}
} // namespace

QString SystrayLauncher::binaryPath()
{
    // Next to kdock itself (the installed case), then the build tree layout
    // (build/kdock + build/systray/kdock-systray), then $PATH.
    const QString dir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        dir + QLatin1Char('/') + kBinary,
        dir + QStringLiteral("/systray/") + kBinary,
    };
    for (const QString &candidate : candidates) {
        const QFileInfo info(candidate);
        if (info.isFile() && info.isExecutable())
            return info.absoluteFilePath();
    }
    return QStandardPaths::findExecutable(kBinary);
}

bool SystrayLauncher::installed()
{
    return !binaryPath().isEmpty();
}

bool SystrayLauncher::running()
{
    auto *iface = QDBusConnection::sessionBus().interface();
    return iface && iface->isServiceRegistered(kService);
}

bool SystrayLauncher::preload()
{
    QSettings s(settingsFilePath(), QSettings::IniFormat);
    return s.value(QStringLiteral("preload"), true).toBool();
}

void SystrayLauncher::setPreload(bool on)
{
    QSettings s(settingsFilePath(), QSettings::IniFormat);
    s.setValue(QStringLiteral("preload"), on);
}

bool SystrayLauncher::start(const QStringList &args)
{
    const QString binary = binaryPath();
    if (binary.isEmpty())
        return false;
    // startDetached: the tray is the session's, not a child of this dock, and it
    // has to survive a dock restart. The child inherits an environment main.cpp
    // already cleaned of QT_WAYLAND_SHELL_INTEGRATION.
    return QProcess::startDetached(binary, args);
}

void SystrayLauncher::toggle(const QString &screenName)
{
    if (running()) {
        callSystray(QStringLiteral("toggle"), {screenName});
        return;
    }
    QStringList args{QStringLiteral("--toggle")};
    if (!screenName.isEmpty())
        args << QStringLiteral("--screen") << screenName;
    start(args);
}

void SystrayLauncher::openSettings()
{
    if (running())
        callSystray(QStringLiteral("showSettings"));
    else
        start({QStringLiteral("--settings")});
}

void SystrayLauncher::startIfPreloading()
{
    // --hide: come up resident with nothing on screen, so it is the session's SNI
    // watcher/host from the start (before the tray clients register) and the
    // first click is instant.
    if (preload() && !running())
        start({QStringLiteral("--hide")});
}

void SystrayLauncher::quitRunning()
{
    if (running())
        callSystray(QStringLiteral("quit"));
}
