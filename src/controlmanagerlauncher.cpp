#include "controlmanagerlauncher.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QFileInfo>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>

namespace {
const auto kBinary = QStringLiteral("kdock-controlmanager");
const auto kService = QStringLiteral("org.kdock.ControlManager");
const auto kPath = QStringLiteral("/ControlManager");

QString settingsFilePath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
           + QStringLiteral("/kdock/controlmanager.conf");
}

void callPanel(const QString &method, const QVariantList &args = {})
{
    QDBusMessage msg = QDBusMessage::createMethodCall(kService, kPath, kService, method);
    if (!args.isEmpty())
        msg.setArguments(args);
    // asyncCall: a wedged instance must not block the dock for the D-Bus default
    // of 25 s.
    QDBusConnection::sessionBus().asyncCall(msg);
}
} // namespace

QString ControlManagerLauncher::binaryPath()
{
    // Next to kdock itself (the installed case), then the build tree layout
    // (build/kdock + build/controlmanager/kdock-controlmanager), then $PATH.
    const QString dir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        dir + QLatin1Char('/') + kBinary,
        dir + QStringLiteral("/controlmanager/") + kBinary,
    };
    for (const QString &candidate : candidates) {
        const QFileInfo info(candidate);
        if (info.isFile() && info.isExecutable())
            return info.absoluteFilePath();
    }
    return QStandardPaths::findExecutable(kBinary);
}

bool ControlManagerLauncher::installed()
{
    return !binaryPath().isEmpty();
}

bool ControlManagerLauncher::running()
{
    auto *iface = QDBusConnection::sessionBus().interface();
    return iface && iface->isServiceRegistered(kService);
}

bool ControlManagerLauncher::start(const QStringList &args)
{
    const QString binary = binaryPath();
    if (binary.isEmpty())
        return false;
    // startDetached so the panel outlives a kdock restart. The child gets this
    // process' environment, which main.cpp has already cleaned of
    // QT_WAYLAND_SHELL_INTEGRATION (see the note there).
    return QProcess::startDetached(binary, args);
}

void ControlManagerLauncher::toggle(const QString &screenName)
{
    if (running()) {
        callPanel(QStringLiteral("toggle"), {screenName});
        return;
    }
    QStringList args{QStringLiteral("--toggle")};
    if (!screenName.isEmpty())
        args << QStringLiteral("--screen") << screenName;
    start(args);
}

void ControlManagerLauncher::showSection(const QString &sectionId, const QString &screenName)
{
    if (running()) {
        callPanel(QStringLiteral("showSection"), {sectionId, screenName});
        return;
    }
    QStringList args{QStringLiteral("--show"), QStringLiteral("--section"), sectionId};
    if (!screenName.isEmpty())
        args << QStringLiteral("--screen") << screenName;
    start(args);
}

void ControlManagerLauncher::openSettings()
{
    if (running())
        callPanel(QStringLiteral("showSettings"));
    else
        start({QStringLiteral("--settings")});
}

bool ControlManagerLauncher::preload()
{
    QSettings s(settingsFilePath(), QSettings::IniFormat);
    return s.value(QStringLiteral("preload"), false).toBool();
}

void ControlManagerLauncher::setPreload(bool on)
{
    QSettings s(settingsFilePath(), QSettings::IniFormat);
    s.setValue(QStringLiteral("preload"), on);
}

void ControlManagerLauncher::quitRunning()
{
    if (running())
        callPanel(QStringLiteral("quit"));
}

void ControlManagerLauncher::startIfPreloading()
{
    // --hide: come up resident but with nothing on screen, so the first click is
    // as instant as every one after it.
    if (preload() && !running())
        start({QStringLiteral("--hide")});
}
