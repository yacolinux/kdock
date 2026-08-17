#include "dockwindow.h"

#include "apppreviews.h"
#include "apprestart.h"
#include "brightnesscontrol.h"
#include "batterycontrol.h"
#include "clockwidget.h"
#include "clockwidget2.h"
#include "dockconfig.h"
#include "dockgeometry.h"
#include "dockmanager.h"
#include "dockmodel.h"
#include "desktopentry.h"
#include "appmenu.h"
#include "iconcolorprovider.h"
#include "iconprovider.h"
#include "overviewcontrol.h"
#include "desktopcontrol.h"
#include "activewindowcontrol.h"
#include "maxmincontrol.h"
#include "monitorcontrol.h"
#include "wallpapercontrol.h"
#include "clipboardhistory.h"
#include "diskscontrol.h"
#include "appearancecontrol.h"
#include "autocolorscheme.h"
#include "networkcontrol.h"
#include "relanzadoresmanager.h"
#include "scriptrunnersmanager.h"
#include "powercontrol.h"
#include "windowmonitor.h"
#include "settingsdialog.h"
#include "systraymodel.h"
#include "systrayimageprovider.h"
#include "thumbnailimageprovider.h"
#include "controlmanagerlauncher.h"
#include "weathercontrol.h"
#include "weatherlauncher.h"
#include "tilemenulauncher.h"
#include "virtualdesktops.h"
#include "theme.h"
#include "volumecontrol.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QProcess>
#include <QMargins>
#include <QMessageBox>
#include <QScreen>
#include <QQmlContext>
#include <QQmlEngine>
#include <QTimer>

#include <QtWaylandClient/private/qwaylandscreen_p.h>

namespace {
// zwlr_layer_surface_v1 anchor bits
constexpr uint AnchorTop = 1;
constexpr uint AnchorBottom = 2;
constexpr uint AnchorLeft = 4;
constexpr uint AnchorRight = 8;
} // namespace

DockWindow::DockWindow(DockConfig *config, Theme *theme, DockModel *model, DesktopEntryIndex *apps,
                       VolumeControl *volume, ClockWidget *clock, ClockWidget2 *clock2,
                       BrightnessControl *brightness, BatteryControl *battery,
                       OverviewControl *overview,
                       DesktopControl *desktopControl, MonitorControl *monitorControl,
                       MaxMinControl *maxmin, ActiveWindowControl *activeWindow,
                       WallpaperControl *wallpaperControl, PowerControl *power,
                       SystrayModel *systrayModel, SystrayHost *systrayHost,
                       RelanzadoresManager *relanzadores,
                       ScriptRunnersManager *scriptRunners,
                       ClipboardHistory *clipboardHistory,
                       DisksControl *disks, NetworkControl *network,
                       AppearanceControl *appearance, WindowMonitor *monitor,
                       VirtualDesktops *desktops, WeatherControl *weather,
                       AutoColorScheme *autoColors, AppPreviews *appPreviews)
    : m_config(config)
    , m_theme(theme)
    , m_model(model)
    , m_apps(apps)
    , m_systrayHost(systrayHost)
    , m_relanzadores(relanzadores)
    , m_scriptRunners(scriptRunners)
    , m_clipboardHistory(clipboardHistory)
    , m_monitor(monitor)
    , m_overview(overview)
    , m_desktopControl(desktopControl)
    , m_monitorControl(monitorControl)
    , m_maxmin(maxmin)
    , m_activeWindow(activeWindow)
    , m_wallpaperControl(wallpaperControl)
    , m_power(power)
    , m_appearance(appearance)
    , m_desktops(desktops)
    , m_autoColors(autoColors)
    , m_appPreviews(appPreviews)
{
    setColor(Qt::transparent);
    setFlags(Qt::FramelessWindowHint);
    setTitle(QStringLiteral("kdock"));

    // Layer-shell properties must be in place before the platform window
    // exists (i.e. before show()).
    setProperty("kdock.layershell", true);
    setProperty("kdock.keyboardInteractivity", 0u); // none
    applyLayerProperties();

    connect(m_config, &DockConfig::edgeChanged, this, &DockWindow::applyLayerProperties);
    connect(m_config, &DockConfig::iconSizeChanged, this, &DockWindow::applyLayerProperties);
    // Covers the app-icon label settings, which change the dock thickness (and
    // so the exclusive zone) without touching the icon size.
    connect(m_config, &DockConfig::dockThicknessChanged, this, &DockWindow::applyLayerProperties);
    connect(m_config, &DockConfig::screenMarginChanged, this, &DockWindow::applyLayerProperties);
    connect(m_config, &DockConfig::hideModeChanged, this, &DockWindow::applyLayerProperties);
    connect(m_config, &DockConfig::hideModeChanged, this, &DockWindow::updateWindowsOverlap);
    connect(m_config, &DockConfig::panelModeChanged, this, &DockWindow::applyLayerProperties);
    connect(m_config, &DockConfig::compactChanged, this, &DockWindow::applyLayerProperties);
    connect(m_config, &DockConfig::alignmentChanged, this, &DockWindow::applyLayerProperties);
    connect(m_config, &DockConfig::dockLengthChanged, this, &DockWindow::applyLayerProperties);
    // A selectable-apps widget removed from the Layout tab would otherwise leave
    // its model behind, still connected and still rebuilding itself.
    connect(m_config, &DockConfig::widgetOrderChanged, this,
            &DockWindow::dropUnusedAppsModels);

    // The wl_output is fixed at layer-surface creation, so moving to
    // another screen recreates the platform window. Runtime changes are
    // coalesced (see scheduleApplyScreen) to avoid tearing the surface down
    // and rebuilding it several times during monitor hotplug enumeration,
    // which can leave the dock on the wrong output or drop the Wayland
    // connection. The initial placement below is applied synchronously so the
    // very first surface is created on the right output (before show()).
    connect(m_config, &DockConfig::screenNameChanged, this, &DockWindow::scheduleApplyScreen);
    connect(qGuiApp, &QGuiApplication::screenAdded, this, &DockWindow::scheduleApplyScreen);
    connect(qGuiApp, &QGuiApplication::screenRemoved, this, &DockWindow::scheduleApplyScreen);
    connect(qGuiApp, &QGuiApplication::primaryScreenChanged, this, &DockWindow::scheduleApplyScreen);
    applyScreen();

    engine()->addImageProvider(QStringLiteral("icon"), new IconProvider);
    if (m_systrayHost)
        engine()->addImageProvider(QStringLiteral("systray"),
                                   new SystrayImageProvider(m_systrayHost));
    // Window captures for the apps block's hover previews. One provider per
    // engine (the engine takes ownership), all of them over the one shared cache.
    if (m_appPreviews)
        engine()->addImageProvider(QStringLiteral("thumb"),
                                   m_appPreviews->createImageProvider());
    rootContext()->setContextProperty(QStringLiteral("iconColors"), new IconColorProvider(this));
    rootContext()->setContextProperty(QStringLiteral("config"), m_config);
    rootContext()->setContextProperty(QStringLiteral("theme"), m_theme);
    rootContext()->setContextProperty(QStringLiteral("dockModel"), m_model);
    rootContext()->setContextProperty(QStringLiteral("dockWindow"), this);
    rootContext()->setContextProperty(QStringLiteral("volume"), volume);
    rootContext()->setContextProperty(QStringLiteral("clock"), clock);
    rootContext()->setContextProperty(QStringLiteral("clock2"), clock2);
    rootContext()->setContextProperty(QStringLiteral("brightness"), brightness);
    rootContext()->setContextProperty(QStringLiteral("battery"), battery);
    rootContext()->setContextProperty(QStringLiteral("overview"), m_overview);
    rootContext()->setContextProperty(QStringLiteral("desktopControl"), m_desktopControl);
    rootContext()->setContextProperty(QStringLiteral("monitorControl"), m_monitorControl);
    rootContext()->setContextProperty(QStringLiteral("maxmin"), m_maxmin);
    rootContext()->setContextProperty(QStringLiteral("activeWindow"), m_activeWindow);
    rootContext()->setContextProperty(QStringLiteral("wallpaperControl"), m_wallpaperControl);
    rootContext()->setContextProperty(QStringLiteral("power"), m_power);
    rootContext()->setContextProperty(QStringLiteral("systray"), systrayModel);
    rootContext()->setContextProperty(QStringLiteral("relanzadores"), m_relanzadores);
    rootContext()->setContextProperty(QStringLiteral("scriptRunners"), m_scriptRunners);
    rootContext()->setContextProperty(QStringLiteral("clipboardHistory"), m_clipboardHistory);
    rootContext()->setContextProperty(QStringLiteral("disks"), disks);
    rootContext()->setContextProperty(QStringLiteral("network"), network);
    rootContext()->setContextProperty(QStringLiteral("appearance"), appearance);
    rootContext()->setContextProperty(QStringLiteral("virtualDesktops"), m_desktops);
    // ColorAuto: the "colorauto" widget calls generateNow() on it. The object
    // is process-wide (it owns the ping-pong of the two generated schemes), so
    // it travels through Shared like every other backend rather than being
    // built here.
    rootContext()->setContextProperty(QStringLiteral("autoColors"), m_autoColors);
    rootContext()->setContextProperty(QStringLiteral("appPreviews"), m_appPreviews);
    rootContext()->setContextProperty(QStringLiteral("dockIsPrimary"), m_primary);
    rootContext()->setContextProperty(QStringLiteral("apps"), m_apps);
    rootContext()->setContextProperty(QStringLiteral("showdesktop"), monitor);
    rootContext()->setContextProperty(QStringLiteral("appMenu"),
                                      new AppMenu(m_apps, m_config, this));
    m_tileLauncher = new TileMenuLauncher(this);
    rootContext()->setContextProperty(QStringLiteral("tileLauncher"), m_tileLauncher);
    // Same shape as the tile menu's: a thin, stateless launcher for another
    // binary, so it is built here instead of travelling through Shared.
    m_cmLauncher = new ControlManagerLauncher(this);
    rootContext()->setContextProperty(QStringLiteral("cmLauncher"), m_cmLauncher);
    // The weather window is another binary, but its *data* is this process':
    // the widget draws the temperature from the shared WeatherControl and only
    // uses the launcher when clicked.
    m_weatherLauncher = new WeatherLauncher(this);
    rootContext()->setContextProperty(QStringLiteral("weatherLauncher"), m_weatherLauncher);
    rootContext()->setContextProperty(QStringLiteral("weather"), weather);

    // The autohide mask is a rectangle derived from the surface size, so it goes
    // stale whenever the dock resizes (an icon-size change, or coming back from
    // another virtual desktop with a brand-new surface).
    // The mask is the autohide strip while hidden, and the surface minus the
    // transparent separators while visible: both follow the surface size.
    connect(this, &QWindow::widthChanged, this,
            [this] { if (m_hidden || !m_gapRects.isEmpty()) applyHiddenMask(); });
    connect(this, &QWindow::heightChanged, this,
            [this] { if (m_hidden || !m_gapRects.isEmpty()) applyHiddenMask(); });

    // Dodge mode inputs: the window list (and every window's geometry), the
    // current desktop, and the dock's own rectangle — which moves with the
    // surface size and with every geometry setting.
    if (m_monitor) {
        connect(m_monitor, &WindowMonitor::windowAdded, this, [this](AbstractWindow *w) {
            watchWindow(w);
            updateWindowsOverlap();
        });
        connect(m_monitor, &WindowMonitor::windowRemoved, this,
                &DockWindow::updateWindowsOverlap);
        for (AbstractWindow *w : std::as_const(m_monitor->windows))
            watchWindow(w);
    }
    if (m_desktops)
        connect(m_desktops, &VirtualDesktops::currentChanged, this,
                &DockWindow::updateWindowsOverlap);
    connect(this, &QWindow::widthChanged, this, &DockWindow::updateWindowsOverlap);
    connect(this, &QWindow::heightChanged, this, &DockWindow::updateWindowsOverlap);
    connect(m_config, &DockConfig::edgeChanged, this, &DockWindow::updateWindowsOverlap);
    connect(m_config, &DockConfig::alignmentChanged, this, &DockWindow::updateWindowsOverlap);
    connect(m_config, &DockConfig::screenMarginChanged, this, &DockWindow::updateWindowsOverlap);
    // The mode decides both whether the dodge runs at all and how the margin is
    // split between the anchor and the inset, i.e. where dockRect() lands.
    connect(m_config, &DockConfig::hideModeChanged, this, &DockWindow::updateWindowsOverlap);

    setResizeMode(QQuickView::SizeViewToRootObject); // content drives surface size
    setSource(QUrl(QStringLiteral("qrc:/qml/Dock.qml")));

    // After the event loop starts, KWin sends window_with_uuid events for
    // all existing windows, and each window is only exposed once its initial
    // state burst completes. Several staggered re-syncs catch windows that
    // were still settling at rebuild() time (empty appId, events not yet
    // dispatched); this matters on multi-monitor sessions where the initial
    // toplevel burst is larger and slower to drain.
    if (m_model && m_monitor) {
        for (int delay : {200, 700, 1500}) {
            QTimer::singleShot(delay, this, [this] {
                m_model->syncWindows();
            });
        }
    }
}

int DockWindow::thickness() const
{
    // Single source of truth, shared with Dock.qml's root.thickness (which
    // reads the same config.dockThickness): icon/label cell + padding.
    return m_config->dockThickness();
}

QRect DockWindow::dockRect() const
{
    // The arithmetic (and the reason a centered dock reports the whole band)
    // lives in dockgeometry.h, where it can be tested without a window.
    QScreen *s = screen();
    if (!s)
        return {};
    // The *surface* rect, so it has to use the margin the surface is really
    // anchored with: in a hiding mode that is 0, and the size already carries
    // the screen margin as the transparent inset band (DockConfig::edgeInset()).
    // The band counts as dock for the dodge test — it is part of the input
    // region, so a window that reaches it is a window the dock sits on top of.
    return kdock::dockRectFor(s->geometry(), m_config->edge(), m_config->alignment(),
                              m_config->anchorEdgeMargin(), QSize(width(), height()));
}

void DockWindow::watchWindow(AbstractWindow *window)
{
    if (!window)
        return;
    // `changed` covers geometry, minimized and the desktop list, which is
    // everything the overlap test reads.
    connect(window, &AbstractWindow::changed, this, &DockWindow::updateWindowsOverlap);
}

void DockWindow::updateWindowsOverlap()
{
    bool overlap = false;
    if (m_config->hideMode() == DockConfig::DodgeWindows && m_monitor) {
        const QRect dock = dockRect();
        const QString desktop = m_desktops ? m_desktops->currentDesktopId() : QString();
        if (!dock.isEmpty()) {
            for (const AbstractWindow *w : std::as_const(m_monitor->windows)) {
                if (w->minimized || w->skipTaskbar || w->geometry.isEmpty())
                    continue;
                // An empty desktop list means "on all desktops" in the
                // plasma-window protocol, not "on none" — see AbstractWindow.
                if (!desktop.isEmpty() && !w->desktops.isEmpty()
                    && !w->desktops.contains(desktop))
                    continue;
                if (w->geometry.intersects(dock)) {
                    overlap = true;
                    break;
                }
            }
        }
    }
    if (m_windowsOverlap == overlap)
        return;
    m_windowsOverlap = overlap;
    // Test seam. The dodge state is not on D-Bus and cannot be seen from a
    // screenshot with any certainty, so this line is the only way an automated
    // check can assert it (tests/live/dodge.sh, the one thing about the mode
    // that Xvfb cannot exercise: there is no window protocol there). Silent
    // unless asked for.
    if (qEnvironmentVariableIsSet("KDOCK_DEBUG_DODGE")) {
        const QRect r = dockRect();
        qInfo("kdock: dodge overlap=%d rect=%d,%d %dx%d windows=%lld", overlap ? 1 : 0,
              r.x(), r.y(), r.width(), r.height(),
              m_monitor ? qlonglong(m_monitor->windows.size()) : -1LL);
    }
    emit windowsOverlapChanged();
}

void DockWindow::scheduleApplyScreen()
{
    // Coalesce a burst of screen events into one deferred applyScreen(): during
    // startup/hotplug Qt emits screenAdded/Removed/primaryScreenChanged in quick
    // succession, and recreating the surface on each would flicker the dock
    // across outputs (and risks a protocol error).
    if (m_screenChangePending)
        return;
    m_screenChangePending = true;
    QTimer::singleShot(0, this, [this] {
        m_screenChangePending = false;
        applyScreen();
    });
}

void DockWindow::applyScreen()
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

    // Resolve the target's wl_output authoritatively here and stash it on the
    // window, so the layer-shell integration binds the surface to exactly this
    // output instead of guessing from QWindow::screen() (which may still lag
    // behind setScreen() right after a destroy()+show() cycle, landing the dock
    // on the wrong monitor). See LayerSurface's output resolution.
    quintptr outputPtr = 0;
    if (auto *qws = dynamic_cast<QtWaylandClient::QWaylandScreen *>(target->handle()))
        outputPtr = reinterpret_cast<quintptr>(qws->output());
    setProperty("kdock.output", QVariant::fromValue(outputPtr));

    // Recreate when the platform window is missing, or when the target's real
    // wl_output differs from the one the current surface is bound to. We track
    // the bound output ourselves (m_boundOutput) instead of comparing against
    // QWindow::screen(), which stays stale after a layer-surface recreation and
    // would otherwise wedge the dock on one monitor after the first move.
    // (Fall back to a screen-identity check when the output can't be resolved.)
    const bool needsRecreate = !handle()
        || (outputPtr ? outputPtr != m_boundOutput : screen() != target);
    m_boundOutput = outputPtr;
    if (!needsRecreate)
        return;

    // Layer-shell binds the wl_output at surface creation, so switching output
    // means destroying the platform window and recreating it on the new screen.
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

void DockWindow::applyLayerProperties()
{
    uint anchor = AnchorBottom;
    QMargins margins;
    const int m = m_config->effectiveMargin();
    // In a hiding mode the surface hugs the screen edge for good and pays the
    // margin as extra thickness instead (DockConfig::edgeInset(), drawn as a
    // transparent band by Dock.qml): the reveal strip is a few pixels at the
    // *surface's* edge (applyHiddenMask()), so with an anchor margin those
    // pixels sit inside the screen and the dock only comes back once the
    // pointer moves away from the edge by exactly the margin — the erratic
    // reveal reported 2026-08-10.
    //
    // This used to be `m_hidden ? 0 : m`, i.e. the surface was re-anchored on
    // every hide and every reveal, and that is the bump reported 2026-08-15:
    // the reveal restores the margin at the *start* of the slide, so the dock
    // jumps `m` px mid-animation — and a pointer resting on the last pixels of
    // the screen (where the strip is, and where the pointer lands when it is
    // thrown at the edge) ends up outside the surface it just revealed, loses
    // the hover, and the dock hides, un-anchors, is hovered again… forever.
    // Only the dock's own edge; the alignment margins below keep the real
    // margin, or the dock would slide along the edge every time it hides.
    const int edgeM = m_config->anchorEdgeMargin();
    const bool horizontal = m_config->edge() == DockConfig::Bottom
                            || m_config->edge() == DockConfig::Top;
    switch (m_config->edge()) {
    case DockConfig::Bottom: anchor = AnchorBottom; margins.setBottom(edgeM); break;
    case DockConfig::Top:    anchor = AnchorTop;    margins.setTop(edgeM);    break;
    case DockConfig::Left:   anchor = AnchorLeft;   margins.setLeft(edgeM);   break;
    case DockConfig::Right:  anchor = AnchorRight;  margins.setRight(edgeM);  break;
    }

    // A dock that spans the whole edge anchors the full side (edge + both
    // corners): KWin only derives an exclusive edge from the anchor for a
    // single edge or a whole side, so edge + ONE corner (the fixed-length
    // start/end alignment below) silently loses the strut and maximized
    // windows pass under the dock. At 100 % length the corner anchor is
    // meaningless anyway — and its margin would push the surface off-screen.
    const bool fullEdge = (m_config->panelMode() && m_config->dockLength() == 0)
                          || m_config->dockLength() == 100;
    if (fullEdge) {
        // Panel mode (100%): also anchor both side edges so the surface
        // stretches across the whole screen edge (alignment is done in QML).
        anchor |= horizontal ? (AnchorLeft | AnchorRight) : (AnchorTop | AnchorBottom);
    } else if (m_config->dockLength() > 0) {
        // Fixed-length mode: anchor only the edge plus one side corner for
        // start/end alignment. A single-edge anchor centers the surface; the
        // QML side sets the exact pixel size via Window width/height.
        if (m_config->alignment() == DockConfig::Start) {
            anchor |= horizontal ? AnchorLeft : AnchorTop;
            if (horizontal) margins.setLeft(m); else margins.setTop(m);
        } else if (m_config->alignment() == DockConfig::End) {
            anchor |= horizontal ? AnchorRight : AnchorBottom;
            if (horizontal) margins.setRight(m); else margins.setBottom(m);
        }
    } else {
        // Floating mode: a single-edge anchor centers the surface; anchor a
        // side corner for start/end alignment.
        if (m_config->alignment() == DockConfig::Start) {
            anchor |= horizontal ? AnchorLeft : AnchorTop;
            if (horizontal) margins.setLeft(m); else margins.setTop(m);
        } else if (m_config->alignment() == DockConfig::End) {
            anchor |= horizontal ? AnchorRight : AnchorBottom;
            if (horizontal) margins.setRight(m); else margins.setBottom(m);
        }
    }

    setProperty("kdock.anchors", anchor);
    // The dock's own edge, as a wlr-layer-shell anchor value: the layer
    // surface asks the compositor (protocol v5, see layershell.cpp) to apply
    // the exclusive zone on this edge even when the anchor is a corner.
    uint exclusiveEdge = 0;
    switch (m_config->edge()) {
    case DockConfig::Bottom: exclusiveEdge = AnchorBottom; break;
    case DockConfig::Top:    exclusiveEdge = AnchorTop;    break;
    case DockConfig::Left:   exclusiveEdge = AnchorLeft;   break;
    case DockConfig::Right:  exclusiveEdge = AnchorRight;  break;
    }
    setProperty("kdock.exclusiveEdge", exclusiveEdge);
    setProperty("kdock.margins", QVariant::fromValue(margins));
    setProperty("kdock.layer", 2u); // top
    // Only the plain always-visible mode reserves a strut. Auto-hide, dodge and
    // "windows go below" all let maximized windows use the whole edge.
    setProperty("kdock.exclusiveZone", m_config->reservesSpace() ? thickness() + m : 0);
}

void DockWindow::setHidden(bool hidden)
{
    if (m_hidden == hidden)
        return;
    m_hidden = hidden;
    // Only the input region changes here. The surface stays exactly where it
    // is: see the edgeM comment in applyLayerProperties() for why re-anchoring
    // on this transition is the one thing this must not do.
    applyHiddenMask();
}

void DockWindow::setGapRects(const QVariantList &rects)
{
    QList<QRect> parsed;
    parsed.reserve(rects.size());
    for (const QVariant &v : rects) {
        const QVariantMap m = v.toMap();
        const QRect r(m.value(QStringLiteral("x")).toInt(), m.value(QStringLiteral("y")).toInt(),
                      m.value(QStringLiteral("width")).toInt(),
                      m.value(QStringLiteral("height")).toInt());
        if (!r.isEmpty())
            parsed.append(r);
    }
    if (parsed == m_gapRects)
        return;
    m_gapRects = parsed;
    applyHiddenMask();
}

void DockWindow::applyHiddenMask()
{
    if (!m_hidden) {
        // An empty region means "no mask at all", which is the whole surface —
        // and the fast path for a dock without transparent separators.
        if (m_gapRects.isEmpty()) {
            setMask(QRegion());
            return;
        }
        QRegion region(QRect(0, 0, width(), height()));
        for (const QRect &r : std::as_const(m_gapRects))
            region -= r;
        setMask(region);
        return;
    }

    constexpr int strip = 3; // hover strip that reveals the dock
    QRect r;
    switch (m_config->edge()) {
    case DockConfig::Bottom: r = QRect(0, height() - strip, width(), strip); break;
    case DockConfig::Top:    r = QRect(0, 0, width(), strip);                break;
    case DockConfig::Left:   r = QRect(0, 0, strip, height());               break;
    case DockConfig::Right:  r = QRect(width() - strip, 0, strip, height()); break;
    }
    setMask(QRegion(r));
}

void DockWindow::setDeskVisible(bool visible)
{
    if (!visible) {
        hide();
        return;
    }

    // hide() tore the layer surface down, and applyScreen() deliberately skips
    // rebuilding it while the window is invisible (see its `wasVisible`), so the
    // output this dock should bind to may have changed behind our back. Resolve
    // it again before showing, or the dock comes back on the wrong monitor.
    applyScreen();
    if (!isVisible())
        show();
    // The input mask belongs to the platform window, which is a new one: without
    // this an autohidden dock would come back fully clickable and swallow every
    // click meant for the window underneath. The size only settles once the
    // compositor has configured the fresh surface, and the mask is built from
    // it, hence the deferred second pass (the resize hook in the constructor
    // catches the rest).
    applyHiddenMask();
    QTimer::singleShot(0, this, [this] { applyHiddenMask(); });
}

void DockWindow::setPrimary(bool primary)
{
    m_primary = primary;
    rootContext()->setContextProperty(QStringLiteral("dockIsPrimary"), primary);
}

DockWindow::~DockWindow()
{
    // The dialog has no parent, so without this it outlives the dock that owns
    // it: it stays on screen editing a dock that is gone, it leaks (one more per
    // round trip if the dock is one of the per-virtual-desktop copies, which are
    // destroyed and rebuilt on every switch), and worse, removeDock() deletes
    // the DockConfig it still points at.
    //
    // hide() + deleteLater() and not delete: we are routinely destroyed from
    // inside a handler of the dialog itself (its Docks tab is one of the two
    // places that remove a dock), so the object cannot go away under its own
    // stack frame. The hide() is what stops the user from touching it in the
    // meantime.
    if (m_dialog) {
        m_dialog->hide();
        m_dialog->deleteLater();
        m_dialog = nullptr;
    }
}

void DockWindow::openSettings()
{
    if (!m_dialog)
        m_dialog = new SettingsDialog(m_config, m_apps,
                m_manager ? m_manager->systrayHost() : m_systrayHost,
                m_manager ? m_manager->relanzadores() : m_relanzadores,
                m_manager, m_theme,
                m_manager ? m_manager->audio() : nullptr, m_appearance);
    m_dialog->show();
    m_dialog->raise();
    m_dialog->activateWindow();
}

void DockWindow::retranslate()
{
    engine()->retranslate();
    if (!m_dialog)
        return;
    // The dialog is normally where the change was made, so it is rebuilt and
    // reopened on the Traducciones tab. deleteLater(): this runs from inside the
    // old dialog's own signal handler.
    const bool wasVisible = m_dialog->isVisible();
    m_dialog->deleteLater();
    m_dialog = nullptr;
    if (!wasVisible)
        return;
    openSettings();
    m_dialog->showTranslationsTab();
}

void DockWindow::openSettingsToDock()
{
    openSettings();
    if (m_dialog)
        m_dialog->showMonitorsTab(m_config->dockId());
}

void DockWindow::deleteDock()
{
    if (!m_manager || m_config->dockId().isEmpty())
        return;
    const QString dockId = m_config->dockId();
    const auto answer = QMessageBox::question(
        nullptr, tr("Borrar este Dock"),
        tr("¿Eliminar este dock?\n\nSe dejará de mostrar y su archivo de "
           "configuración se borrará."),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;
    // Defer the removal: removeDock() destroys this window synchronously, and
    // we are inside the QML handler that invoked us.
    DockManager *manager = m_manager;
    QTimer::singleShot(0, manager, [manager, dockId] {
        manager->removeDock(dockId);
    });
}

void DockWindow::createEmptyDock()
{
    if (!m_manager)
        return;
    QString err;
    const QString created = m_manager->createEmptyDock(m_config->screenName(), &err);
    if (created.isEmpty()) {
        QMessageBox::warning(nullptr, tr("Crear dock vacío"), err);
        return;
    }
    // Land on the new dock's row: it is born unnamed and on a free edge, so the
    // first thing to do with it is name it / move it.
    openSettings();
    if (m_dialog)
        m_dialog->showMonitorsTab(created);
}

void DockWindow::moveToNextMonitor()
{
    if (!m_manager || m_config->dockId().isEmpty())
        return;
    const QString dockId = m_config->dockId();
    DockManager *manager = m_manager;
    // Defer: moveDockToNextMonitor() -> sync() destroys this dock's instance
    // (it is disabled on its old monitor), and we are inside the QML click
    // handler that invoked us — same pattern as deleteDock().
    QTimer::singleShot(0, this, [this, manager, dockId] {
        const QString newId = manager->moveDockToNextMonitor(dockId);
        // Land the (already open) settings dialog on the new dock so the Docks
        // tab reflects the move instead of pointing at the renamed-away one.
        if (!newId.isEmpty() && m_dialog)
            m_dialog->showMonitorsTab(newId);
    });
}

void DockWindow::copyToNextMonitor()
{
    if (!m_manager || m_config->dockId().isEmpty())
        return;
    const QString dockId = m_config->dockId();
    DockManager *manager = m_manager;
    // Deferred like the move: this dock survives, but sync() still rebuilds the
    // dock list from inside the QML click handler that invoked us.
    QTimer::singleShot(0, this, [this, manager, dockId] {
        const QString newId = manager->copyDockToNextMonitor(dockId);
        // Land the (already open) settings dialog on the copy, which is the one
        // the user is going to want to edit.
        if (!newId.isEmpty() && m_dialog)
            m_dialog->showMonitorsTab(newId);
    });
}

void DockWindow::openAudioSettings()
{
    openSettings();
    m_dialog->showAudioTab();
}

void DockWindow::openVideoSettings()
{
    openSettings();
    m_dialog->showVideoTab();
}

void DockWindow::openColorAutoSettings()
{
    openSettings();
    m_dialog->showColorAutoTab();
}

void DockWindow::openNetworkSettings()
{
    openSettings();
    m_dialog->showNetworkTab();
}

void DockWindow::dropUnusedAppsModels()
{
    const QStringList alive = m_config->appsWidgetTokens();
    for (auto it = m_appsModels.begin(); it != m_appsModels.end();) {
        if (alive.contains(it.key())) {
            ++it;
            continue;
        }
        // Not just memory: the model is connected to the window monitor and
        // rebuilds itself on every config change, for a widget nobody draws.
        delete it.value();
        it = m_appsModels.erase(it);
    }
}

QObject *DockWindow::appsModelFor(const QString &token)
{
    if (token.isEmpty() || !DockConfig::isAppsWidgetToken(token))
        return m_model;
    DockModel *&model = m_appsModels[token];
    if (!model) {
        model = new DockModel(m_config, m_apps, m_monitor, m_desktops, token, this);
        // Same deferred re-scan the dock's own model gets: the window list of
        // the compositor is not complete when the surface is built.
        QTimer::singleShot(0, model, &DockModel::syncWindows);
    }
    return model;
}

void DockWindow::quit()
{
    QCoreApplication::quit();
}

void DockWindow::restart()
{
    // The accessories go down with the dock and come back with it: restarting
    // only this process would leave them on the binary they were started with,
    // which right after an install is the old one (see apprestart.h).
    kdock::restartAll();
}

void DockWindow::setKeyboardInteractive(bool on)
{
    // 1 = exclusive (compositor grants keyboard focus to the layer surface
    // and its popups unconditionally), 0 = none.
    // The layershell integration picks up the property change and re-commits.
    setProperty("kdock.keyboardInteractivity", on ? 1u : 0u);
    if (on)
        requestActivate();
}
