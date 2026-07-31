#include "monitorcontrol.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QProcessEnvironment>

MonitorControl::MonitorControl(QObject *parent)
    : QObject(parent)
{
    const auto env = QProcessEnvironment::systemEnvironment();
    const QString desktop = env.value(QStringLiteral("XDG_CURRENT_DESKTOP"));
    const QString session = env.value(QStringLiteral("XDG_SESSION_DESKTOP"));
    m_isKde = desktop.contains(QStringLiteral("KDE"), Qt::CaseInsensitive) ||
              session.contains(QStringLiteral("KDE"), Qt::CaseInsensitive) ||
              session.contains(QStringLiteral("plasma"), Qt::CaseInsensitive);

    // Available whenever we're on KWin, mirroring the overview / move-to-desktop
    // widgets. (It used to also require >1 connected monitor, which hid the
    // button on single-monitor states even when the user enabled it.)
    m_available = m_isKde;
}

void MonitorControl::moveToNextScreen()
{
    if (!m_available)
        return;

    // KWin registers the "Window to Next Screen" global shortcut, which moves
    // the active window to the next monitor. Invoking it via kglobalaccel keeps
    // this consistent with the overview/next-desktop widgets.
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.kde.kglobalaccel"),
        QStringLiteral("/component/kwin"),
        QStringLiteral("org.kde.kglobalaccel.Component"),
        QStringLiteral("invokeShortcut"));
    msg << QStringLiteral("Window to Next Screen");

    QDBusConnection::sessionBus().call(msg);
}
