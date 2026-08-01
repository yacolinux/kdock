#include "previewwindow.h"

#include "previewconfig.h"
#include "previewmanager.h"
#include "previewmodel.h"
#include "thumbnailcache.h"
#include "thumbnailimageprovider.h"

#include "desktopentry.h"
#include "iconprovider.h"
#include "theme.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QMargins>
#include <QProcess>
#include <QQmlContext>
#include <QQmlEngine>
#include <QScreen>
#include <QTimer>

#include <QtWaylandClient/private/qwaylandscreen_p.h>

namespace {
// zwlr_layer_surface_v1 anchor bits
constexpr uint AnchorTop = 1;
constexpr uint AnchorBottom = 2;
constexpr uint AnchorLeft = 4;
constexpr uint AnchorRight = 8;
} // namespace

PreviewWindow::PreviewWindow(PreviewConfig *config, Theme *theme, PreviewModel *model,
                             ThumbnailCache *cache, DesktopEntryIndex *apps, QObject *parent)
    : m_config(config)
    , m_theme(theme)
    , m_model(model)
    , m_cache(cache)
    , m_apps(apps)
{
    Q_UNUSED(parent);

    setColor(Qt::transparent);
    setFlags(Qt::FramelessWindowHint);
    setTitle(QStringLiteral("kdock-previews"));

    // Layer-shell properties must be in place before the platform window exists
    // (i.e. before show()).
    setProperty("kdock.layershell", true);
    setProperty("kdock.keyboardInteractivity", 0u); // none
    applyLayerProperties();

    connect(m_config, &PreviewConfig::edgeChanged, this, &PreviewWindow::applyLayerProperties);
    connect(m_config, &PreviewConfig::stripThicknessChanged, this,
            &PreviewWindow::applyLayerProperties);
    connect(m_config, &PreviewConfig::stripLengthChanged, this,
            &PreviewWindow::applyLayerProperties);
    connect(m_config, &PreviewConfig::alignmentChanged, this, &PreviewWindow::applyLayerProperties);
    connect(m_config, &PreviewConfig::screenMarginChanged, this,
            &PreviewWindow::applyLayerProperties);
    connect(m_config, &PreviewConfig::reserveSpaceChanged, this,
            &PreviewWindow::applyLayerProperties);
    connect(m_config, &PreviewConfig::autohideChanged, this, &PreviewWindow::applyLayerProperties);
    // The auto-fit shrinks the cards (and with them the panel: the exclusive
    // zone must follow, or the strip would either overlap windows or leave a
    // dead band — bug 2026-07-31).
    connect(m_model, &PreviewModel::effectiveThicknessChanged, this,
            &PreviewWindow::applyLayerProperties);

    // The wl_output is fixed at layer-surface creation, so moving to another
    // screen recreates the platform window. Runtime changes are coalesced (see
    // scheduleApplyScreen) to avoid tearing the surface down several times
    // during a hotplug burst; the initial placement is synchronous so the very
    // first surface lands on the right output.
    connect(qGuiApp, &QGuiApplication::screenAdded, this, &PreviewWindow::scheduleApplyScreen);
    connect(qGuiApp, &QGuiApplication::screenRemoved, this, &PreviewWindow::scheduleApplyScreen);
    connect(qGuiApp, &QGuiApplication::primaryScreenChanged, this,
            &PreviewWindow::scheduleApplyScreen);
    applyScreen();

    engine()->addImageProvider(QStringLiteral("thumb"), new ThumbnailImageProvider(m_cache));
    engine()->addImageProvider(QStringLiteral("icon"), new IconProvider);

    rootContext()->setContextProperty(QStringLiteral("config"), m_config);
    rootContext()->setContextProperty(QStringLiteral("theme"), m_theme);
    rootContext()->setContextProperty(QStringLiteral("previews"), m_model);
    rootContext()->setContextProperty(QStringLiteral("previewWindow"), this);
    rootContext()->setContextProperty(QStringLiteral("apps"), m_apps);

    // Content drives the surface size, exactly like DockWindow: the QML root
    // takes its stretched dimension from Window.width/height (what the
    // compositor configured) and its cross-axis one from config.stripThicknessPx.
    setResizeMode(QQuickView::SizeViewToRootObject);
    setSource(QUrl(QStringLiteral("qrc:/qml/PreviewStrip.qml")));
}

int PreviewWindow::thickness() const
{
    return m_model ? m_model->effectiveThicknessPx() : m_config->stripThicknessPx();
}

void PreviewWindow::scheduleApplyScreen()
{
    if (m_screenChangePending)
        return;
    m_screenChangePending = true;
    QTimer::singleShot(0, this, [this] {
        m_screenChangePending = false;
        applyScreen();
    });
}

void PreviewWindow::applyScreen()
{
    QScreen *target = nullptr;
    const QString wanted = m_config->screenName();
    if (!wanted.isEmpty()) {
        const auto screens = QGuiApplication::screens();
        for (QScreen *s : screens) {
            if (s->name() == wanted) {
                target = s;
                break;
            }
        }
    }
    if (!target)
        target = QGuiApplication::primaryScreen();
    if (!target)
        return;

    // Resolve the target's wl_output here and stash it on the window, so the
    // layer-shell integration binds the surface to exactly this output instead
    // of guessing from QWindow::screen() (which lags behind setScreen() right
    // after a destroy()+show() cycle).
    quintptr outputPtr = 0;
    if (auto *qws = dynamic_cast<QtWaylandClient::QWaylandScreen *>(target->handle()))
        outputPtr = reinterpret_cast<quintptr>(qws->output());
    setProperty("kdock.output", QVariant::fromValue(outputPtr));

    // Captures are requested at the resolution the card is drawn at.
    if (m_model)
        m_model->setTargetScale(target->devicePixelRatio());

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

void PreviewWindow::applyLayerProperties()
{
    uint anchor = AnchorLeft;
    QMargins margins;
    const int m = m_config->screenMargin();
    const bool horizontal = m_config->edge() == PreviewConfig::Bottom
                            || m_config->edge() == PreviewConfig::Top;
    switch (m_config->edge()) {
    case PreviewConfig::Bottom: anchor = AnchorBottom; margins.setBottom(m); break;
    case PreviewConfig::Top:    anchor = AnchorTop;    margins.setTop(m);    break;
    case PreviewConfig::Left:   anchor = AnchorLeft;   margins.setLeft(m);   break;
    case PreviewConfig::Right:  anchor = AnchorRight;  margins.setRight(m);  break;
    }

    if (m_config->stripLength() == 0) {
        // Whole edge: anchor both side edges so the compositor stretches the
        // surface (alignment is then meaningless).
        anchor |= horizontal ? (AnchorLeft | AnchorRight) : (AnchorTop | AnchorBottom);
    } else if (m_config->alignment() == PreviewConfig::Start) {
        anchor |= horizontal ? AnchorLeft : AnchorTop;
        if (horizontal) margins.setLeft(m); else margins.setTop(m);
    } else if (m_config->alignment() == PreviewConfig::End) {
        anchor |= horizontal ? AnchorRight : AnchorBottom;
        if (horizontal) margins.setRight(m); else margins.setBottom(m);
    }
    // Center: a single-edge anchor already centers the surface.

    setProperty("kdock.anchors", anchor);
    setProperty("kdock.margins", QVariant::fromValue(margins));
    setProperty("kdock.layer", 2u); // top
    // Autohide and "don't reserve" are independent settings, but a hidden strip
    // must never keep the space reserved.
    const bool reserve = m_config->reserveSpace() && !m_config->autohide();
    setProperty("kdock.exclusiveZone", reserve ? thickness() + m : 0);
}

void PreviewWindow::setHidden(bool hidden)
{
    if (m_hidden == hidden)
        return;
    m_hidden = hidden;

    // No point capturing windows nobody can see.
    if (m_model)
        m_model->setStripVisible(!hidden);

    if (!hidden) {
        setMask(QRegion());
        return;
    }

    constexpr int strip = 3; // hover strip that reveals the previews
    QRect r;
    switch (m_config->edge()) {
    case PreviewConfig::Bottom: r = QRect(0, height() - strip, width(), strip); break;
    case PreviewConfig::Top:    r = QRect(0, 0, width(), strip);                break;
    case PreviewConfig::Left:   r = QRect(0, 0, strip, height());               break;
    case PreviewConfig::Right:  r = QRect(width() - strip, 0, strip, height()); break;
    }
    setMask(QRegion(r));
}

void PreviewWindow::openSettings()
{
    if (m_manager)
        m_manager->showSettings();
}

void PreviewWindow::quit()
{
    QCoreApplication::quit();
}

void PreviewWindow::restart()
{
    QStringList args = QCoreApplication::arguments();
    if (!args.isEmpty())
        args.removeFirst();
    QProcess::startDetached(QCoreApplication::applicationFilePath(), args);
    QCoreApplication::quit();
}
