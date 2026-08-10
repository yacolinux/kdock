#include "tilemenulauncher.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QFileInfo>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>

namespace {
const auto kBinary = QStringLiteral("kdock-tilemenu");
const auto kService = QStringLiteral("org.kdock.TileMenu");
const auto kPath = QStringLiteral("/TileMenu");

QString settingsFilePath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
           + QStringLiteral("/kdock/tilemenu.conf");
}

void callTileMenu(const QString &method, const QVariantList &args = {})
{
    QDBusMessage msg = QDBusMessage::createMethodCall(kService, kPath, kService, method);
    if (!args.isEmpty())
        msg.setArguments(args);
    // asyncCall: a wedged instance must not block the dock for the D-Bus default
    // of 25 s.
    QDBusConnection::sessionBus().asyncCall(msg);
}
} // namespace

QString TileMenuLauncher::binaryPath()
{
    // Next to kdock itself (the installed case), then the build tree layout
    // (build/kdock + build/tilemenu/kdock-tilemenu), then $PATH.
    const QString dir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        dir + QLatin1Char('/') + kBinary,
        dir + QStringLiteral("/tilemenu/") + kBinary,
    };
    for (const QString &candidate : candidates) {
        const QFileInfo info(candidate);
        if (info.isFile() && info.isExecutable())
            return info.absoluteFilePath();
    }
    return QStandardPaths::findExecutable(kBinary);
}

bool TileMenuLauncher::installed()
{
    return !binaryPath().isEmpty();
}

bool TileMenuLauncher::running()
{
    auto *iface = QDBusConnection::sessionBus().interface();
    return iface && iface->isServiceRegistered(kService);
}

bool TileMenuLauncher::start(const QStringList &args)
{
    const QString binary = binaryPath();
    if (binary.isEmpty())
        return false;
    // startDetached so the menu outlives a kdock restart. The child gets this
    // process' environment, which main.cpp has already cleaned of
    // QT_WAYLAND_SHELL_INTEGRATION (see the note there).
    return QProcess::startDetached(binary, args);
}

void TileMenuLauncher::toggle(const QString &screenName)
{
    if (running()) {
        callTileMenu(QStringLiteral("toggle"), {screenName});
        return;
    }
    QStringList args{QStringLiteral("--toggle")};
    if (!screenName.isEmpty())
        args << QStringLiteral("--screen") << screenName;
    start(args);
}

void TileMenuLauncher::openSettings()
{
    if (running())
        callTileMenu(QStringLiteral("showSettings"));
    else
        start({QStringLiteral("--settings")});
}

bool TileMenuLauncher::preload()
{
    QSettings s(settingsFilePath(), QSettings::IniFormat);
    return s.value(QStringLiteral("preload"), false).toBool();
}

void TileMenuLauncher::setPreload(bool on)
{
    QSettings s(settingsFilePath(), QSettings::IniFormat);
    s.setValue(QStringLiteral("preload"), on);
}

void TileMenuLauncher::quitRunning()
{
    if (running())
        callTileMenu(QStringLiteral("quit"));
}

void TileMenuLauncher::startIfPreloading()
{
    // --hide: come up resident but with nothing on screen, so the first click is
    // as instant as every one after it.
    if (preload() && !running())
        start({QStringLiteral("--hide")});
}
