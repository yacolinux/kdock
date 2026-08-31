#include "clipboardservice.h"

#include "clipboardwindow.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QVariant>

QString ClipboardService::serviceName()
{
    return QStringLiteral("org.kdock.Clipboard");
}

QString ClipboardService::objectPath()
{
    return QStringLiteral("/Clipboard");
}

bool ClipboardService::alreadyRunning()
{
    auto *iface = QDBusConnection::sessionBus().interface();
    return iface && iface->isServiceRegistered(serviceName());
}

namespace {
void call(const QString &method, const QVariantList &args = {})
{
    QDBusMessage msg = QDBusMessage::createMethodCall(ClipboardService::serviceName(),
                                                      ClipboardService::objectPath(),
                                                      ClipboardService::serviceName(), method);
    if (!args.isEmpty())
        msg.setArguments(args);
    QDBusConnection::sessionBus().asyncCall(msg);
}
} // namespace

void ClipboardService::callToggle(const QString &screenName)
{
    call(QStringLiteral("toggle"), {screenName});
}

ClipboardService::ClipboardService(ClipboardWindow *window, QObject *parent)
    : QObject(parent)
    , m_window(window)
{
}

bool ClipboardService::registerOnBus()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected() || !bus.registerService(serviceName()))
        return false;
    return bus.registerObject(objectPath(), this, QDBusConnection::ExportScriptableSlots);
}

void ClipboardService::toggle(const QString &screenName)
{
    if (!m_window)
        return;
    if (m_window->isVisible())
        m_window->hideWindow();
    else
        m_window->showWindow(screenName);
}

void ClipboardService::hide()
{
    if (m_window)
        m_window->hideWindow();
}

void ClipboardService::quit()
{
    QCoreApplication::quit();
}
