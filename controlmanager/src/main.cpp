#include <QApplication>
#include <QTextStream>
#include <QtPlugin>

#include "audiocontrol.h"
#include "batterycontrol.h"
#include "brightnesscontrol.h"
#include "cmconfig.h"
#include "cmlayout.h"
#include "cmmodel.h"
#include "cmsections.h"
#include "cmservice.h"
#include "cmwindow.h"
#include "desktopentry.h"
#include "docklink.h"
#include "mpriscontrol.h"
#include "networkcontrol.h"
#include "powercontrol.h"
#include "screenbrightness.h"
#include "theme.h"
#include "translations.h"
#include "wallpapercontrol.h"

// In-tree layer-shell integration, shared verbatim with kdock (src/layershell.cpp).
Q_IMPORT_PLUGIN(KDockLayerShellPlugin)

namespace {

QString argValue(const QStringList &args, const QString &flag)
{
    const int i = args.indexOf(flag);
    if (i < 0 || i + 1 >= args.size())
        return {};
    const QString next = args.at(i + 1);
    return next.startsWith(QLatin1String("--")) ? QString() : next;
}

// --dump-sections: print what every backend reports plus the resolved grid, then
// exit. No window, no compositor, and it is safe while another instance runs
// because it only reads — the same role --dump-layout plays for kdock-tilemenu.
//
// It is also the only way to see the D-Bus backends without touching the
// machine: the panel's sliders write to the real brightness and the real mixer.
int runDumpSections(QApplication &app)
{
    Q_UNUSED(app);
    QTextStream out(stdout);

    CmConfig config;
    CmLayout layout(&config);

    out << layout.dump() << Qt::endl;

    out << "== secciones ==" << Qt::endl;
    for (const CmSectionInfo &s : CmSections::all()) {
        out << QStringLiteral("  %1  tab=%2  card=%3  %4x%5  \"%6\"")
                   .arg(s.id.leftJustified(10))
                   .arg(config.sectionEnabled(s.id) ? QStringLiteral("si") : QStringLiteral("no"),
                        config.cardEnabled(s.id) ? QStringLiteral("si") : QStringLiteral("no"))
                   .arg(s.defaultW)
                   .arg(s.defaultH)
                   .arg(CmSections::label(s.id))
            << Qt::endl;
    }

    ScreenBrightness screens;
    out << Qt::endl << "== brillo por monitor ==" << Qt::endl;
    if (!screens.available()) {
        out << "  org.kde.ScreenBrightness no responde (¿PowerDevil apagado?)" << Qt::endl;
    } else {
        const QVariantList list = screens.displays();
        for (const QVariant &v : list) {
            const QVariantMap m = v.toMap();
            out << QStringLiteral("  %1  %2  %3/%4  (%5%)%6")
                       .arg(m.value(QStringLiteral("name")).toString().leftJustified(12),
                            m.value(QStringLiteral("label")).toString().leftJustified(36))
                       .arg(m.value(QStringLiteral("brightness")).toInt())
                       .arg(m.value(QStringLiteral("max")).toInt())
                       .arg(qRound(m.value(QStringLiteral("value")).toDouble() * 100))
                       .arg(m.value(QStringLiteral("internal")).toBool()
                                ? QStringLiteral("  interno") : QString())
                << Qt::endl;
        }
    }

    MprisControl mpris;
    out << Qt::endl << "== reproductores ==" << Qt::endl;
    const QVariantList players = mpris.players();
    if (players.isEmpty()) {
        out << "  ninguno" << Qt::endl;
    } else {
        for (const QVariant &v : players) {
            const QVariantMap m = v.toMap();
            out << QStringLiteral("  %1  [%2]  %3")
                       .arg(m.value(QStringLiteral("identity")).toString().leftJustified(24),
                            m.value(QStringLiteral("status")).toString(),
                            m.value(QStringLiteral("id")).toString())
                << Qt::endl;
        }
        out << QStringLiteral("  actual: %1 — %2 / %3")
                   .arg(mpris.identity(), mpris.title(), mpris.artist())
            << Qt::endl;
    }

    DockLink dock;
    out << Qt::endl << "== kdock ==" << Qt::endl;
    if (!dock.available()) {
        out << "  org.kdock.Dock no responde (¿kdock viejo o apagado?)" << Qt::endl;
    } else {
        out << QStringLiteral("  modo oscuro: %1")
                   .arg(dock.darkMode() ? QStringLiteral("si") : QStringLiteral("no"))
            << Qt::endl;
        const QVariantList docks = dock.docks();
        for (const QVariant &v : docks) {
            const QVariantMap m = v.toMap();
            out << QStringLiteral("  dock %1 en %2%3")
                       .arg(m.value(QStringLiteral("id")).toString(),
                            m.value(QStringLiteral("screen")).toString(),
                            m.value(QStringLiteral("primary")).toBool()
                                ? QStringLiteral("  (primario)") : QString())
                << Qt::endl;
        }
    }

    return 0;
}

} // namespace

int main(int argc, char *argv[])
{
    // Must be decided before QGuiApplication builds the platform integration.
    // Our shell integration falls back to xdg-shell for every window that is not
    // the panel (the settings dialog, menus, popups). The key comes from
    // src/kdock-layershell.json, shared with kdock: same code, compiled in here.
    if (qEnvironmentVariableIsSet("WAYLAND_DISPLAY")
        && !qEnvironmentVariableIsSet("QT_WAYLAND_SHELL_INTEGRATION")) {
        qputenv("QT_WAYLAND_SHELL_INTEGRATION", "kdock-layershell");
    }

    QApplication app(argc, argv);

    // Qt has loaded the integration by now. Drop the variable so anything this
    // process spawns (the System section launches applications) does not inherit
    // it and abort with 'No shell integration named ... found'.
    qunsetenv("QT_WAYLAND_SHELL_INTEGRATION");

    app.setApplicationName(QStringLiteral("kdock-controlmanager"));
    app.setOrganizationName(QStringLiteral("kdock"));
    app.setApplicationDisplayName(QStringLiteral("kdock Control Manager"));
    app.setDesktopFileName(QStringLiteral("kdock-controlmanager"));
    // The panel hides instead of closing, and the settings dialog can be the
    // only thing on screen for a while.
    app.setQuitOnLastWindowClosed(false);

    const QStringList args = app.arguments();

    if (args.contains(QStringLiteral("--dump-sections")))
        return runDumpSections(app);

    const QString screenName = argValue(args, QStringLiteral("--screen"));
    const QString sectionArg = argValue(args, QStringLiteral("--section"));
    const bool wantSettings = args.contains(QStringLiteral("--settings"));
    const bool wantHide = args.contains(QStringLiteral("--hide"));
    const bool wantToggle = args.contains(QStringLiteral("--toggle"));
    // Bare run (or --show): put the panel on screen. That is what the widget's
    // very first click does, and what makes a manual test one command long.
    const bool wantShow = args.contains(QStringLiteral("--show"))
                          || (!wantSettings && !wantHide && !wantToggle);

    // Owning the bus name is the single-instance lock: forward and get out of
    // the way.
    if (CmService::alreadyRunning()) {
        if (wantToggle)
            CmService::callToggle(screenName);
        else if (wantHide)
            CmService::callHide();
        else if (wantShow && !sectionArg.isEmpty())
            CmService::callShowSection(sectionArg, screenName);
        else if (wantShow)
            CmService::callShow(screenName);
        if (wantSettings)
            CmService::callShowSettings();
        return 0;
    }

    // Same translation layer as kdock, and the same setting: this binary does
    // not choose a language, it follows the dock's. BaseOnly because an ALT
    // layer only renames the dock's widgets and apps — here it means nothing.
    Translations translations(Translations::BaseOnly);

    Theme theme;
    DesktopEntryIndex apps;

    CmConfig config;
    CmLayout layout(&config);
    CmModel model(&layout, &config);

    // The backends. They are cheap to build and every one of them is lazy about
    // talking to the system, so there is no reason to defer any.
    AudioControl audio;
    BatteryControl battery;
    BrightnessControl brightness;
    ScreenBrightness screenBrightness;
    NetworkControl network;
    WallpaperControl wallpaper;
    PowerControl power;
    MprisControl mpris;
    DockLink dockLink;

    CmBackends backends;
    backends.apps = &apps;
    backends.audio = &audio;
    backends.battery = &battery;
    backends.brightness = &brightness;
    backends.screenBrightness = &screenBrightness;
    backends.network = &network;
    backends.wallpaper = &wallpaper;
    backends.power = &power;
    backends.mpris = &mpris;
    backends.dock = &dockLink;

    CmWindow window(&config, &theme, &layout, &model, backends);
    QObject::connect(&translations, &Translations::changed, &window,
                     [&window] { window.retranslate(); });
    CmService service(&window);
    service.registerOnBus();

    if (!sectionArg.isEmpty() && CmSections::exists(sectionArg))
        window.setCurrentTab(sectionArg);

    // A first launch is always a request to show something: --toggle from a cold
    // start means "open", not "close what isn't there".
    if (wantShow || wantToggle)
        window.showOn(screenName);
    if (wantSettings)
        window.openSettings();

    return app.exec();
}
