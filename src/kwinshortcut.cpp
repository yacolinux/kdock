#include "kwinshortcut.h"

#include "session.h"

#include <QDBusConnection>
#include <QDBusMessage>

namespace KWinShortcut {

bool available()
{
    return Session::hasKWin();
}

void invoke(const QString &name)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.kde.kglobalaccel"),
        QStringLiteral("/component/kwin"),
        QStringLiteral("org.kde.kglobalaccel.Component"),
        QStringLiteral("invokeShortcut"));
    msg << name;

    QDBusConnection::sessionBus().call(msg);
}

} // namespace KWinShortcut
