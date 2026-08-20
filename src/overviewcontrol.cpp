#include "overviewcontrol.h"

#include "session.h"

#include <QDBusConnection>
#include <QDBusMessage>

OverviewControl::OverviewControl(QObject *parent)
    : QObject(parent)
{
    checkAvailability();
}

void OverviewControl::checkAvailability()
{
    // KWin's own effect, invoked through KWin's own shortcut: what this needs
    // is KWin, not Plasma. Gating it on "the session is KDE" left the widget
    // dead in the LXQt+kwin_wayland session. See Session.
    m_available = Session::hasKWin();
}

void OverviewControl::toggle()
{
    if (!m_available)
        return;

    // Plasma 6: the overview effect is always loaded; toggling its *loaded*
    // state (org.kde.kwin.Effects.toggleEffect) does NOT show it. The screen is
    // shown/hidden by invoking KWin's global "Overview" shortcut.
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.kde.kglobalaccel"),
        QStringLiteral("/component/kwin"),
        QStringLiteral("org.kde.kglobalaccel.Component"),
        QStringLiteral("invokeShortcut"));
    msg << QStringLiteral("Overview");

    QDBusConnection::sessionBus().call(msg);
    // No reliable way to observe the real state (it can also be toggled via
    // keyboard/gesture); track optimistically so the indicator reflects clicks.
    m_active = !m_active;
    emit activeChanged();
}
