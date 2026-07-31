#include "overviewcontrol.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QProcessEnvironment>

OverviewControl::OverviewControl(QObject *parent)
    : QObject(parent)
{
    checkAvailability();
}

void OverviewControl::checkAvailability()
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
