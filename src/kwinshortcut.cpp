#include "kwinshortcut.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QProcessEnvironment>

namespace KWinShortcut {

bool sessionIsKde()
{
    static const bool kde = [] {
        const auto env = QProcessEnvironment::systemEnvironment();
        const QString desktop = env.value(QStringLiteral("XDG_CURRENT_DESKTOP"));
        const QString session = env.value(QStringLiteral("XDG_SESSION_DESKTOP"));
        return desktop.contains(QStringLiteral("KDE"), Qt::CaseInsensitive)
            || session.contains(QStringLiteral("KDE"), Qt::CaseInsensitive)
            || session.contains(QStringLiteral("plasma"), Qt::CaseInsensitive);
    }();
    return kde;
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
