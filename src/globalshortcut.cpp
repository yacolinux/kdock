#include "globalshortcut.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusReply>

namespace {
const auto kService = QStringLiteral("org.kde.kglobalaccel");
const auto kPath = QStringLiteral("/kglobalaccel");
const auto kIface = QStringLiteral("org.kde.KGlobalAccel");
const auto kComponent = QStringLiteral("kdock");
const auto kComponentPath = QStringLiteral("/component/kdock");
const auto kComponentIface = QStringLiteral("org.kde.kglobalaccel.Component");

// kglobalaccel identifies an action by four strings, in this order.
QStringList actionIdFields(const QString &action, const QString &friendlyName)
{
    return {kComponent, action, QStringLiteral("kdock"), friendlyName};
}
} // namespace

GlobalShortcuts::GlobalShortcuts(QObject *parent)
    : QObject(parent)
{
}

bool GlobalShortcuts::registerAction(const QString &actionId, const QString &friendlyName)
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    auto *iface = bus.interface();
    if (!iface || !iface->isServiceRegistered(kService))
        return false;

    const QStringList fields = actionIdFields(actionId, friendlyName);

    QDBusMessage reg = QDBusMessage::createMethodCall(kService, kPath, kIface,
                                                      QStringLiteral("doRegister"));
    reg.setArguments({fields});
    // asyncCall, and NOT call(BlockWithGui): this runs from main() before
    // app.exec(), and BlockWithGui spins a *nested* event loop while it waits.
    // Anything the bus delivers meanwhile is dispatched right there — which is
    // how a tray client registering its icon ended up being served from inside
    // this call, before the dock had started (see systray.cpp). Nothing here
    // needs the reply: kglobalaccel either takes the action or it does not.
    bus.asyncCall(reg, 2000);

    // An empty key list with SetPresent (2) publishes the action with no
    // combination attached: it appears in the shortcuts KCM waiting for one.
    // Autoloading is left on, so whatever the user assigns is remembered by
    // kglobalaccel across restarts without us persisting anything.
    //
    // setShortcut (asaiu), not setShortcutKeys (asa(ai)u): the older signature
    // takes a plain array of ints, which an empty QList<int> marshals to with no
    // qDBusRegisterMetaType. The newer one needs a registered struct type to say
    // "nothing".
    QDBusMessage keys = QDBusMessage::createMethodCall(kService, kPath, kIface,
                                                       QStringLiteral("setShortcut"));
    keys.setArguments({fields, QVariant::fromValue(QList<int>()), uint(2)});
    // Ordered after doRegister by the bus itself: both messages travel the same
    // connection to the same destination, and D-Bus preserves that order.
    bus.asyncCall(keys, 2000);

    if (!m_connected) {
        // The press arrives on the *component* object, not on /kglobalaccel.
        m_connected = bus.connect(kService, kComponentPath, kComponentIface,
                                  QStringLiteral("globalShortcutPressed"), this,
                                  SLOT(onPressed(QString, QString, qlonglong)));
    }
    return true;
}

void GlobalShortcuts::onPressed(const QString &component, const QString &action,
                                qlonglong timestamp)
{
    Q_UNUSED(timestamp);
    if (component != kComponent)
        return;
    emit triggered(action);
}
