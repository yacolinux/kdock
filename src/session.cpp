#include "session.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QProcessEnvironment>
#include <QString>

namespace Session {

namespace {

Kind detect()
{
    const QString forced = qEnvironmentVariable("KDOCK_TEST_SESSION");
    if (!forced.isEmpty()) {
        if (forced.compare(QLatin1String("kde"), Qt::CaseInsensitive) == 0)
            return Kde;
        if (forced.compare(QLatin1String("lxqt"), Qt::CaseInsensitive) == 0)
            return Lxqt;
        return Other;
    }

    const auto env = QProcessEnvironment::systemEnvironment();
    const QString desktop = env.value(QStringLiteral("XDG_CURRENT_DESKTOP"));
    const QString session = env.value(QStringLiteral("XDG_SESSION_DESKTOP"));
    const QString theme = env.value(QStringLiteral("QT_QPA_PLATFORMTHEME"));

    // LXQt first: XDG_CURRENT_DESKTOP is a colon-separated list and this
    // session's is "LXQt:kwin_wayland", which contains the window manager's
    // name too. Asking about KDE first would be right for the *compositor* and
    // wrong for everything this enum is about.
    if (desktop.contains(QLatin1String("LXQt"), Qt::CaseInsensitive)
        || session.contains(QLatin1String("lxqt"), Qt::CaseInsensitive)
        || theme.compare(QLatin1String("lxqt"), Qt::CaseInsensitive) == 0) {
        return Lxqt;
    }

    if (desktop.contains(QLatin1String("KDE"), Qt::CaseInsensitive)
        || session.contains(QLatin1String("KDE"), Qt::CaseInsensitive)
        || session.contains(QLatin1String("plasma"), Qt::CaseInsensitive)) {
        return Kde;
    }

    return Other;
}

bool serviceRegistered(const char *name)
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return false;
    QDBusConnectionInterface *iface = bus.interface();
    return iface && iface->isServiceRegistered(QLatin1String(name));
}

} // namespace

Kind kind()
{
    static const Kind k = detect();
    return k;
}

bool hasKWin()
{
    static const bool present = serviceRegistered("org.kde.KWin");
    return present;
}

bool hasPlasmaShell()
{
    static const bool present = serviceRegistered("org.kde.plasmashell");
    return present;
}

} // namespace Session
