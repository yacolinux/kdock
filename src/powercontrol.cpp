#include "powercontrol.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QProcessEnvironment>

PowerControl::PowerControl(QObject *parent)
    : QObject(parent)
{
    const auto env = QProcessEnvironment::systemEnvironment();
    const QString desktop = env.value(QStringLiteral("XDG_CURRENT_DESKTOP"));
    const QString session = env.value(QStringLiteral("XDG_SESSION_DESKTOP"));
    m_available = desktop.contains(QStringLiteral("KDE"), Qt::CaseInsensitive) ||
                  session.contains(QStringLiteral("KDE"), Qt::CaseInsensitive) ||
                  session.contains(QStringLiteral("plasma"), Qt::CaseInsensitive);
}

static void callDbus(const QString &service, const QString &path,
                     const QString &iface, const QString &method)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(service, path, iface, method);
    QDBusConnection::sessionBus().asyncCall(msg);
}

void PowerControl::logout()
{
    if (!m_available)
        return;
    callDbus(QStringLiteral("org.kde.LogoutPrompt"), QStringLiteral("/LogoutPrompt"),
             QStringLiteral("org.kde.LogoutPrompt"), QStringLiteral("promptLogout"));
}

void PowerControl::reboot()
{
    if (!m_available)
        return;
    callDbus(QStringLiteral("org.kde.LogoutPrompt"), QStringLiteral("/LogoutPrompt"),
             QStringLiteral("org.kde.LogoutPrompt"), QStringLiteral("promptReboot"));
}

void PowerControl::shutdown()
{
    if (!m_available)
        return;
    callDbus(QStringLiteral("org.kde.LogoutPrompt"), QStringLiteral("/LogoutPrompt"),
             QStringLiteral("org.kde.LogoutPrompt"), QStringLiteral("promptShutDown"));
}

void PowerControl::lock()
{
    callDbus(QStringLiteral("org.freedesktop.ScreenSaver"), QStringLiteral("/ScreenSaver"),
             QStringLiteral("org.freedesktop.ScreenSaver"), QStringLiteral("Lock"));
}

void PowerControl::suspend()
{
    callDbus(QStringLiteral("org.freedesktop.PowerManagement"),
             QStringLiteral("/org/freedesktop/PowerManagement"),
             QStringLiteral("org.freedesktop.PowerManagement"), QStringLiteral("Suspend"));
}
