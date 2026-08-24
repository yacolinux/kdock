#include "cmservice.h"

#include "cmsections.h"
#include "cmwindow.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QVariant>

namespace {
QString s_busScreen;

// Bus name elements must match [A-Za-z0-9_]; connectors carry '-' (DP-1).
QString sanitize(const QString &s)
{
    QString out = s;
    for (QChar &c : out)
        if (!c.isLetterOrNumber() && c != QLatin1Char('_'))
            c = QLatin1Char('_');
    return out;
}
} // namespace

void CmService::setBusScreen(const QString &screen)
{
    s_busScreen = screen;
}

QString CmService::interfaceName()
{
    return QStringLiteral("org.kdock.Desktop");
}

QString CmService::serviceName()
{
    return s_busScreen.isEmpty()
               ? interfaceName()
               : interfaceName() + QLatin1Char('.') + sanitize(s_busScreen);
}

QString CmService::objectPath()
{
    return QStringLiteral("/Desktop");
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
                                                      CmService::interfaceName(), method);
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

void CmService::callReloadConfig()
{
    call(QStringLiteral("reloadConfig"));
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

void CmService::reloadConfig()
{
    if (m_window)
        m_window->reloadConfig();
}

void CmService::quit()
{
    QCoreApplication::quit();
}
