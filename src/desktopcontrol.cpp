#include "desktopcontrol.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QProcessEnvironment>

DesktopControl::DesktopControl(QObject *parent)
    : QObject(parent)
{
    checkAvailability();
}

void DesktopControl::checkAvailability()
{
    const auto env = QProcessEnvironment::systemEnvironment();
    const QString desktop = env.value(QStringLiteral("XDG_CURRENT_DESKTOP"));
    const QString session = env.value(QStringLiteral("XDG_SESSION_DESKTOP"));

    if (desktop.contains(QStringLiteral("KDE"), Qt::CaseInsensitive) ||
        session.contains(QStringLiteral("KDE"), Qt::CaseInsensitive) ||
        session.contains(QStringLiteral("plasma"), Qt::CaseInsensitive)) {
        m_available = true;
    }
}

void DesktopControl::moveToNextDesktop()
{
    if (!m_available)
        return;

    // KWin registers the "Window to Next Desktop" global shortcut, which moves
    // the active window to the next virtual desktop (wrapping around per KWin's
    // rollover setting). Invoking it via kglobalaccel avoids needing the
    // org_kde_plasma_virtual_desktop protocol to enumerate desktops ourselves.
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.kde.kglobalaccel"),
        QStringLiteral("/component/kwin"),
        QStringLiteral("org.kde.kglobalaccel.Component"),
        QStringLiteral("invokeShortcut"));
    msg << QStringLiteral("Window to Next Desktop");

    QDBusConnection::sessionBus().call(msg);
}
