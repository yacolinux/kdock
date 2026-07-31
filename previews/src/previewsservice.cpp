#include "previewsservice.h"

#include "previewmanager.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>

QString PreviewsService::serviceName()
{
    return QStringLiteral("org.kdock.Previews");
}

QString PreviewsService::objectPath()
{
    return QStringLiteral("/Previews");
}

bool PreviewsService::alreadyRunning()
{
    auto *iface = QDBusConnection::sessionBus().interface();
    return iface && iface->isServiceRegistered(serviceName());
}

namespace {
void call(const QString &method)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(PreviewsService::serviceName(),
                                                     PreviewsService::objectPath(),
                                                     QStringLiteral("org.kdock.Previews"), method);
    // asyncCall so a wedged instance cannot block the caller for the D-Bus
    // default of 25 s.
    QDBusConnection::sessionBus().asyncCall(msg);
}
} // namespace

void PreviewsService::callShowSettings()
{
    call(QStringLiteral("showSettings"));
}

void PreviewsService::callQuit()
{
    call(QStringLiteral("quit"));
}

void PreviewsService::callReload()
{
    call(QStringLiteral("reload"));
}

PreviewsService::PreviewsService(PreviewManager *manager, QObject *parent)
    : QObject(parent)
    , m_manager(manager)
{
}

bool PreviewsService::registerOnBus()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return false;
    if (!bus.registerService(serviceName()))
        return false;
    return bus.registerObject(objectPath(), this, QDBusConnection::ExportScriptableSlots);
}

void PreviewsService::showSettings()
{
    if (m_manager)
        m_manager->showSettings();
}

void PreviewsService::reload()
{
    if (m_manager)
        m_manager->reload();
}

void PreviewsService::quit()
{
    QCoreApplication::quit();
}
