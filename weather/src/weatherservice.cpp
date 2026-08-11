#include "weatherservice.h"

#include "weatherwindow.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QVariant>

QString WeatherService::serviceName()
{
    return QStringLiteral("org.kdock.Weather");
}

QString WeatherService::objectPath()
{
    return QStringLiteral("/Weather");
}

bool WeatherService::alreadyRunning()
{
    auto *iface = QDBusConnection::sessionBus().interface();
    return iface && iface->isServiceRegistered(serviceName());
}

namespace {
void call(const QString &method, const QVariantList &args = {})
{
    QDBusMessage msg = QDBusMessage::createMethodCall(WeatherService::serviceName(),
                                                      WeatherService::objectPath(),
                                                      WeatherService::serviceName(), method);
    if (!args.isEmpty())
        msg.setArguments(args);
    // asyncCall so a wedged instance cannot block the caller for the D-Bus
    // default of 25 s.
    QDBusConnection::sessionBus().asyncCall(msg);
}
} // namespace

void WeatherService::callToggle(const QString &screenName)
{
    call(QStringLiteral("toggle"), {screenName});
}

void WeatherService::callShow(const QString &screenName)
{
    call(QStringLiteral("show"), {screenName});
}

void WeatherService::callShowSettings()
{
    call(QStringLiteral("showSettings"));
}

void WeatherService::callQuit()
{
    call(QStringLiteral("quit"));
}

WeatherService::WeatherService(WeatherWindow *window, QObject *parent)
    : QObject(parent)
    , m_window(window)
{
}

bool WeatherService::registerOnBus()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return false;
    if (!bus.registerService(serviceName()))
        return false;
    return bus.registerObject(objectPath(), this, QDBusConnection::ExportScriptableSlots);
}

void WeatherService::toggle(const QString &screenName)
{
    if (!m_window)
        return;
    if (m_window->isVisible())
        m_window->closeWindow();
    else
        m_window->showOn(screenName);
}

void WeatherService::show(const QString &screenName)
{
    if (m_window)
        m_window->showOn(screenName);
}

void WeatherService::showSettings()
{
    if (m_window)
        m_window->openSettings();
}

void WeatherService::quit()
{
    QCoreApplication::quit();
}
