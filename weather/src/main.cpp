#include <QApplication>
#include <QQuickStyle>
#include <QTextStream>
#include <QTimer>

#include "dockconfig.h"
#include "theme.h"
#include "translations.h"
#include "weatherconfig.h"
#include "weathercontrol.h"
#include "weatherservice.h"
#include "weathersettingsdialog.h"
#include "weatherwindow.h"

namespace {

QString argValue(const QStringList &args, const QString &flag)
{
    const int i = args.indexOf(flag);
    if (i < 0 || i + 1 >= args.size())
        return {};
    const QString next = args.at(i + 1);
    return next.startsWith(QLatin1String("--")) ? QString() : next;
}

// --dump: print the resolved forecast and exit, no window and no compositor.
// Same role --dump-sections plays for the control panel: it only reads, so it is
// safe to run while an instance is up, and with KDOCK_WEATHER_FIXTURE it needs
// no network either.
int runDump(QApplication &app)
{
    auto *config = new WeatherConfig(&app);
    auto *weather = new WeatherControl(config, &app);
    QTextStream out(stdout);

    // The first fetch is asynchronous (and may be answered from the cache), so
    // print once it settles rather than on an empty model.
    int elapsed = 0;
    auto *timer = new QTimer(&app);
    QObject::connect(timer, &QTimer::timeout, &app, [&, timer, weather] {
        elapsed += 200;
        const bool ready = weather->available() && !weather->loading();
        if (ready || elapsed >= 12000) {
            timer->stop();
            out << weather->dump() << Qt::flush;
            app.quit();
        }
    });
    timer->start(200);
    return app.exec();
}

} // namespace

int main(int argc, char *argv[])
{
    // kdock unsets this before spawning anything, but a run launched by hand
    // from a shell that still has it set would abort with 'No shell integration
    // named "kdock-layershell" found'. This binary is an ordinary toplevel.
    if (qgetenv("QT_WAYLAND_SHELL_INTEGRATION") == "kdock-layershell")
        qunsetenv("QT_WAYLAND_SHELL_INTEGRATION");

    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("kdock-weather"));
    app.setOrganizationName(QStringLiteral("kdock"));
    app.setApplicationDisplayName(QStringLiteral("kdock Weather"));
    app.setDesktopFileName(QStringLiteral("kdock-weather"));
    // The settings dialog can be the only thing on screen for a while (a first
    // run with no city configured opens exactly that).
    app.setQuitOnLastWindowClosed(false);

    const QString style = DockConfig::qtStyle();
    if (!style.isEmpty()) {
        app.setStyle(style);
        const QString qqc2 = (style == QStringLiteral("Breeze"))
                                 ? QStringLiteral("Fusion") : style;
        QQuickStyle::setStyle(qqc2);
    }

    const QStringList args = app.arguments();

    if (args.contains(QStringLiteral("--dump")))
        return runDump(app);

    const QString screenName = argValue(args, QStringLiteral("--screen"));
    const bool wantSettings = args.contains(QStringLiteral("--settings"));
    const bool wantToggle = args.contains(QStringLiteral("--toggle"));
    const bool wantShow = args.contains(QStringLiteral("--show"))
                          || (!wantSettings && !wantToggle);

    // Owning the bus name is the single-instance lock: forward and get out of
    // the way.
    if (WeatherService::alreadyRunning()) {
        if (wantToggle)
            WeatherService::callToggle(screenName);
        else if (wantShow)
            WeatherService::callShow(screenName);
        if (wantSettings)
            WeatherService::callShowSettings();
        return 0;
    }

    // Same translation layer as kdock, and the same setting: this binary does
    // not choose a language, it follows the dock's. BaseOnly because an ALT
    // layer only renames the dock's widgets and apps.
    Translations translations(Translations::BaseOnly);

    Theme theme;
    WeatherConfig config;
    WeatherControl weather(&config);

    WeatherWindow window(&config, &weather, &theme);
    QObject::connect(&translations, &Translations::changed, &window,
                     [&window] { window.retranslate(); });
    WeatherService service(&window);
    service.registerOnBus();

    if (wantShow || wantToggle)
        window.showOn(screenName);
    if (wantSettings)
        window.openSettings();

    // Nothing configured yet: the only useful thing to show is the city picker.
    if (config.cities().isEmpty() && !wantSettings)
        window.openSettings();

    return app.exec();
}
