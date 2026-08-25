#include "systrayservice.h"

#include "systraywindow.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QVariant>

QString SystrayService::serviceName()
{
    return QStringLiteral("org.kdock.Systray");
}

QString SystrayService::objectPath()
{
    return QStringLiteral("/Systray");
}

bool SystrayService::alreadyRunning()
{
    auto *iface = QDBusConnection::sessionBus().interface();
    return iface && iface->isServiceRegistered(serviceName());
}

namespace {
void call(const QString &method, const QVariantList &args = {})
{
    QDBusMessage msg = QDBusMessage::createMethodCall(SystrayService::serviceName(),
                                                      SystrayService::objectPath(),
                                                      SystrayService::serviceName(), method);
    if (!args.isEmpty())
        msg.setArguments(args);
    // asyncCall so a wedged instance cannot block the caller for the D-Bus
    // default of 25 s.
    QDBusConnection::sessionBus().asyncCall(msg);
}
} // namespace

void SystrayService::callToggle(const QString &screenName)
{
    call(QStringLiteral("toggle"), {screenName});
}

void SystrayService::callShow(const QString &screenName)
{
    call(QStringLiteral("show"), {screenName});
}

void SystrayService::callHide()
{
    call(QStringLiteral("hide"));
}

void SystrayService::callShowSettings()
{
    call(QStringLiteral("showSettings"));
}

void SystrayService::callQuit()
{
    call(QStringLiteral("quit"));
}

void SystrayService::callReloadConfig()
{
    call(QStringLiteral("reloadConfig"));
}

SystrayService::SystrayService(SystrayWindow *window, QObject *parent)
    : QObject(parent)
    , m_window(window)
{
}

bool SystrayService::registerOnBus()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return false;
    if (!bus.registerService(serviceName()))
        return false;
    return bus.registerObject(objectPath(), this, QDBusConnection::ExportScriptableSlots);
}

void SystrayService::toggle(const QString &screenName)
{
    if (!m_window)
        return;
    if (m_window->isVisible())
        m_window->hideWindow();
    else
        m_window->showOn(screenName);
}

void SystrayService::show(const QString &screenName)
{
    if (m_window)
        m_window->showOn(screenName);
}

void SystrayService::hide()
{
    if (m_window)
        m_window->hideWindow();
}

void SystrayService::showSettings()
{
    if (m_window)
        m_window->openSettings();
}

void SystrayService::reloadConfig()
{
    if (m_window)
        m_window->reloadConfig();
}

void SystrayService::quit()
{
    QCoreApplication::quit();
}
