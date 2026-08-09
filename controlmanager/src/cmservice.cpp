#include "cmservice.h"

#include "cmsections.h"
#include "cmwindow.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QVariant>

QString CmService::serviceName()
{
    return QStringLiteral("org.kdock.ControlManager");
}

QString CmService::objectPath()
{
    return QStringLiteral("/ControlManager");
}

bool CmService::alreadyRunning()
{
    auto *iface = QDBusConnection::sessionBus().interface();
    return iface && iface->isServiceRegistered(serviceName());
}

namespace {
void call(const QString &method, const QVariantList &args = {})
{
    QDBusMessage msg = QDBusMessage::createMethodCall(CmService::serviceName(),
                                                      CmService::objectPath(),
                                                      CmService::serviceName(), method);
    if (!args.isEmpty())
        msg.setArguments(args);
    // asyncCall so a wedged instance cannot block the caller for the D-Bus
    // default of 25 s.
    QDBusConnection::sessionBus().asyncCall(msg);
}
} // namespace

void CmService::callToggle(const QString &screenName)
{
    call(QStringLiteral("toggle"), {screenName});
}

void CmService::callShow(const QString &screenName)
{
    call(QStringLiteral("show"), {screenName});
}

void CmService::callHide()
{
    call(QStringLiteral("hide"));
}

void CmService::callShowSettings()
{
    call(QStringLiteral("showSettings"));
}

void CmService::callShowSection(const QString &sectionId, const QString &screenName)
{
    call(QStringLiteral("showSection"), {sectionId, screenName});
}

void CmService::callQuit()
{
    call(QStringLiteral("quit"));
}

CmService::CmService(CmWindow *window, QObject *parent)
    : QObject(parent)
    , m_window(window)
{
}

bool CmService::registerOnBus()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return false;
    if (!bus.registerService(serviceName()))
        return false;
    return bus.registerObject(objectPath(), this, QDBusConnection::ExportScriptableSlots);
}

void CmService::toggle(const QString &screenName)
{
    if (!m_window)
        return;
    if (m_window->isVisible())
        m_window->hidePanel();
    else
        m_window->showOn(screenName);
}

void CmService::show(const QString &screenName)
{
    if (m_window)
        m_window->showOn(screenName);
}

void CmService::hide()
{
    if (m_window)
        m_window->hidePanel();
}

void CmService::showSection(const QString &sectionId, const QString &screenName)
{
    if (!m_window)
        return;
    // An unknown id would leave the tab bar pointing at nothing; treat it as
    // "just open" instead of failing.
    m_window->setCurrentTab(CmSections::exists(sectionId) ? sectionId : QString());
    m_window->showOn(screenName);
}

void CmService::showSettings()
{
    if (m_window)
        m_window->openSettings();
}

void CmService::quit()
{
    QCoreApplication::quit();
}
