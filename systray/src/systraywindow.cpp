#include "systraywindow.h"

#include "dockconfig.h"
#include "iconprovider.h"
#include "systrayconfig.h"
#include "systrayimageprovider.h"
#include "systraymodel.h"
#include "systray.h"
#include "systraysettingsdialog.h"
#include "theme.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QGuiApplication>
#include <QMargins>
#include <QQmlContext>
#include <QQmlEngine>
#include <QScreen>
#include <QSettings>
#include <QTimer>

#include <QtWaylandClient/private/qwaylandscreen_p.h>

namespace {
// zwlr_layer_surface_v1 anchor bits.
constexpr uint AnchorTop = 1;
constexpr uint AnchorBottom = 2;
constexpr uint AnchorLeft = 4;
constexpr uint AnchorRight = 8;
// Focus can bounce for a frame or two while the compositor maps the surface.
constexpr qint64 kFocusGraceMs = 400;
} // namespace

SystrayWindow::SystrayWindow(SystrayConfig *config, Theme *theme, SystrayModel *model,
                             SystrayHost *host)
    : m_config(config)
    , m_theme(theme)
    , m_model(model)
    , m_host(host)
{
    setTitle(QStringLiteral("kdock Systray"));
    setFlags(Qt::FramelessWindowHint);
    // The QML paints its own background at config.backgroundOpacity, so the
    // surface itself has to show through.
    setColor(Qt::transparent);
    setResizeMode(QQuickView::SizeRootObjectToView);

    // Layer-shell properties must be in place before the platform window exists
    // (before show()).
    setProperty("kdock.layershell", true);
    setProperty("kdock.keyboardInteractivity", 0u);
    applyLayerProperties();

    connect(m_config, &SystrayConfig::windowChanged, this, [this] {
        applyLayerProperties();
        applySize();
    });
    connect(m_config, &SystrayConfig::settingsChanged, this, &SystrayWindow::iconSuffixChanged);
    connect(qGuiApp, &QGuiApplication::screenAdded, this, &SystrayWindow::scheduleApplyScreen);
    connect(qGuiApp, &QGuiApplication::screenRemoved, this, &SystrayWindow::scheduleApplyScreen);
    connect(qGuiApp, &QGuiApplication::primaryScreenChanged, this,
            &SystrayWindow::scheduleApplyScreen);
    if (m_theme)
        connect(m_theme, &Theme::changed, this, &SystrayWindow::iconSuffixChanged);

    engine()->addImageProvider(QStringLiteral("icon"), new IconProvider);
    engine()->addImageProvider(QStringLiteral("systray"), new SystrayImageProvider(m_host));

    rootContext()->setContextProperty(QStringLiteral("systrayConfig"), m_config);
    rootContext()->setContextProperty(QStringLiteral("config"), m_config);
    rootContext()->setContextProperty(QStringLiteral("theme"), m_theme);
    rootContext()->setContextProperty(QStringLiteral("systray"), m_model);
    rootContext()->setContextProperty(QStringLiteral("win"), this);

    applyScreen();
    applySize();
    setSource(QUrl(QStringLiteral("qrc:/qml/Systray.qml")));

    connect(this, &QWindow::activeChanged, this, &SystrayWindow::onActiveChanged);
}

QString SystrayWindow::iconSuffix() const
{
    QString suffix = QStringLiteral("@") + QString::number(m_theme ? m_theme->revision() : 0);
    const QColor base = m_theme ? m_theme->background() : QColor(Qt::black);
    const qreal luma = 0.299 * base.redF() + 0.587 * base.greenF() + 0.114 * base.blueF();
    QSettings shared(DockConfig::settingsFilePath(), QSettings::IniFormat);
    const QString set = luma < 0.5
        ? shared.value(QStringLiteral("widgetIconThemeDarkBg"),
                       QStringLiteral("breeze-dark")).toString()
        : shared.value(QStringLiteral("widgetIconThemeLightBg"),
                       QStringLiteral("breeze")).toString();
    if (!set.isEmpty())
        suffix += QLatin1Char('@') + set;
    return suffix;
}

void SystrayWindow::scheduleApplyScreen()
{
    if (m_screenChangePending)
        return;
    m_screenChangePending = true;
    QTimer::singleShot(0, this, [this] {
        m_screenChangePending = false;
        applyScreen();
        applySize();
    });
}

void SystrayWindow::applyScreen()
{
    QScreen *target = nullptr;
    if (!m_screenName.isEmpty()) {
        const auto screens = QGuiApplication::screens();
        for (QScreen *s : screens) {
            if (s->name() == m_screenName) {
                target = s;
                break;
            }
        }
    }
    if (!target)
        target = QGuiApplication::primaryScreen();
    if (!target)
        return;

    // Resolve the target's wl_output and stash it so the layer-shell integration
    // binds the surface to exactly this output (QWindow::screen() lags behind a
    // destroy()+show() cycle).
    quintptr outputPtr = 0;
    if (auto *qws = dynamic_cast<QtWaylandClient::QWaylandScreen *>(target->handle()))
        outputPtr = reinterpret_cast<quintptr>(qws->output());
    setProperty("kdock.output", QVariant::fromValue(outputPtr));

    const bool needsRecreate = !handle()
        || (outputPtr ? outputPtr != m_boundOutput : screen() != target);
    m_boundOutput = outputPtr;
    if (!needsRecreate)
        return;

    const bool wasVisible = isVisible();
    if (handle()) {
        hide();
        destroy();
    }
    setScreen(target);
    if (wasVisible) {
        create();
        show();
    }
}

void SystrayWindow::applySize()
{
    QScreen *s = screen() ? screen() : QGuiApplication::primaryScreen();
    const QRect geo = s ? s->geometry() : QRect(0, 0, 1920, 1080);
    resize(m_config->windowWidthFor(geo.width()), m_config->windowHeightFor(geo.height()));
}

uint SystrayWindow::layerAnchors(QMargins *margins) const
{
    uint anchor = AnchorTop;
    QMargins m4;
    const int m = m_config->screenMargin();
    const bool horizontal = m_config->edge() == SystrayConfig::Top
                            || m_config->edge() == SystrayConfig::Bottom;
    switch (m_config->edge()) {
    case SystrayConfig::Top:    anchor = AnchorTop;    m4.setTop(m);    break;
    case SystrayConfig::Bottom: anchor = AnchorBottom; m4.setBottom(m); break;
    case SystrayConfig::Left:   anchor = AnchorLeft;   m4.setLeft(m);   break;
    case SystrayConfig::Right:  anchor = AnchorRight;  m4.setRight(m);  break;
    }

    if (m_config->alignment() == SystrayConfig::Start) {
        anchor |= horizontal ? AnchorLeft : AnchorTop;
        if (horizontal) m4.setLeft(m); else m4.setTop(m);
    } else if (m_config->alignment() == SystrayConfig::End) {
        anchor |= horizontal ? AnchorRight : AnchorBottom;
        if (horizontal) m4.setRight(m); else m4.setBottom(m);
    }

    if (margins)
        *margins = m4;
    return anchor;
}

void SystrayWindow::applyLayerProperties()
{
    QMargins margins;
    const uint anchor = layerAnchors(&margins);
    setProperty("kdock.anchors", anchor);
    setProperty("kdock.margins", QVariant::fromValue(margins));
    setProperty("kdock.layer", 2u); // top
    // Always zero: this window comes and goes. Reserving space would rearrange
    // the user's maximized windows on every open.
    setProperty("kdock.exclusiveZone", 0);
}

void SystrayWindow::showOn(const QString &screenName)
{
    if (!screenName.isEmpty() && screenName != m_screenName) {
        m_screenName = screenName;
        applyScreen();
    }
    applySize();

    m_shownAt = QDateTime::currentMSecsSinceEpoch();
    // Exclusive keyboard while up: Esc closes, and losing it is the "clicked
    // somewhere else" signal a layer surface has no other way to hear.
    setProperty("kdock.keyboardInteractivity", 1u);
    show();
    requestActivate();
}

void SystrayWindow::hideWindow()
{
    // Give the keyboard back before going away, or the compositor keeps routing
    // keys at a surface nobody can see.
    setProperty("kdock.keyboardInteractivity", 0u);
    // hide(), never destroy(): the whole point of staying resident is that the
    // SNI host keeps collecting items and the next open is instant.
    hide();
}

bool SystrayWindow::blockingClose() const
{
    if (m_menuOpen)
        return true;
    return m_settingsDialog && m_settingsDialog->isVisible();
}

void SystrayWindow::setMenuOpen(bool on)
{
    m_menuOpen = on;
    // When the menu closes the tray window may have gone inactive under it (the
    // user clicked outside): the focus-loss path was blocked while the menu was
    // up, so re-run it now, once the compositor has settled the focus.
    if (!on && !m_config->keepOpen() && m_config->closeOnFocusLoss()) {
        QTimer::singleShot(0, this, [this] {
            if (isVisible() && !isActive() && !blockingClose())
                hideWindow();
        });
    }
}

void SystrayWindow::onActiveChanged()
{
    if (isActive() || !isVisible())
        return;
    if (m_config->keepOpen() || !m_config->closeOnFocusLoss())
        return;
    if (blockingClose())
        return;
    if (QDateTime::currentMSecsSinceEpoch() - m_shownAt < kFocusGraceMs)
        return;
    hideWindow();
}

void SystrayWindow::openSettings()
{
    if (!m_settingsDialog)
        m_settingsDialog = new SystraySettingsDialog(m_config);
    m_settingsDialog->show();
    m_settingsDialog->raise();
    m_settingsDialog->activateWindow();
}

void SystrayWindow::reloadConfig()
{
    m_config->reloadFromDisk();
}
