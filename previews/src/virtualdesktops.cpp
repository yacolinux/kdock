#include "virtualdesktops.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QVariant>

namespace {
const auto kService = QStringLiteral("org.kde.KWin");
const auto kPath = QStringLiteral("/VirtualDesktopManager");
const auto kIface = QStringLiteral("org.kde.KWin.VirtualDesktopManager");
} // namespace

VirtualDesktops::VirtualDesktops(QObject *parent)
    : QObject(parent)
{
    QDBusConnection bus = QDBusConnection::sessionBus();

    bus.connect(kService, kPath, kIface, QStringLiteral("currentChanged"), this,
                SLOT(setCurrent(QString)));

    // Async: a blocking property read at startup would stall the first frame
    // whenever KWin is busy bringing the session up.
    QDBusMessage req = QDBusMessage::createMethodCall(
        kService, kPath, QStringLiteral("org.freedesktop.DBus.Properties"),
        QStringLiteral("Get"));
    req.setArguments({kIface, QStringLiteral("current")});

    auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(req), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this](QDBusPendingCallWatcher *call) {
                const QDBusPendingReply<QVariant> reply = *call;
                if (reply.isValid())
                    setCurrent(reply.value().toString());
                call->deleteLater();
            });
}

void VirtualDesktops::setCurrent(const QString &id)
{
    if (m_current == id)
        return;
    m_current = id;
    emit currentChanged();
}
