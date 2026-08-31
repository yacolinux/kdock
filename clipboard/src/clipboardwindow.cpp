#include "clipboardwindow.h"

#include "clipboardhistory.h"
#include "iconprovider.h"

#include <QCoreApplication>
#include <QEvent>
#include <QGuiApplication>
#include <QQmlContext>
#include <QQmlEngine>
#include <QResizeEvent>
#include <QScreen>
#include <QSettings>
#include <QStandardPaths>

ClipboardWindow::ClipboardWindow(ClipboardHistory *history, QWindow *parent)
    : QQuickView(nullptr, parent)
    , m_history(history)
{
    setTitle(QStringLiteral("kdock Clipboard"));
    setFlag(Qt::Window, true);
    setResizeMode(QQuickView::SizeRootObjectToView);
    setMinimumSize(QSize(320, 220));

    QSettings settings(settingsPath(), QSettings::IniFormat);
    m_alwaysOnTop = settings.value(QStringLiteral("alwaysOnTop"), false).toBool();
    m_closeAfterCopy = settings.value(QStringLiteral("closeAfterCopy"), false).toBool();
    const int width = qBound(320, settings.value(QStringLiteral("width"), 480).toInt(), 4000);
    const int height = qBound(220, settings.value(QStringLiteral("height"), 600).toInt(), 4000);
    resize(width, height);
    setFlag(Qt::WindowStaysOnTopHint, m_alwaysOnTop);

    engine()->addImageProvider(QStringLiteral("icon"), new IconProvider);
    rootContext()->setContextProperty(QStringLiteral("clipboardHistory"), m_history);
    rootContext()->setContextProperty(QStringLiteral("win"), this);
    setSource(QUrl(QStringLiteral("qrc:/qml/Clipboard.qml")));
}

QString ClipboardWindow::settingsPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
           + QStringLiteral("/kdock/clipboard.conf");
}

void ClipboardWindow::saveGeometrySettings()
{
    QSettings settings(settingsPath(), QSettings::IniFormat);
    settings.setValue(QStringLiteral("width"), width());
    settings.setValue(QStringLiteral("height"), height());
}

void ClipboardWindow::setAlwaysOnTop(bool on)
{
    if (m_alwaysOnTop == on)
        return;
    m_alwaysOnTop = on;
    QSettings(settingsPath(), QSettings::IniFormat).setValue(QStringLiteral("alwaysOnTop"), on);
    setFlag(Qt::WindowStaysOnTopHint, on);
    if (isVisible())
        show();
    emit alwaysOnTopChanged();
}

void ClipboardWindow::setCloseAfterCopy(bool on)
{
    if (m_closeAfterCopy == on)
        return;
    m_closeAfterCopy = on;
    QSettings(settingsPath(), QSettings::IniFormat).setValue(QStringLiteral("closeAfterCopy"), on);
    emit closeAfterCopyChanged();
}

void ClipboardWindow::hideWindow()
{
    hide();
}

void ClipboardWindow::closeWindow()
{
    QCoreApplication::quit();
}

void ClipboardWindow::showWindow(const QString &screenName)
{
    QScreen *target = nullptr;
    if (!screenName.isEmpty()) {
        for (QScreen *screen : QGuiApplication::screens()) {
            if (screen->name() == screenName) {
                target = screen;
                break;
            }
        }
    }
    if (!target)
        target = screen() ? screen() : QGuiApplication::primaryScreen();
    if (target && screen() != target)
        setScreen(target);

    show();
    raise();
    requestActivate();
}

bool ClipboardWindow::event(QEvent *event)
{
    if (event->type() == QEvent::Close) {
        QCoreApplication::quit();
        return true;
    }
    return QQuickView::event(event);
}

void ClipboardWindow::resizeEvent(QResizeEvent *event)
{
    QQuickView::resizeEvent(event);
    saveGeometrySettings();
}
