#include "tilemenuservice.h"

#include "tilewindow.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QVariant>

QString TileMenuService::serviceName()
{
    return QStringLiteral("org.kdock.TileMenu");
}

QString TileMenuService::objectPath()
{
    return QStringLiteral("/TileMenu");
}

bool TileMenuService::alreadyRunning()
{
    auto *iface = QDBusConnection::sessionBus().interface();
    return iface && iface->isServiceRegistered(serviceName());
}

namespace {
void call(const QString &method, const QVariantList &args = {})
{
    QDBusMessage msg = QDBusMessage::createMethodCall(TileMenuService::serviceName(),
                                                      TileMenuService::objectPath(),
                                                      TileMenuService::serviceName(), method);
    if (!args.isEmpty())
        msg.setArguments(args);
    // asyncCall so a wedged instance cannot block the caller for the D-Bus
    // default of 25 s.
    QDBusConnection::sessionBus().asyncCall(msg);
}
} // namespace

void TileMenuService::callToggle(const QString &screenName)
{
    call(QStringLiteral("toggle"), {screenName});
}

void TileMenuService::callShow(const QString &screenName)
{
    call(QStringLiteral("show"), {screenName});
}

void TileMenuService::callHide()
{
    call(QStringLiteral("hide"));
}

void TileMenuService::callShowSettings()
{
    call(QStringLiteral("showSettings"));
}

void TileMenuService::callQuit()
{
    call(QStringLiteral("quit"));
}

TileMenuService::TileMenuService(TileWindow *window, QObject *parent)
    : QObject(parent)
    , m_window(window)
{
}

bool TileMenuService::registerOnBus()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return false;
    if (!bus.registerService(serviceName()))
        return false;
    return bus.registerObject(objectPath(), this, QDBusConnection::ExportScriptableSlots);
}

void TileMenuService::toggle(const QString &screenName)
{
    if (!m_window)
        return;
    if (m_window->isVisible())
        m_window->hideMenu();
    else
        m_window->showOn(screenName);
}

void TileMenuService::show(const QString &screenName)
{
    if (m_window)
        m_window->showOn(screenName);
}

void TileMenuService::hide()
{
    if (m_window)
        m_window->hideMenu();
}

void TileMenuService::showSettings()
{
    if (m_window)
        m_window->openSettings();
}

void TileMenuService::quit()
{
    QCoreApplication::quit();
}
