#include "desktopcontrol.h"

#include "session.h"

#include <QDBusConnection>
#include <QDBusMessage>

DesktopControl::DesktopControl(QObject *parent)
    : QObject(parent)
{
    checkAvailability();
}

void DesktopControl::checkAvailability()
{
    // Virtual desktops and the shortcuts that move a window between them are
    // KWin's, not Plasma's — see Session for why the two are asked separately.
    m_available = Session::hasKWin();
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
