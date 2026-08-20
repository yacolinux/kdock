#include "powercontrol.h"

#include "desktopentry.h"
#include "session.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QProcess>
#include <QStandardPaths>

namespace {

// The .desktop base name and the lxqt-leave flag of each action. Both are
// needed: the file is the preferred path (it is how the session declares what
// it can do, and it survives an LXQt that puts its tools elsewhere), the flag
// is the fallback when the file is missing.
struct LxqtAction
{
    const char *desktopFile;
    const char *flag;
};

LxqtAction lxqtAction(PowerControl::Action action)
{
    switch (action) {
    case PowerControl::Logout:   return {"lxqt-logout.desktop", "--logout"};
    case PowerControl::Reboot:   return {"lxqt-reboot.desktop", "--reboot"};
    case PowerControl::Shutdown: return {"lxqt-shutdown.desktop", "--shutdown"};
    case PowerControl::Suspend:  return {"lxqt-suspend.desktop", "--suspend"};
    case PowerControl::Lock:     return {"lxqt-lockscreen.desktop", "--lockscreen"};
    }
    return {nullptr, nullptr};
}

void callDbus(const QString &service, const QString &path,
              const QString &iface, const QString &method)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(service, path, iface, method);
    QDBusConnection::sessionBus().asyncCall(msg);
}

} // namespace

PowerControl::PowerControl(QObject *parent)
    : QObject(parent)
{
    if (Session::isLxqt()) {
        // Available when the session can at least be left. Either the .desktop
        // files are there, or lxqt-leave is on the PATH, or lxqt-session is on
        // the bus — any one of the three means these buttons will do something.
        const bool haveFiles = !desktopFileFor(Logout).isEmpty()
                            || !desktopFileFor(Shutdown).isEmpty();
        const bool haveTool =
            !QStandardPaths::findExecutable(QStringLiteral("lxqt-leave")).isEmpty();
        if (haveFiles || haveTool)
            m_backend = Lxqt;
    } else if (Session::isKde()) {
        m_backend = Kde;
    }
}

QString PowerControl::desktopFileFor(Action action)
{
    const LxqtAction a = lxqtAction(action);
    if (!a.desktopFile)
        return {};
    return QStandardPaths::locate(QStandardPaths::ApplicationsLocation,
                                  QString::fromLatin1(a.desktopFile));
}

QString PowerControl::resolvedCommand(Action action) const
{
    if (m_backend != Lxqt)
        return {};
    const QString file = desktopFileFor(action);
    if (!file.isEmpty()) {
        const DesktopEntry entry = DesktopEntryIndex::fromFile(file);
        if (entry.isValid())
            return entry.exec;
    }
    const LxqtAction a = lxqtAction(action);
    if (!a.flag)
        return {};
    return QStringLiteral("lxqt-leave %1").arg(QLatin1String(a.flag));
}

void PowerControl::runLxqt(Action action)
{
    const QString file = desktopFileFor(action);
    if (!file.isEmpty()) {
        const DesktopEntry entry = DesktopEntryIndex::fromFile(file);
        if (entry.isValid()) {
            DesktopEntryIndex::launch(entry);
            return;
        }
    }
    const LxqtAction a = lxqtAction(action);
    if (!a.flag)
        return;
    QProcess::startDetached(QStringLiteral("lxqt-leave"),
                            {QString::fromLatin1(a.flag)});
}

void PowerControl::logout()
{
    switch (m_backend) {
    case None:
        return;
    case Lxqt:
        runLxqt(Logout);
        return;
    case Kde:
        callDbus(QStringLiteral("org.kde.LogoutPrompt"), QStringLiteral("/LogoutPrompt"),
                 QStringLiteral("org.kde.LogoutPrompt"), QStringLiteral("promptLogout"));
        return;
    }
}

void PowerControl::reboot()
{
    switch (m_backend) {
    case None:
        return;
    case Lxqt:
        runLxqt(Reboot);
        return;
    case Kde:
        callDbus(QStringLiteral("org.kde.LogoutPrompt"), QStringLiteral("/LogoutPrompt"),
                 QStringLiteral("org.kde.LogoutPrompt"), QStringLiteral("promptReboot"));
        return;
    }
}

void PowerControl::shutdown()
{
    switch (m_backend) {
    case None:
        return;
    case Lxqt:
        runLxqt(Shutdown);
        return;
    case Kde:
        callDbus(QStringLiteral("org.kde.LogoutPrompt"), QStringLiteral("/LogoutPrompt"),
                 QStringLiteral("org.kde.LogoutPrompt"), QStringLiteral("promptShutDown"));
        return;
    }
}

void PowerControl::lock()
{
    // org.freedesktop.ScreenSaver is owned by the compositor, not by the
    // desktop: KWin answers it in this LXQt session too (verified). So the
    // D-Bus call is tried first in every backend, and only a session that does
    // not offer it falls back to LXQt's own tool.
    if (QDBusConnection::sessionBus().interface()
        && QDBusConnection::sessionBus().interface()->isServiceRegistered(
               QStringLiteral("org.freedesktop.ScreenSaver"))) {
        callDbus(QStringLiteral("org.freedesktop.ScreenSaver"), QStringLiteral("/ScreenSaver"),
                 QStringLiteral("org.freedesktop.ScreenSaver"), QStringLiteral("Lock"));
        return;
    }
    if (m_backend == Lxqt)
        runLxqt(Lock);
}

void PowerControl::suspend()
{
    // Same shape as lock(): the freedesktop name is the portable one, and LXQt
    // ships lxqt-powermanagement, which owns it. Fall back to the .desktop only
    // when nobody does.
    if (QDBusConnection::sessionBus().interface()
        && QDBusConnection::sessionBus().interface()->isServiceRegistered(
               QStringLiteral("org.freedesktop.PowerManagement"))) {
        callDbus(QStringLiteral("org.freedesktop.PowerManagement"),
                 QStringLiteral("/org/freedesktop/PowerManagement"),
                 QStringLiteral("org.freedesktop.PowerManagement"), QStringLiteral("Suspend"));
        return;
    }
    if (m_backend == Lxqt)
        runLxqt(Suspend);
}
