#include "dockwindow.h"

#include "brightnesscontrol.h"
#include "batterycontrol.h"
#include "clockwidget.h"
#include "clockwidget2.h"
#include "dockconfig.h"
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
#include "networkcontrol.h"
#include "relanzadoresmanager.h"
#include "scriptrunnersmanager.h"
#include "powercontrol.h"
#include "windowmonitor.h"
#include "settingsdialog.h"
#include "systraymodel.h"
#include "systrayimageprovider.h"
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
                       VirtualDesktops *desktops)
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
    connect(m_config, &DockConfig::autohideChanged, this, &DockWindow::applyLayerProperties);
    connect(m_config, &DockConfig::panelModeChanged, this, &DockWindow::applyLayerProperties);
    connect(m_config, &DockConfig::compactChanged, this, &DockWindow::applyLayerProperties);
    connect(m_config, &DockConfig::alignmentChanged, this, &DockWindow::applyLayerProperties);
    connect(m_config, &DockConfig::dockLengthChanged, this, &DockWindow::applyLayerProperties);

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
    rootContext()->setContextProperty(QStringLiteral("dockIsPrimary"), m_primary);
    rootContext()->setContextProperty(QStringLiteral("apps"), m_apps);
    rootContext()->setContextProperty(QStringLiteral("showdesktop"), monitor);
    rootContext()->setContextProperty(QStringLiteral("appMenu"),
                                      new AppMenu(m_apps, m_config, this));
    m_tileLauncher = new TileMenuLauncher(this);
    rootContext()->setContextProperty(QStringLiteral("tileLauncher"), m_tileLauncher);

    // The autohide mask is a rectangle derived from the surface size, so it goes
    // stale whenever the dock resizes (an icon-size change, or coming back from
    // another virtual desktop with a brand-new surface).
    connect(this, &QWindow::widthChanged, this, [this] { if (m_hidden) applyHiddenMask(); });
    connect(this, &QWindow::heightChanged, this, [this] { if (m_hidden) applyHiddenMask(); });

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
    const bool horizontal = m_config->edge() == DockConfig::Bottom
                            || m_config->edge() == DockConfig::Top;
    switch (m_config->edge()) {
    case DockConfig::Bottom: anchor = AnchorBottom; margins.setBottom(m); break;
    case DockConfig::Top:    anchor = AnchorTop;    margins.setTop(m);    break;
    case DockConfig::Left:   anchor = AnchorLeft;   margins.setLeft(m);   break;
    case DockConfig::Right:  anchor = AnchorRight;  margins.setRight(m);  break;
    }

    if (m_config->panelMode() && m_config->dockLength() == 0) {
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
    setProperty("kdock.margins", QVariant::fromValue(margins));
    setProperty("kdock.layer", 2u); // top
    setProperty("kdock.exclusiveZone", m_config->autohide() ? 0 : thickness() + m);
}

void DockWindow::setHidden(bool hidden)
{
    if (m_hidden == hidden)
        return;
    m_hidden = hidden;
    applyHiddenMask();
}

void DockWindow::applyHiddenMask()
{
    if (!m_hidden) {
        setMask(QRegion());
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

void DockWindow::openAudioSettings()
{
    openSettings();
    m_dialog->showAudioTab();
}

void DockWindow::openNetworkSettings()
{
    openSettings();
    m_dialog->showNetworkTab();
}

void DockWindow::quit()
{
    QCoreApplication::quit();
}

void DockWindow::restart()
{
    // Preserve the CLI arguments this instance was started with (e.g.
    // --screen <name>) so the relaunched dock lands on the same output.
    QStringList args = QCoreApplication::arguments();
    if (!args.isEmpty())
        args.removeFirst();
    QProcess::startDetached(QCoreApplication::applicationFilePath(), args);
    QCoreApplication::quit();
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
