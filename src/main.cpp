#include <QApplication>
#include <QDir>
#include <QImage>
#include <QQmlEngine>
#include <QQuickStyle>
#include <QScreen>
#include <QSocketNotifier>
#include <QTimer>
#include <QWidget>
#include <QWindow>
#include <QtPlugin>

#include <csignal>
#include <sys/socket.h>
#include <unistd.h>

#include <functional>

#include "apppreviews.h"
#include "apprestart.h"
#include "clockwidget.h"
#include "clockwidget2.h"
#include "configarchive.h"
#include "clipboardhistory.h"
#include "desktopentry.h"
#include "desktopmaximize.h"
#include "dockconfig.h"
#include "dockmanager.h"
#include "dockmodel.h"
#include "dockwindow.h"
#include "brightnesscontrol.h"
#include "screenbrightness.h"
#include "batterycontrol.h"
#include "overviewcontrol.h"
#include "desktopcontrol.h"
#include "virtualdesktops.h"
#include "activewindowcontrol.h"
#include "maxmincontrol.h"
#include "monitorcontrol.h"
#include "wallpapercontrol.h"
#include "desktopwallpapers.h"
#include "lxqtwallpapers.h"
#include "session.h"
#include "powercontrol.h"
#include "previewslauncher.h"
#include "controlmanagerlauncher.h"
#include "dockservice.h"
#include "globalshortcut.h"
#include "tilemenulauncher.h"
#include "diskscontrol.h"
#include "appearancecontrol.h"
#include "autocolorscheme.h"
#include "qtcompat.h"
#include "keyboardcontrol.h"
#include "kwinscripts.h"
#include "darkmodeappearance.h"
#include "networkcontrol.h"
#include "weatherconfig.h"
#include "weathercontrol.h"
#include "relanzadorconfig.h"
#include "relanzadormodel.h"
#include "relanzadoresmanager.h"
#include "scriptrunnerconfig.h"
#include "scriptrunnersmanager.h"
#include "screenshotsource.h"
#include "systray.h"
#include "systraymodel.h"
#include "thumbnailcache.h"
#include "thumbnailsource.h"
#include "theme.h"
#include "translations.h"
#include "volumecontrol.h"
#include "audiocontrol.h"
#include "windowmonitor.h"

// In-tree layer-shell integration (see layershell.cpp)
Q_IMPORT_PLUGIN(KDockLayerShellPlugin)

namespace {

int g_quitSignalFd[2] = {-1, -1};

void onQuitSignal(int)
{
    // Async-signal-safe on purpose: one byte down the pipe, and the notifier
    // below does the real work back on the event loop.
    const char byte = 1;
    const ssize_t written = ::write(g_quitSignalFd[1], &byte, 1);
    Q_UNUSED(written);
}

// Qt installs no handler for SIGTERM, so a logout (or a plain `kill`) tears the
// process down without ever emitting aboutToQuit. That matters since the
// wallpapers-per-desktop feature restores KDE's own wallpaper from that hook:
// without this, being killed on desktop 2 would leave our static image up until
// the next run put it back (DesktopWallpapers::start).
//
// `onQuit` runs on the event loop and is expected **not to return** — see the
// call site: unwinding the whole application here would tear the QQmlEngines
// down under live bindings and flood the journal with ~1800 "property of null"
// warnings on every logout, which is precisely what a SIGTERM used to avoid by
// killing the process outright.
void installQuitSignalHandler(QCoreApplication *app, std::function<void()> onQuit)
{
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, g_quitSignalFd) != 0)
        return;
    auto *notifier = new QSocketNotifier(g_quitSignalFd[0], QSocketNotifier::Read, app);
    QObject::connect(notifier, &QSocketNotifier::activated, app,
                     [notifier, onQuit = std::move(onQuit)] {
                         notifier->setEnabled(false);
                         char byte = 0;
                         const ssize_t read = ::read(g_quitSignalFd[0], &byte, 1);
                         Q_UNUSED(read);
                         onQuit();
                     });

    struct sigaction action = {};
    action.sa_handler = onQuitSignal;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_RESTART;
    for (int signalNumber : {SIGTERM, SIGINT, SIGHUP})
        ::sigaction(signalNumber, &action, nullptr);
}

} // namespace

// One-shot mode used by next-wall.sh: advance the KDE slideshow wallpaper on a
// single monitor, then exit. Reuses the dock's per-monitor engine
// (WallpaperControl) so we never reimplement the geometry/next-image logic.
//   --screen NAME  target that connector directly (path B: the dock knows it).
//   (no --screen)  open a 1x1 probe window; the compositor maps it onto the
//                  active output and we advance whichever monitor that is
//                  (path A: fallback for global shortcut / terminal launches).
static int runNextWallpaperCli(QApplication &app, const QString &screenName)
{
    // The probe window closing must not tear down the event loop before the
    // async D-Bus round-trips finish.
    app.setQuitOnLastWindowClosed(false);

    WallpaperControl wallpaper;
    if (!wallpaper.available())
        return 1;

    if (screenName.isEmpty()) {
        auto *probe = new QWidget;
        probe->setFixedSize(1, 1);
        probe->setWindowFlags(Qt::FramelessWindowHint | Qt::Tool
                              | Qt::WindowStaysOnTopHint);
        probe->setAttribute(Qt::WA_TranslucentBackground);
        probe->setAttribute(Qt::WA_DeleteOnClose);
        probe->show();
        // Give the compositor a moment to assign the surface to an output,
        // then read back which monitor it landed on and advance that one.
        QTimer::singleShot(250, &app, [&wallpaper, probe]() {
            QString name;
            if (probe->windowHandle() && probe->windowHandle()->screen())
                name = probe->windowHandle()->screen()->name();
            probe->close();
            wallpaper.nextWallpaper(name); // empty name -> global-shortcut fallback
        });
    } else {
        wallpaper.nextWallpaper(screenName);
    }

    // Keep the loop alive long enough for the chained (read -> write) async
    // evaluateScript calls to be dispatched, then quit.
    QTimer::singleShot(screenName.isEmpty() ? 2000 : 1600, &app, &QApplication::quit);
    return app.exec();
}

// --dump-captures <dir>: ask KWin for one capture of every window it reports,
// write them as PNGs and exit, without building a single dock.
//
// kdock-previews has the same flag, and it does *not* answer for this binary:
// KWin grants org.kde.KWin.ScreenShot2 per executable (it resolves the caller's
// /proc/<pid>/exe against an installed .desktop), so one of them being authorized
// says nothing about the other. This is what separates "kdock is not allowed to
// capture" from "the hover previews have a bug in the QML" — from the outside
// both look like nothing appearing.
//
// It goes through the dock's own WindowMonitor, i.e. the very objects that feed
// the previews, so a uuid missing here is a uuid missing there.
static int runDumpCapturesCli(QApplication &app, const QString &dir)
{
    if (!app.platformName().contains(QLatin1String("wayland"))) {
        qWarning("kdock: --dump-captures needs the Wayland session (the window list "
                 "comes from a Wayland protocol).");
        return 1;
    }
    QDir().mkpath(dir);

    auto *monitor = WindowMonitor::create(&app);
    if (!monitor) {
        qWarning("kdock: the compositor offers no window-management protocol; without it "
                 "there is nothing to capture. On KWin this means the installed "
                 "kdock.desktop is missing X-KDE-Wayland-Interfaces.");
        return 1;
    }
    auto *source = new ScreenShotSource(&app);
    auto *pending = new int(0);
    auto *done = new int(0);

    QObject::connect(source, &ThumbnailSource::thumbnailReady, &app,
                     [dir, done](const QString &uuid, const QImage &image) {
                         // The braces of KWin's uuid are legal in a file name but
                         // noisy; the cache's own normalization is what the image
                         // provider uses, so reuse it here too.
                         const QString path = dir + QLatin1Char('/')
                             + ThumbnailCache::normalizeKey(uuid) + QStringLiteral(".png");
                         const bool ok = image.save(path);
                         qInfo("captured %s -> %dx%d %s", qPrintable(uuid), image.width(),
                               image.height(), ok ? "saved" : "SAVE FAILED");
                         ++*done;
                     });
    QObject::connect(source, &ThumbnailSource::thumbnailFailed, &app,
                     [done](const QString &uuid, const QString &reason) {
                         qWarning("failed  %s -> %s", qPrintable(uuid), qPrintable(reason));
                         ++*done;
                     });

    // Let KWin's initial window burst drain, then queue one capture per window
    // (ScreenShotSource serializes them).
    QTimer::singleShot(1200, &app, [monitor, source, pending] {
        qInfo("kdock: %lld window(s) reported by the compositor",
              qint64(monitor->windows.size()));
        for (AbstractWindow *w : std::as_const(monitor->windows)) {
            qInfo("  %s  [%s]%s%s%s  %dx%d+%d+%d  %s",
                  w->uuid.isEmpty() ? "(no uuid)" : qPrintable(w->uuid), qPrintable(w->appId),
                  w->minimized ? " MIN" : "", w->activated ? " ACTIVE" : "",
                  w->skipTaskbar ? " SKIPTASKBAR" : "", w->geometry.width(),
                  w->geometry.height(), w->geometry.x(), w->geometry.y(), qPrintable(w->title));
            if (w->uuid.isEmpty())
                continue; // wlr backend: no handle, no capture
            source->request(w->uuid, QSize()); // full size: show what KWin hands over
            ++*pending;
        }
        if (*pending == 0)
            QCoreApplication::quit();
    });

    // Hard stop: one capture can block on a client that stopped drawing.
    QTimer::singleShot(15000, &app, &QCoreApplication::quit);
    auto *poll = new QTimer(&app);
    poll->setInterval(200);
    QObject::connect(poll, &QTimer::timeout, &app, [pending, done] {
        if (*pending > 0 && *done >= *pending)
            QCoreApplication::quit();
    });
    poll->start();

    return app.exec();
}

int main(int argc, char *argv[])
{
    // Must be decided before QGuiApplication constructs the platform
    // integration. Our shell integration falls back to xdg-shell for
    // every window that is not the dock surface.
    if (qEnvironmentVariableIsSet("WAYLAND_DISPLAY")
        && !qEnvironmentVariableIsSet("QT_WAYLAND_SHELL_INTEGRATION")) {
        qputenv("QT_WAYLAND_SHELL_INTEGRATION", "kdock-layershell");
    }

    QApplication app(argc, argv);

    // Qt has now loaded our layer-shell integration. Drop the variable so the
    // apps we launch (see DesktopEntryIndex::launch) don't inherit it and fail
    // with 'No shell integration named "kdock-layershell" found'.
    qunsetenv("QT_WAYLAND_SHELL_INTEGRATION");

    app.setApplicationName(QStringLiteral("kdock"));
    app.setOrganizationName(QStringLiteral("kdock"));
    app.setApplicationDisplayName(QStringLiteral("kdock"));
    app.setDesktopFileName(QStringLiteral("kdock"));
    app.setQuitOnLastWindowClosed(false);

    // Apply a preset before the first config read of the process. The Presets
    // tab relaunches us with this flag instead of copying the .conf files
    // itself: the dock it is running in still has a QSettings open on every one
    // of them, and QSettings flushes its own dirty keys on the way out — the
    // freshly applied file would be partially overwritten by the config it just
    // replaced. Here nothing has read anything yet.
    {
        const QStringList args = app.arguments();
        const int at = args.indexOf(QLatin1String(kdock::kApplyPresetFlag));
        if (at >= 0 && at + 1 < args.size()) {
            QString err;
            if (!ConfigArchive::importFrom(args.at(at + 1), &err))
                qWarning("kdock: could not apply preset %s: %s",
                         qPrintable(args.at(at + 1)), qPrintable(err));
        }
    }

    const QString style = DockConfig::qtStyle();
    if (!style.isEmpty())
        app.setStyle(style);

    // QML files import QtQuick.Controls (no explicit style), so this call
    // actually takes effect. Fusion is the safe desktop-style default
    // everywhere (Xvfb, bare wlroots); org.kde.desktop needs a KDE session.
    // Only known QQC2 style names are passed through — widget-only styles
    // like Breeze / Oxygen / Windows fall back to Fusion.
    {
        const auto qqc2Style = [](const QString &s) -> QString {
            if (s.isEmpty() || s == QStringLiteral("Breeze"))
                return QStringLiteral("Fusion");
            if (s == QStringLiteral("Basic") || s == QStringLiteral("Fusion")
                || s == QStringLiteral("Material") || s == QStringLiteral("Universal")
                || s == QStringLiteral("Imagine"))
                return s;
            return QStringLiteral("Fusion");
        };
        QQuickStyle::setStyle(qqc2Style(style));
    }

    // One-shot CLI (next-wall.sh): advance a monitor's slideshow and exit,
    // before building any dock.
    {
        const QStringList args = app.arguments();
        if (args.contains(QStringLiteral("--next-wallpaper"))) {
            QString screen;
            const int si = args.indexOf(QStringLiteral("--screen"));
            if (si >= 0 && si + 1 < args.size())
                screen = args.at(si + 1);
            return runNextWallpaperCli(app, screen);
        }
        // One-shot diagnostic: prove the ScreenShot2 path (authorization
        // included) without any dock in the way. See runDumpCapturesCli.
        {
            const int i = args.indexOf(QStringLiteral("--dump-captures"));
            if (i >= 0) {
                const QString dir = i + 1 < args.size() ? args.at(i + 1)
                                                        : QStringLiteral("/tmp/kdock-thumbs");
                return runDumpCapturesCli(app, dir);
            }
        }
        // One-shot diagnostic probe: dump window state on every virtual-desktop
        // switch without building any dock.
        if (args.contains(QStringLiteral("--probe-maximize"))) {
            const bool way = app.platformName().contains(QLatin1String("wayland"));
            auto *probeMonitor = way ? WindowMonitor::create(&app) : nullptr;
            VirtualDesktops probeDesktops;
            return probeMonitor
                ? DesktopMaximize::runProbe(&probeDesktops, probeMonitor)
                : 1;
        }
    }

    // Register types so QML can handle them
    qmlRegisterUncreatableType<RelanzadorModel>("kdock", 1, 0, "RelanzadorModel", QStringLiteral("Access via RelanzadoresManager"));
    qmlRegisterUncreatableType<RelanzadorConfig>("kdock", 1, 0, "RelanzadorConfig", QStringLiteral("Access via RelanzadoresManager"));
    qmlRegisterUncreatableType<ScriptRunnerConfig>("kdock", 1, 0, "ScriptRunnerConfig", QStringLiteral("Access via ScriptRunnersManager"));

    const bool wayland = app.platformName().contains(QLatin1String("wayland"));

    // First of the singletons: it installs the QTranslator every tr()/qsTr()
    // below goes through, so nothing built before it would be translated.
    Translations translations;

    // System-wide singletons shared by every dock instance.
    Theme theme;
    DesktopEntryIndex apps;
    WindowMonitor *monitor = wayland ? WindowMonitor::create(&app) : nullptr;
    VolumeControl volume;
    AudioControl audio;
    BrightnessControl brightness;
    // PowerDevil's per-monitor brightness. The widget drives exactly one of
    // these displays (which one is set in the VideoEnergía tab); brightnessctl
    // stays as the fallback for a session without PowerDevil.
    ScreenBrightness screenBrightness;
    brightness.setScreens(&screenBrightness);
    BatteryControl battery;
    OverviewControl overview;
    DesktopControl desktopControl;
    MonitorControl monitorControl;
    MaxMinControl maxmin;
    VirtualDesktops virtualDesktops;
    ActiveWindowControl activeWindow(monitor, &virtualDesktops);
    WallpaperControl wallpaperControl;
    DesktopWallpapers desktopWallpapers(&virtualDesktops);
    // The LXQt half of the same feature. Constructed always and started only
    // under LXQt (below): while it has not been started it owns no surfaces and
    // has touched nothing.
    LxqtWallpapers lxqtWallpapers(&virtualDesktops);
    // active(), not enabled(): the key is shared with the Plasma engine, so
    // asking it would make the "next wallpaper" widget hand the job to an
    // engine that is not running — under Plasma, with the per-desktop feature
    // on, the button would silently do nothing.
    wallpaperControl.setAlternateEngine([&lxqtWallpapers] { return lxqtWallpapers.active(); },
                                        [&lxqtWallpapers](const QString &s) {
                                            lxqtWallpapers.advance(s);
                                        });
    QObject::connect(&lxqtWallpapers, &LxqtWallpapers::activeChanged, &wallpaperControl,
                     [&wallpaperControl] { wallpaperControl.refreshAvailability(); });
    PowerControl power;
    SystrayHost systray;
    RelanzadoresManager relanzadores(&apps);
    ScriptRunnersManager scriptRunners;
    ClipboardHistory clipboardHistory;
    DisksControl disks;
    NetworkControl network;
    AppearanceControl appearance(&theme);
    // "Modo QT": mirrors the KDE color scheme onto the LXQt session palette, so
    // the schemes every picker of kdock already applies also reach the rest of
    // the desktop's Qt applications. Off by default and inert while off.
    QtCompat qtCompat(&theme);
    KWinScripts kwinScripts;
    // The session's keyboard layout. Under Wayland the compositor owns the
    // keymap and LXQt has no way to reach it (lxqt-config-input applies its
    // choice with setxkbmap, which is X11), so kdock is what makes the layout
    // the user picked survive a login. Off by default: applying writes kxkbrc
    // and notifies the session bus, neither of which any sandbox isolates.
    KeyboardControl keyboard;
    // …except in the session it was written for, where leaving it off means
    // every color scheme kdock applies stops at kdock's own window. Switched on
    // once, the first time kdock runs under LXQt, and never again: `configured()`
    // is false only while nobody has decided, so turning it off afterwards
    // sticks.
    //
    // This lives in main() and NOT in QtCompat's constructor on purpose. The
    // class writes ~/.config/lxqt/lxqt.conf, which no XDG_DATA_HOME sandbox
    // isolates (see qtcompat.h), so a probe linking kdock_core with a throwaway
    // data dir would count as a first run and re-theme the developer's desktop.
    if (Session::isLxqt() && !QtCompat::configured())
        qtCompat.setEnabled(true);
    // Ensure GIO/systemd-launched KDE apps inherit the LXQt platform theme
    // on every boot, not just the first time the feature is enabled.
    // Without this, a magnet-handler ktorrent at 22:42:08 had no
    // QT_QPA_PLATFORMTHEME while dolphin at 22:45 did — the former fell
    // back to Fusion/Breeze Light even after kdeglobals was fixed.
    if (Session::isLxqt())
        QtCompat::updateActivationEnvironment();
    // Re-assert the keyboard layout on every start. KWin builds its keymap from
    // kxkbrc when *it* starts, so this only ever has work to do when something
    // else moved the file (or when the user just configured it here) — but that
    // is exactly the case the feature is for, and the writes are synchronous on
    // purpose: the layout has to be right before anybody types. Costs nothing
    // while off, and while on it only writes the keys that actually differ.
    keyboard.apply();
    // The weather widget draws from the same backend the mini-app and the
    // control panel use; its own config file is watched, so a city picked in
    // kdock-weather reaches the dock without a restart.
    WeatherConfig weatherConfig;
    WeatherControl weather(&weatherConfig);
    // Window thumbnails for the apps block's hover previews. Inert until the
    // QML asks for a capture, so a session with the feature off pays nothing.
    AppPreviews appPreviews(monitor);

    // One dock per enabled monitor; the manager handles hotplug and the
    // per-monitor config files (see DockManager). The clocks are created
    // per instance because their format is a per-monitor setting.
    DockManager::Shared shared;
    shared.theme = &theme;
    shared.apps = &apps;
    shared.monitor = monitor;
    shared.volume = &volume;
    shared.audio = &audio;
    shared.brightness = &brightness;
    shared.screens = &screenBrightness;
    shared.battery = &battery;
    shared.overview = &overview;
    shared.desktopControl = &desktopControl;
    shared.monitorControl = &monitorControl;
    shared.maxmin = &maxmin;
    shared.activeWindow = &activeWindow;
    shared.wallpaperControl = &wallpaperControl;
    shared.power = &power;
    shared.systrayHost = &systray;
    shared.relanzadores = &relanzadores;
    shared.scriptRunners = &scriptRunners;
    shared.clipboardHistory = &clipboardHistory;
    shared.disks = &disks;
    shared.network = &network;
    shared.weather = &weather;
    shared.appearance = &appearance;
    shared.qtCompat = &qtCompat;
    shared.kwinScripts = &kwinScripts;
    shared.keyboard = &keyboard;
    shared.desktops = &virtualDesktops;
    shared.desktopWallpapers = &desktopWallpapers;
    shared.lxqtWallpapers = &lxqtWallpapers;
    shared.appPreviews = &appPreviews;

    // ColorAuto: the color scheme (and optionally the docks' own colors)
    // following the wallpaper. Built **before** the manager on purpose — every
    // dock gets it as a QML context property when it is created, and the first
    // docks are created inside DockManager's own constructor, so building it
    // after would leave them with a null "autoColors" and a dead widget. The
    // manager is injected right below; the class tolerates not having one.
    AutoColorScheme autoColors(&theme, &appearance, nullptr, &virtualDesktops);
    shared.autoColors = &autoColors;

    DockManager manager(shared);
    autoColors.setManager(&manager);
    // The dock's own D-Bus service — register early so `org.kdock.Dock` answers
    // quickly after a restart. Previously it was at the very end, after wallpapers
    // / darkAppearance / previews, so `dockIds` took ~2.8s to appear and the dock
    // felt frozen for ~5s. Now the first dock is already on screen after ~1.1s.
    DockService dockService(&manager);
    dockService.registerOnBus();

    // Wallpapers per virtual desktop. start() re-applies the current desktop's
    // set (or puts desktop 1 back if a previous run died with ours up), and the
    // quit hook always leaves KDE's own wallpaper behind.
    //
    // Under LXQt the other engine runs instead (see LxqtWallpapers): there is
    // no plasmashell to rewrite, so kdock paints the wallpapers itself. Exactly
    // one of the two is ever live, but BOTH quit hooks are installed — they are
    // no-ops for the engine that never started, and the LXQt one has to run on
    // the signal path too or a logout leaves the session with no desktop
    // manager at all.
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &desktopWallpapers,
                     &DesktopWallpapers::quit);
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &lxqtWallpapers,
                     &LxqtWallpapers::quit);
    // On a signal (logout, `kill`) put KDE's wallpaper back and then leave the
    // same way we used to: immediately, without unwinding. See the helper.
    installQuitSignalHandler(&app, [&desktopWallpapers, &lxqtWallpapers] {
        desktopWallpapers.quit();
        lxqtWallpapers.quit();
        ::_exit(0);
    });
    if (Session::isLxqt())
        lxqtWallpapers.start();
    else
        desktopWallpapers.start();

    // The wallpaper triggers kdock can see for itself. They are not enough on
    // their own — the usual way to change a wallpaper never goes through kdock
    // at all — which is why AutoColorScheme also watches Plasma's config.
    QObject::connect(&wallpaperControl, &WallpaperControl::wallpaperAdvanced, &autoColors,
                     [&autoColors](const QString &) { autoColors.refresh(); });
    QObject::connect(&desktopWallpapers, &DesktopWallpapers::wallpapersApplied, &autoColors,
                     [&autoColors](int) { autoColors.refresh(); });
    QObject::connect(&lxqtWallpapers, &LxqtWallpapers::wallpapersApplied, &autoColors,
                     [&autoColors](int) { autoColors.refresh(); });

    // Optional system-wide side effects of dark mode (KDE color scheme / icon
    // theme, dock icon override). Built after the docks so its first sync()
    // sees their real mode instead of "no dock is dark yet".
    DarkModeAppearance darkAppearance(&theme, &appearance);
    darkAppearance.sync();

    // Re-maximize windows that lost their state when sync() briefly left two
    // docks reserving exclusive zone on the same output during a desktop switch.
    // No config checkbox means this runs silently; the global on/off is in the
    // shared settings file (General tab) and the env escape below.
    DesktopMaximize desktopMaximize(&virtualDesktops, monitor);

    // Accessory binary: bring up the preview strips if the user left them on
    // (Settings -> Previews). It is a separate process with its own config, so
    // this is the whole of kdock's involvement at startup.
    PreviewsLauncher previews;
    previews.startIfEnabled();

    // Same deal for the full-screen tile menu: it normally comes up on the
    // widget's first click, but the user can ask for it to be resident from the
    // start so that click is instant too.
    TileMenuLauncher::startIfPreloading();
    ControlManagerLauncher::startIfPreloading();

    // And the one global shortcut kdock publishes of its own, with no default
    // key: the user assigns one in Preferencias del sistema → Atajos.
    GlobalShortcuts shortcuts;
    QObject::connect(&shortcuts, &GlobalShortcuts::triggered, &dockService,
                     [&dockService](const QString &action) {
                         if (action == QLatin1String("toggle-dark-mode"))
                             dockService.toggleDarkMode();
                     });
    shortcuts.registerAction(QStringLiteral("toggle-dark-mode"),
                             QCoreApplication::translate("main", "Alternar modo oscuro"));

    return app.exec();
}
