#include "desktoplauncher.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QFileInfo>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>

namespace {
const auto kBinary = QStringLiteral("kdock-desktop");
const auto kService = QStringLiteral("org.kdock.Desktop");
const auto kPath = QStringLiteral("/Desktop");

QString settingsFilePath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
           + QStringLiteral("/kdock/desktop.conf");
}

void callCanvas(const QString &method, const QVariantList &args = {})
{
    QDBusMessage msg = QDBusMessage::createMethodCall(kService, kPath, kService, method);
    if (!args.isEmpty())
        msg.setArguments(args);
    // asyncCall: a wedged instance must not block the dock for the D-Bus default
    // of 25 s.
    QDBusConnection::sessionBus().asyncCall(msg);
}
} // namespace

QString DesktopLauncher::binaryPath()
{
    // Next to kdock itself (the installed case), then the build tree layout
    // (build/kdock + build/desktop/kdock-desktop), then $PATH.
    const QString dir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        dir + QLatin1Char('/') + kBinary,
        dir + QStringLiteral("/desktop/") + kBinary,
    };
    for (const QString &candidate : candidates) {
        const QFileInfo info(candidate);
        if (info.isFile() && info.isExecutable())
            return info.absoluteFilePath();
    }
    return QStandardPaths::findExecutable(kBinary);
}

bool DesktopLauncher::installed()
{
    return !binaryPath().isEmpty();
}

bool DesktopLauncher::running()
{
    auto *iface = QDBusConnection::sessionBus().interface();
    return iface && iface->isServiceRegistered(kService);
}

bool DesktopLauncher::start(const QStringList &args)
{
    const QString binary = binaryPath();
    if (binary.isEmpty())
        return false;
    // startDetached so the canvas outlives a kdock restart. The child gets this
    // process' environment, which main.cpp has already cleaned of
    // QT_WAYLAND_SHELL_INTEGRATION (see the note there).
    return QProcess::startDetached(binary, args);
}

void DesktopLauncher::openSettings()
{
    if (running())
        callCanvas(QStringLiteral("showSettings"));
    else
        start({QStringLiteral("--settings")});
}

void DesktopLauncher::launch()
{
    if (running())
        callCanvas(QStringLiteral("show"), {QString()});
    else
        start();
}

void DesktopLauncher::restart()
{
    if (!running()) {
        start();
        return;
    }
    // Quit is async and the bus name is the single-instance lock: starting the
    // new instance immediately would just forward its request to the dying one
    // and exit. Let the old instance drop the name first, then start fresh.
    callCanvas(QStringLiteral("quit"));
    QTimer::singleShot(600, [] { start(); });
}

bool DesktopLauncher::preload()
{
    QSettings s(settingsFilePath(), QSettings::IniFormat);
    return s.value(QStringLiteral("preload"), false).toBool();
}

void DesktopLauncher::setPreload(bool on)
{
    QSettings s(settingsFilePath(), QSettings::IniFormat);
    s.setValue(QStringLiteral("preload"), on);
}

void DesktopLauncher::quitRunning()
{
    if (running())
        callCanvas(QStringLiteral("quit"));
}

void DesktopLauncher::startIfPreloading()
{
    // Unlike the control panel (which comes up hidden), the canvas is always on:
    // preload means "show it with the session".
    if (preload() && !running())
        start();
}
