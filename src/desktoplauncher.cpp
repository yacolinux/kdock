#include "desktoplauncher.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QFileInfo>
#include <QGuiApplication>
#include <QProcess>
#include <QScreen>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>

namespace {
const auto kBinary = QStringLiteral("kdock-desktop");
const auto kServiceBase = QStringLiteral("org.kdock.Desktop");
const auto kInterface = QStringLiteral("org.kdock.Desktop");
const auto kPath = QStringLiteral("/Desktop");

QString settingsFilePath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
           + QStringLiteral("/kdock/desktop.conf");
}

// Must match CmService::sanitize in the desktop binary: bus-name elements are
// [A-Za-z0-9_], and connectors carry '-' (DP-1 -> DP_1).
QString sanitize(const QString &s)
{
    QString out = s;
    for (QChar &c : out)
        if (!c.isLetterOrNumber() && c != QLatin1Char('_'))
            c = QLatin1Char('_');
    return out;
}

void callInstance(const QString &connector, const QString &method,
                  const QVariantList &args = {})
{
    QDBusMessage msg = QDBusMessage::createMethodCall(
        DesktopLauncher::serviceFor(connector), kPath, kInterface, method);
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

// --- switches --------------------------------------------------------------

bool DesktopLauncher::masterEnabled()
{
    QSettings s(settingsFilePath(), QSettings::IniFormat);
    // Migrate the pre-per-monitor "preload" (always-on autostart) into the master
    // when the new key was never written.
    if (s.contains(QStringLiteral("enabled")))
        return s.value(QStringLiteral("enabled")).toBool();
    return s.value(QStringLiteral("preload"), false).toBool();
}

void DesktopLauncher::setMasterEnabled(bool on)
{
    QSettings s(settingsFilePath(), QSettings::IniFormat);
    s.setValue(QStringLiteral("enabled"), on);
    s.sync();
}

QStringList DesktopLauncher::enabledScreens()
{
    QSettings s(settingsFilePath(), QSettings::IniFormat);
    const QStringList raw = s.value(QStringLiteral("enabledScreens"))
                                .toString()
                                .split(QLatin1Char(','), Qt::SkipEmptyParts);
    QStringList out;
    for (const QString &c : raw) {
        const QString t = c.trimmed();
        if (!t.isEmpty() && !out.contains(t))
            out << t;
    }
    return out;
}

bool DesktopLauncher::screenEnabled(const QString &connector)
{
    return enabledScreens().contains(connector);
}

void DesktopLauncher::setScreenEnabled(const QString &connector, bool on)
{
    QStringList list = enabledScreens();
    if (on) {
        if (!list.contains(connector))
            list << connector;
    } else {
        list.removeAll(connector);
    }
    QSettings s(settingsFilePath(), QSettings::IniFormat);
    s.setValue(QStringLiteral("enabledScreens"), list.join(QLatin1Char(',')));
    s.sync();
}

// --- per-instance lifecycle ------------------------------------------------

QString DesktopLauncher::serviceFor(const QString &connector)
{
    return connector.isEmpty() ? kServiceBase
                               : kServiceBase + QLatin1Char('.') + sanitize(connector);
}

bool DesktopLauncher::runningOn(const QString &connector)
{
    auto *iface = QDBusConnection::sessionBus().interface();
    return iface && iface->isServiceRegistered(serviceFor(connector));
}

bool DesktopLauncher::start(const QStringList &args)
{
    const QString binary = binaryPath();
    if (binary.isEmpty())
        return false;
    // startDetached so the canvas outlives a kdock restart. The child gets this
    // process' environment, which main.cpp has already cleaned of
    // QT_WAYLAND_SHELL_INTEGRATION.
    return QProcess::startDetached(binary, args);
}

void DesktopLauncher::launchOn(const QString &connector)
{
    if (runningOn(connector))
        return;
    start({QStringLiteral("--screen"), connector});
}

void DesktopLauncher::quitOn(const QString &connector)
{
    if (runningOn(connector))
        callInstance(connector, QStringLiteral("quit"));
}

void DesktopLauncher::restartOn(const QString &connector)
{
    if (!runningOn(connector)) {
        launchOn(connector);
        return;
    }
    // The bus name is the single-instance lock; a new instance started now would
    // just forward to the dying one. Let the old one drop the name first.
    callInstance(connector, QStringLiteral("quit"));
    QTimer::singleShot(600, [connector] { start({QStringLiteral("--screen"), connector}); });
}

void DesktopLauncher::openSettingsOn(const QString &connector)
{
    if (runningOn(connector))
        callInstance(connector, QStringLiteral("showSettings"));
    else
        start({QStringLiteral("--screen"), connector, QStringLiteral("--settings")});
}

// --- reconcile -------------------------------------------------------------

QStringList DesktopLauncher::connectedScreensNow()
{
    // Honour the same test seam DockManager uses, so a harness can pretend to
    // have several monitors.
    if (qEnvironmentVariableIsSet("KDOCK_TEST_SCREENS")) {
        return qEnvironmentVariable("KDOCK_TEST_SCREENS")
            .split(QLatin1Char(','), Qt::SkipEmptyParts);
    }
    QStringList names;
    const auto screens = QGuiApplication::screens();
    for (QScreen *s : screens)
        names << s->name();
    return names;
}

void DesktopLauncher::applyState(const QStringList &connectedScreens)
{
    const QStringList connected =
        connectedScreens.isEmpty() ? connectedScreensNow() : connectedScreens;
    const bool master = masterEnabled();
    const QStringList wanted = enabledScreens();
    for (const QString &connector : connected) {
        const bool shouldRun = master && wanted.contains(connector);
        if (shouldRun)
            launchOn(connector);
        else
            quitOn(connector);
    }
}

// --- aggregate helpers -----------------------------------------------------

bool DesktopLauncher::running()
{
    auto *iface = QDBusConnection::sessionBus().interface();
    if (!iface)
        return false;
    const QDBusReply<QStringList> reply = iface->registeredServiceNames();
    if (!reply.isValid())
        return false;
    for (const QString &name : reply.value())
        if (name == kServiceBase || name.startsWith(kServiceBase + QLatin1Char('.')))
            return true;
    return false;
}

void DesktopLauncher::quitRunning()
{
    auto *iface = QDBusConnection::sessionBus().interface();
    if (!iface)
        return;
    const QDBusReply<QStringList> reply = iface->registeredServiceNames();
    if (!reply.isValid())
        return;
    for (const QString &name : reply.value()) {
        if (name != kServiceBase && !name.startsWith(kServiceBase + QLatin1Char('.')))
            continue;
        QDBusMessage msg =
            QDBusMessage::createMethodCall(name, kPath, kInterface, QStringLiteral("quit"));
        QDBusConnection::sessionBus().asyncCall(msg);
    }
}

void DesktopLauncher::startEnabled()
{
    if (masterEnabled())
        applyState();
}
