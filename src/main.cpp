#include <QApplication>
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

#include "clockwidget.h"
#include "clockwidget2.h"
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
#include "powercontrol.h"
#include "previewslauncher.h"
#include "controlmanagerlauncher.h"
#include "dockservice.h"
#include "globalshortcut.h"
#include "tilemenulauncher.h"
#include "diskscontrol.h"
#include "appearancecontrol.h"
#include "autocolorscheme.h"
#include "darkmodeappearance.h"
#include "networkcontrol.h"
#include "weatherconfig.h"
#include "weathercontrol.h"
#include "relanzadorconfig.h"
#include "relanzadormodel.h"
#include "relanzadoresmanager.h"
#include "scriptrunnerconfig.h"
#include "scriptrunnersmanager.h"
#include "systray.h"
#include "systraymodel.h"
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

    const QString style = DockConfig::qtStyle();
    if (!style.isEmpty())
        app.setStyle(style);

    // QML files import QtQuick.Controls (no explicit style), so this call
    // actually takes effect. Fusion is the safe desktop-style default
    // everywhere (Xvfb, bare wlroots); org.kde.desktop needs a KDE session.
    {
        const QString qqc2 = style.isEmpty() || style == QStringLiteral("Breeze")
                                 ? QStringLiteral("Fusion") : style;
        QQuickStyle::setStyle(qqc2);
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
    PowerControl power;
    SystrayHost systray;
    RelanzadoresManager relanzadores(&apps);
    ScriptRunnersManager scriptRunners;
    ClipboardHistory clipboardHistory;
    DisksControl disks;
    NetworkControl network;
    AppearanceControl appearance(&theme);
    // The weather widget draws from the same backend the mini-app and the
    // control panel use; its own config file is watched, so a city picked in
    // kdock-weather reaches the dock without a restart.
    WeatherConfig weatherConfig;
    WeatherControl weather(&weatherConfig);

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
    shared.desktops = &virtualDesktops;
    shared.desktopWallpapers = &desktopWallpapers;

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

    // Wallpapers per virtual desktop. start() re-applies the current desktop's
    // set (or puts desktop 1 back if a previous run died with ours up), and the
    // quit hook always leaves KDE's own wallpaper behind.
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &desktopWallpapers,
                     &DesktopWallpapers::quit);
    // On a signal (logout, `kill`) put KDE's wallpaper back and then leave the
    // same way we used to: immediately, without unwinding. See the helper.
    installQuitSignalHandler(&app, [&desktopWallpapers] {
        desktopWallpapers.quit();
        ::_exit(0);
    });
    desktopWallpapers.start();

    // The wallpaper triggers kdock can see for itself. They are not enough on
    // their own — the usual way to change a wallpaper never goes through kdock
    // at all — which is why AutoColorScheme also watches Plasma's config.
    QObject::connect(&wallpaperControl, &WallpaperControl::wallpaperAdvanced, &autoColors,
                     [&autoColors](const QString &) { autoColors.refresh(); });
    QObject::connect(&desktopWallpapers, &DesktopWallpapers::wallpapersApplied, &autoColors,
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

    // The dock's own D-Bus service. It exists for kdock-controlmanager: dark
    // mode has to be flipped *inside* this process to repaint (there is no file
    // watcher on the .conf), and the settings dialog opens from nowhere else.
    DockService dockService(&manager);
    dockService.registerOnBus();

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
