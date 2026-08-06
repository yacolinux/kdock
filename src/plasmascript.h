// The three things every caller of Plasma's scripting API needs: escaping a
// string for a JS literal, firing an evaluateScript asynchronously, and the
// blocking variant for the one place that has no event loop left (aboutToQuit).
//
// Header-only on purpose: it is three thin wrappers over QDBusMessage, shared by
// WallpaperControl (the nextwallpaper widget) and DesktopWallpapers (wallpapers
// per virtual desktop) so the D-Bus address lives in exactly one place.

#pragma once

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QLatin1Char>
#include <QString>

namespace PlasmaScript {

inline QString escapeJs(const QString &s)
{
    QString r = s;
    r.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    r.replace(QLatin1Char('\''), QStringLiteral("\\'"));
    return r;
}

inline QDBusMessage message(const QString &script)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.kde.plasmashell"), QStringLiteral("/PlasmaShell"),
        QStringLiteral("org.kde.PlasmaShell"), QStringLiteral("evaluateScript"));
    msg << script;
    return msg;
}

// Async call whose reply the caller wants (wrap it in a QDBusPendingCallWatcher).
inline QDBusPendingCall evaluate(const QString &script)
{
    return QDBusConnection::sessionBus().asyncCall(message(script));
}

// Fire and forget.
inline void run(const QString &script)
{
    QDBusConnection::sessionBus().asyncCall(message(script));
}

// Blocking, for the shutdown path: once aboutToQuit has fired there is no event
// loop to deliver an async reply, so a fire-and-forget call would be dropped
// before it reaches plasmashell.
inline QDBusMessage evaluateBlocking(const QString &script, int msec = 3000)
{
    return QDBusConnection::sessionBus().call(message(script), QDBus::Block, msec);
}

} // namespace PlasmaScript
