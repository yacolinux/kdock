#include "screensaverwindow.h"

#include "desktopwallpapers.h"
#include "dockconfig.h"
#include "virtualdesktops.h"
#include "wallpaperfolder.h"

#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QMargins>
#include <QRandomGenerator>
#include <QScreen>
#include <QUrl>
#include <QWindow>
#include <QWebEngineSettings>

#include <QtWaylandClient/private/qwaylandscreen_p.h>
#include <QtWaylandClient/private/qwaylandwindow_p.h>

namespace {
constexpr uint AnchorAll = 1u | 2u | 4u | 8u;

QString jsArray(const QStringList &values)
{
    QJsonArray array;
    for (const QString &value : values)
        array.append(value);
    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}
}

ScreensaverWindow::ScreensaverWindow(const QString &screenName, VirtualDesktops *desktops,
                                     QWidget *parent)
    : QWebEngineView(parent)
    , m_screenName(screenName)
    , m_desktops(desktops)
{
    setWindowTitle(QStringLiteral("kdock Screensaver"));
    setWindowFlags(Qt::FramelessWindowHint | Qt::Tool);
    setAttribute(Qt::WA_DeleteOnClose, false);
    setContextMenuPolicy(Qt::NoContextMenu);
    settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessFileUrls, true);
    settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, false);
    // QWidget top-levels expose their QWindow lazily. Force that handle before
    // show(), then set the same pre-role properties used by DockWindow; the
    // layer-shell integration reads them when the window gets its shell role.
    qApp->setProperty("kdock.screensaverCreating", true);
    qApp->setProperty("kdock.screensaverLayer",
                      DockConfig::screensaverCoverDocks() ? 3u : 2u);
    winId();
    qApp->setProperty("kdock.screensaverCreating", false);
    if (QWindow *handle = windowHandle()) {
        handle->setProperty("kdock.layershell", true);
        handle->setProperty("kdock.keyboardInteractivity", 2u); // on-demand
        // KWin classifies unknown layer-shell scopes as Normal windows.  A
        // normal layer-shell surface can still end up below a panel even
        // when it requests the overlay layer.  Use the compositor's Dock
        // type and select the actual stacking layer independently below;
        // this keeps the saver above kdock when Cover Docks is enabled.
        handle->setProperty("kdock.scope", QStringLiteral("dock"));
        applyLayerProperties();
        // The QWidget handle may already have created an xdg-shell surface as
        // part of winId(). QWindow::destroy() leaves Qt's QWaylandWindow and
        // its selected shell surface alive, so it would keep the screensaver
        // as a normal window. Reset the private Wayland window explicitly;
        // the next show() then asks our integration to create a fresh
        // layer-shell surface with the properties above.
        if (auto *waylandWindow = dynamic_cast<QtWaylandClient::QWaylandWindow *>(handle->handle())) {
            waylandWindow->reset();
            // reset() drops the current shell role, while destroy() makes Qt
            // discard the platform window so the next show() runs the shell
            // integration selection again.
            handle->destroy();
        } else {
            handle->destroy();
        }
    } else {
        applyLayerProperties();
    }
    qApp->installEventFilter(this);
}

ScreensaverWindow::~ScreensaverWindow()
{
    qApp->removeEventFilter(this);
}

int ScreensaverWindow::currentWallpaperDesktop() const
{
    return m_desktops && m_desktops->currentPosition() > 0
               ? m_desktops->currentPosition() : 1;
}

QString ScreensaverWindow::currentWallpaperFolder() const
{
    return DesktopWallpapers::slideshowFolder(currentWallpaperDesktop(), m_screenName);
}

QString ScreensaverWindow::wallpaperHtml() const
{
    QStringList folders;
    const auto addFolder = [&folders](const QString &folder) {
        if (!folder.isEmpty() && !folders.contains(folder))
            folders << folder;
    };
    addFolder(currentWallpaperFolder());
    // Desktop 1 is often the user's active desktop, so inspect every saved
    // desktop for this same monitor without mixing in another monitor's folder.
    for (int desktop = 1; desktop <= DockConfig::kMaxDesktops; ++desktop) {
        addFolder(DesktopWallpapers::slideshowFolder(desktop, m_screenName));
    }
    QStringList files = WallpaperFolder::images(folders);
    if (files.isEmpty()) {
        for (int desktop = 1; desktop <= DockConfig::kMaxDesktops; ++desktop) {
            const QString image = DesktopWallpapers::imageFor(desktop, m_screenName);
            if (!image.isEmpty()) {
                files << image;
                break;
            }
        }
    }

    QStringList urls;
    for (const QString &file : files)
        urls << QUrl::fromLocalFile(file).toString(QUrl::FullyEncoded);
    const int interval = DockConfig::screensaverSlideshowIntervalSeconds();
    return QStringLiteral(R"HTML(<!doctype html><meta charset="utf-8">
<style>
html,body{margin:0;width:100%;height:100%;overflow:hidden;background:#000}
#a,#b{position:fixed;inset:0;width:100%;height:100%;object-fit:cover;transition:opacity 1.2s ease}
#b{opacity:0}
</style><img id="a"><img id="b"><script>
const images=%1, delay=%2*1000; let n=0, front='a';
function paint(){ if(!images.length)return; const next=front==='a'?'b':'a';
 document.getElementById(next).src=images[n];
 document.getElementById(next).style.opacity=1;
 document.getElementById(front).style.opacity=0; front=next; n=(n+1)%images.length; }
paint(); if(images.length>1)setInterval(paint,delay);
if(!images.length){document.body.innerHTML='<div style="height:100%;display:grid;place-items:center;color:#aaa;font:20px sans-serif">No hay wallpapers configurados</div>';}
</script>)HTML").arg(jsArray(urls)).arg(interval);
}

QString ScreensaverWindow::afterDarkHtml(const QString &pageOverride) const
{
    static const QStringList pages = {
        QStringLiteral("bouncing-ball.html"), QStringLiteral("fade-out.html"),
        QStringLiteral("fish.html"),
        QStringLiteral("flying-toasters.html"), QStringLiteral("globe.html"),
        QStringLiteral("hard-rain.html"), QStringLiteral("logo.html"),
        QStringLiteral("messages.html"), QStringLiteral("messages2.html"),
        QStringLiteral("rainstorm.html"), QStringLiteral("spotlight.html"),
        QStringLiteral("warp.html")};
    QString page = pageOverride;
    if (!pages.contains(page))
        page = pages.at(int(QRandomGenerator::global()->bounded(uint(pages.size()))));

    const QString local = DockConfig::screensaverAfterDarkPath();
    if (!local.isEmpty()) {
        const QFileInfo selected(QDir(local).filePath(page));
        if (selected.isFile())
            return QUrl::fromLocalFile(selected.absoluteFilePath()).toString(QUrl::FullyEncoded);
    }
    return QStringLiteral("https://bryanbraun.github.io/after-dark-css/all/%1")
        .arg(page);
}

void ScreensaverWindow::applyLayerProperties()
{
    if (QWindow *handle = windowHandle()) {
        handle->setProperty("kdock.anchors", AnchorAll);
        handle->setProperty("kdock.layer", DockConfig::screensaverCoverDocks() ? 3u : 2u);
        handle->setProperty("kdock.exclusiveZone", -1);
        handle->setProperty("kdock.margins", QVariant::fromValue(QMargins()));
    }
}

void ScreensaverWindow::updateScreen()
{
    QScreen *target = nullptr;
    for (QScreen *screen : QGuiApplication::screens()) {
        if (screen->name() == m_screenName) {
            target = screen;
            break;
        }
    }
    if (!target)
        return;

    quintptr outputPtr = 0;
    if (auto *qws = dynamic_cast<QtWaylandClient::QWaylandScreen *>(target->handle()))
        outputPtr = reinterpret_cast<quintptr>(qws->output());
    winId();
    QWindow *handle = windowHandle();
    if (!handle)
        return;
    handle->setProperty("kdock.output", QVariant::fromValue(outputPtr));
    const bool recreate = outputPtr ? outputPtr != m_boundOutput
                                    : handle->screen() != target;
    m_boundOutput = outputPtr;
    if (recreate && handle)
        handle->destroy();
    if (windowHandle())
        windowHandle()->setScreen(target);
    setGeometry(target->geometry());
}

void ScreensaverWindow::showSaver(int engine, const QString &afterDarkPage)
{
    updateScreen();
    applyLayerProperties();
    const int selectedEngine = engine < 0 ? DockConfig::screensaverEngine() : engine;
    // ScreensaverManager polls configuration while idle. Keep the current
    // automatic page/slideshow alive across those polls; otherwise a random
    // After Dark page would be replaced every second.
    if (isVisible() && engine < 0 && afterDarkPage.isEmpty()
        && selectedEngine == m_activeEngine)
        return;
    m_activeEngine = selectedEngine;
    if (selectedEngine == 1)
        load(QUrl(afterDarkHtml(afterDarkPage)));
    else
        setHtml(wallpaperHtml(), QUrl::fromLocalFile(QStringLiteral("/")));
    show();
    raise();
}

void ScreensaverWindow::hideSaver()
{
    hide();
    stop();
    m_activeEngine = -1;
}

void ScreensaverWindow::refreshConfig()
{
    applyLayerProperties();
}

bool ScreensaverWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (isVisible() && event && (event->type() == QEvent::MouseButtonPress
                                 || event->type() == QEvent::KeyPress
                                 || event->type() == QEvent::TouchBegin
                                 || event->type() == QEvent::Wheel)) {
        auto *widget = qobject_cast<QWidget *>(watched);
        if (widget && widget->window() == this) {
            emit userDismissed();
            return true;
        }
    }
    return QWebEngineView::eventFilter(watched, event);
}
