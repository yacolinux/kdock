#include "previewslauncher.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>

namespace {
const auto kBinary = QStringLiteral("kdock-previews");
const auto kService = QStringLiteral("org.kdock.Previews");
const auto kPath = QStringLiteral("/Previews");

void callPreviews(const QString &method)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(kService, kPath, kService, method);
    // asyncCall: a wedged instance must not block the settings dialog for the
    // D-Bus default of 25 s.
    QDBusConnection::sessionBus().asyncCall(msg);
}
} // namespace

QString PreviewsLauncher::settingsFilePath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
        + QStringLiteral("/kdock/previews.conf");
}

QString PreviewsLauncher::binaryPath()
{
    // Next to kdock itself (the installed case), then the build tree layout
    // (build/kdock + build/previews/kdock-previews), then $PATH.
    const QString dir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        dir + QLatin1Char('/') + kBinary,
        dir + QStringLiteral("/previews/") + kBinary,
    };
    for (const QString &candidate : candidates) {
        const QFileInfo info(candidate);
        if (info.isFile() && info.isExecutable())
            return info.absoluteFilePath();
    }
    return QStandardPaths::findExecutable(kBinary);
}

bool PreviewsLauncher::running()
{
    auto *iface = QDBusConnection::sessionBus().interface();
    return iface && iface->isServiceRegistered(kService);
}

bool PreviewsLauncher::enabled()
{
    QSettings s(settingsFilePath(), QSettings::IniFormat);
    return s.value(QStringLiteral("enabled"), false).toBool();
}

bool PreviewsLauncher::start(const QStringList &args)
{
    const QString binary = binaryPath();
    if (binary.isEmpty())
        return false;
    // startDetached so the previews outlive a kdock restart. The child gets this
    // process' environment, which main.cpp has already cleaned of
    // QT_WAYLAND_SHELL_INTEGRATION (see the note there) — kdock-previews sets its
    // own.
    return QProcess::startDetached(binary, args);
}

void PreviewsLauncher::setEnabled(bool on)
{
    {
        QSettings s(settingsFilePath(), QSettings::IniFormat);
        s.setValue(QStringLiteral("enabled"), on);
    }

    if (on) {
        // A running instance only needs to be told the switch moved; otherwise
        // bring it up.
        if (running())
            callPreviews(QStringLiteral("reload"));
        else
            start();
    } else if (running()) {
        callPreviews(QStringLiteral("quit"));
    }
}

void PreviewsLauncher::openSettings()
{
    if (running())
        callPreviews(QStringLiteral("showSettings"));
    else
        start({QStringLiteral("--settings")});
}

void PreviewsLauncher::quitRunning()
{
    if (running())
        callPreviews(QStringLiteral("quit"));
}

void PreviewsLauncher::startIfEnabled()
{
    if (enabled() && !running())
        start();
}
