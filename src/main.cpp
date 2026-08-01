#include <QApplication>
#include <QQmlEngine>
#include <QScreen>
#include <QTimer>
#include <QWidget>
#include <QWindow>
#include <QtPlugin>

#include "clockwidget.h"
#include "clockwidget2.h"
#include "clipboardhistory.h"
#include "desktopentry.h"
#include "dockconfig.h"
#include "dockmanager.h"
#include "dockmodel.h"
#include "dockwindow.h"
#include "brightnesscontrol.h"
#include "batterycontrol.h"
#include "overviewcontrol.h"
#include "desktopcontrol.h"
#include "maxmincontrol.h"
#include "monitorcontrol.h"
#include "wallpapercontrol.h"
#include "powercontrol.h"
#include "previewslauncher.h"
#include "diskscontrol.h"
#include "appearancecontrol.h"
#include "networkcontrol.h"
#include "relanzadorconfig.h"
#include "relanzadormodel.h"
#include "relanzadoresmanager.h"
#include "scriptrunnerconfig.h"
#include "scriptrunnersmanager.h"
#include "systray.h"
#include "systraymodel.h"
#include "theme.h"
#include "volumecontrol.h"
#include "audiocontrol.h"
#include "windowmonitor.h"

// In-tree layer-shell integration (see layershell.cpp)
Q_IMPORT_PLUGIN(KDockLayerShellPlugin)

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
    }

    // Register types so QML can handle them
    qmlRegisterUncreatableType<RelanzadorModel>("kdock", 1, 0, "RelanzadorModel", QStringLiteral("Access via RelanzadoresManager"));
    qmlRegisterUncreatableType<RelanzadorConfig>("kdock", 1, 0, "RelanzadorConfig", QStringLiteral("Access via RelanzadoresManager"));
    qmlRegisterUncreatableType<ScriptRunnerConfig>("kdock", 1, 0, "ScriptRunnerConfig", QStringLiteral("Access via ScriptRunnersManager"));

    const bool wayland = app.platformName().contains(QLatin1String("wayland"));

    // System-wide singletons shared by every dock instance.
    Theme theme;
    DesktopEntryIndex apps;
    WindowMonitor *monitor = wayland ? WindowMonitor::create(&app) : nullptr;
    VolumeControl volume;
    AudioControl audio;
    BrightnessControl brightness;
    BatteryControl battery;
    OverviewControl overview;
    DesktopControl desktopControl;
    MonitorControl monitorControl;
    MaxMinControl maxmin;
    WallpaperControl wallpaperControl;
    PowerControl power;
    SystrayHost systray;
    RelanzadoresManager relanzadores(&apps);
    ScriptRunnersManager scriptRunners;
    ClipboardHistory clipboardHistory;
    DisksControl disks;
    NetworkControl network;
    AppearanceControl appearance(&theme);

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
    shared.battery = &battery;
    shared.overview = &overview;
    shared.desktopControl = &desktopControl;
    shared.monitorControl = &monitorControl;
    shared.maxmin = &maxmin;
    shared.wallpaperControl = &wallpaperControl;
    shared.power = &power;
    shared.systrayHost = &systray;
    shared.relanzadores = &relanzadores;
    shared.scriptRunners = &scriptRunners;
    shared.clipboardHistory = &clipboardHistory;
    shared.disks = &disks;
    shared.network = &network;
    shared.appearance = &appearance;

    DockManager manager(shared);

    // Accessory binary: bring up the preview strips if the user left them on
    // (Settings -> Previews). It is a separate process with its own config, so
    // this is the whole of kdock's involvement at startup.
    PreviewsLauncher previews;
    previews.startIfEnabled();

    return app.exec();
}
