#include <QApplication>
#include <QQuickStyle>
#include <QTextStream>

#include "appmenu.h"
#include "desktopentry.h"
#include "dockconfig.h"
#include "powercontrol.h"
#include "theme.h"
#include "tileconfig.h"
#include "tilelayout.h"
#include "tilemenuservice.h"
#include "tilemodel.h"
#include "tilewindow.h"
#include "translations.h"

namespace {

QString argValue(const QStringList &args, const QString &flag)
{
    const int i = args.indexOf(flag);
    if (i < 0 || i + 1 >= args.size())
        return {};
    const QString next = args.at(i + 1);
    return next.startsWith(QLatin1String("--")) ? QString() : next;
}

// --dump-layout [section]: print the resolved arrangement as ASCII and exit.
// The cheapest way to see what the placement engine decided — no window, no
// compositor, and it works while another instance is running because it only
// reads. Same role --dump-captures plays for kdock-previews.
int runDumpLayout(const QStringList &args)
{
    DesktopEntryIndex apps;
    DockConfig dockConfig;
    AppMenu menu(&apps, &dockConfig);
    TileConfig config;
    TileLayout layout(&config, &menu);

    QTextStream out(stdout);
    out << layout.dump(argValue(args, QStringLiteral("--dump-layout")));
    return 0;
}

} // namespace

int main(int argc, char *argv[])
{
    // kdock unsets this before spawning anything (its layer-shell integration is
    // a static plugin that lives only inside the kdock binary), but a run
    // launched by hand from a shell that still has it set would abort with
    // 'No shell integration named "kdock-layershell" found'. This binary needs
    // no shell integration of its own: the menu is an ordinary toplevel.
    if (qgetenv("QT_WAYLAND_SHELL_INTEGRATION") == "kdock-layershell")
        qunsetenv("QT_WAYLAND_SHELL_INTEGRATION");

    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("kdock-tilemenu"));
    app.setOrganizationName(QStringLiteral("kdock"));
    app.setApplicationDisplayName(QStringLiteral("kdock Tile Menu"));
    app.setDesktopFileName(QStringLiteral("kdock-tilemenu"));
    // The menu hides instead of closing, and the settings panel can be the only
    // thing on screen for a while.
    app.setQuitOnLastWindowClosed(false);

    const QString style = DockConfig::qtStyle();
    if (!style.isEmpty())
        app.setStyle(style);

    const QString qqc2 = style.isEmpty() || style == QStringLiteral("Breeze")
                             ? QStringLiteral("Fusion") : style;
    QQuickStyle::setStyle(qqc2);

    const QStringList args = app.arguments();

    if (args.contains(QStringLiteral("--dump-layout")))
        return runDumpLayout(args);

    const QString screenName = argValue(args, QStringLiteral("--screen"));
    const bool wantSettings = args.contains(QStringLiteral("--settings"));
    const bool wantHide = args.contains(QStringLiteral("--hide"));
    const bool wantToggle = args.contains(QStringLiteral("--toggle"));
    // Bare run (or --show): put the menu on screen. That is what the widget's
    // very first click does, and what makes a manual test one command long.
    const bool wantShow = args.contains(QStringLiteral("--show"))
                          || (!wantSettings && !wantHide && !wantToggle);

    // Owning the bus name is the single-instance lock: forward and get out of
    // the way.
    if (TileMenuService::alreadyRunning()) {
        if (wantToggle)
            TileMenuService::callToggle(screenName);
        else if (wantHide)
            TileMenuService::callHide();
        else if (wantShow)
            TileMenuService::callShow(screenName);
        if (wantSettings)
            TileMenuService::callShowSettings();
        return 0;
    }

    // Same translation layer as kdock, and the same setting: this binary does
    // not choose a language, it follows the dock's. BaseOnly because an ALT
    // layer only renames the dock's widgets and apps — here it means nothing,
    // so "english-ALT-hacker" loads plain "english".
    Translations translations(Translations::BaseOnly);

    Theme theme;
    DesktopEntryIndex apps;
    // The shared kdock.conf: this is where the menu favorites and the menu
    // editor live, so the tile menu and the dock's own menu agree on both.
    DockConfig dockConfig;
    AppMenu menu(&apps, &dockConfig);
    PowerControl power;

    TileConfig config;
    TileLayout layout(&config, &menu);
    TileModel model(&layout, &menu, &config);

    TileWindow window(&config, &theme, &apps, &menu, &layout, &model, &power);
    QObject::connect(&translations, &Translations::changed, &window,
                     [&window] { window.retranslate(); });
    TileMenuService service(&window);
    service.registerOnBus();

    // A first launch is always a request to show something: --toggle from a cold
    // start means "open", not "close what isn't there".
    if (wantShow || wantToggle)
        window.showOn(screenName);
    if (wantSettings)
        window.openSettings();

    return app.exec();
}
