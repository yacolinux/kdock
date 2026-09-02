#include "apprestart.h"

#include "clipboardlauncher.h"
#include "controlmanagerlauncher.h"
#include "desktoplauncher.h"
#include "previewslauncher.h"
#include "systraylauncher.h"
#include "tilemenulauncher.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusReply>
#include <QElapsedTimer>
#include <QProcess>
#include <QThread>

#include <csignal>
#include <unistd.h>

namespace {
// Long enough for resident processes to notice the D-Bus call and tear down,
// short enough that a wedged one does not hold the restart hostage: past this
// the dock relaunches anyway (worst case an accessory survives, exactly like
// before this existed).
constexpr int kQuitWaitMs = 2000;
constexpr int kForceTermWaitMs = 500;
constexpr int kForceKillWaitMs = 500;
constexpr int kPollMs = 25;

bool anyAccessoryRunning()
{
    return PreviewsLauncher::running() || TileMenuLauncher::running()
           || ControlManagerLauncher::running() || DesktopLauncher::running()
           || ClipboardLauncher::running() || SystrayLauncher::running();
}

bool waitForAccessoriesToQuit(int timeoutMs)
{
    QElapsedTimer waited;
    waited.start();
    while (anyAccessoryRunning() && waited.elapsed() < timeoutMs)
        QThread::msleep(kPollMs);
    return !anyAccessoryRunning();
}

QStringList runningAccessoryServices()
{
    auto *iface = QDBusConnection::sessionBus().interface();
    if (!iface)
        return {};
    const QDBusReply<QStringList> names = iface->registeredServiceNames();
    if (!names.isValid())
        return {};

    QStringList services;
    for (const QString &name : names.value()) {
        if (name == QLatin1String("org.kdock.Previews")
            || name == QLatin1String("org.kdock.TileMenu")
            || name == QLatin1String("org.kdock.ControlManager")
            || name == QLatin1String("org.kdock.Clipboard")
            || name == QLatin1String("org.kdock.Systray")
            || name == QLatin1String("org.kdock.Desktop")
            || name.startsWith(QLatin1String("org.kdock.Desktop."))) {
            services << name;
        }
    }
    return services;
}

void signalRemainingAccessories(int signalNumber)
{
    auto *iface = QDBusConnection::sessionBus().interface();
    if (!iface)
        return;
    const qint64 self = QCoreApplication::applicationPid();
    for (const QString &service : runningAccessoryServices()) {
        const QDBusReply<uint> pidReply = iface->servicePid(service);
        if (!pidReply.isValid())
            continue;
        const qint64 pid = pidReply.value();
        // Service names are the identity check. The pid check avoids ever
        // signalling this process if a malformed session bus maps one of our
        // names back to it.
        if (pid > 1 && pid != self)
            ::kill(static_cast<pid_t>(pid), signalNumber);
    }
}
} // namespace

void kdock::stopAccessories()
{
    static bool stopped = false;
    if (stopped)
        return;
    stopped = true;

    PreviewsLauncher::quitRunning();
    TileMenuLauncher::quitRunning();
    ControlManagerLauncher::quitRunning();
    DesktopLauncher::quitRunning();
    ClipboardLauncher::quitRunning();
    SystrayLauncher::quitRunning();

    // Handing over while an old bus name is still registered means the new
    // kdock skips its startIf*() path and silently keeps the old binary.
    // Give normal D-Bus shutdown a chance first; a stuck event loop then gets
    // SIGTERM, and SIGKILL is reserved for the last, proven service owner.
    if (waitForAccessoriesToQuit(kQuitWaitMs))
        return;
    signalRemainingAccessories(SIGTERM);
    if (waitForAccessoriesToQuit(kForceTermWaitMs))
        return;
    signalRemainingAccessories(SIGKILL);
    waitForAccessoriesToQuit(kForceKillWaitMs);
}

void kdock::restartAll(const QStringList &extraArgs)
{
    stopAccessories();

    // Preserve the CLI arguments this instance was started with (e.g.
    // --screen <name>) so the relaunched dock lands on the same output.
    QStringList args = QCoreApplication::arguments();
    if (!args.isEmpty())
        args.removeFirst();
    // ... except a --apply-preset <zip> pair, which was consumed at startup by
    // this very process (see the header).
    const int applied = args.indexOf(QLatin1String(kApplyPresetFlag));
    if (applied >= 0) {
        args.removeAt(applied);
        if (applied < args.size())
            args.removeAt(applied);
    }
    args += extraArgs;
    QProcess::startDetached(QCoreApplication::applicationFilePath(), args);
    QCoreApplication::quit();
}
