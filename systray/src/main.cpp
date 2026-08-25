#include <QApplication>
#include <QQuickStyle>
#include <QTextStream>
#include <QTimer>
#include <QtPlugin>

#include "dockconfig.h"
#include "systray.h"
#include "systrayconfig.h"
#include "systraymodel.h"
#include "systrayservice.h"
#include "systraywindow.h"
#include "theme.h"
#include "translations.h"

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

// --dump: bring up the SNI host, wait for the watcher round trips to settle,
// print what it collected and exit. No window. Safe while an instance runs only
// if that instance is not the one holding the watcher — otherwise this second
// host competes for it, so it is a diagnostic for a cold session, not a probe of
// a live one. It IS the way to see, without a GUI, whether we became the watcher
// and which items we see.
int runDump(QApplication &app)
{
    auto *config = new SystrayConfig(&app);
    auto *host = new SystrayHost(&app);
    auto *model = new SystrayModel(host, config, &app);
    QTextStream out(stdout);

    auto *timer = new QTimer(&app);
    int elapsed = 0;
    QObject::connect(timer, &QTimer::timeout, &app, [&, timer, host, model] {
        elapsed += 250;
        if (elapsed < 1500) // let RegisterStatusNotifierHost + the list settle
            return;
        timer->stop();
        out << "watcher: we are "
            << (host->isWatcher() ? "the watcher" : "a host of another watcher")
            << "  active=" << (host->active() ? "yes" : "no") << Qt::endl;
        out << "items: " << model->count() << Qt::endl;
        const auto items = host->items();
        for (const SystrayItem *it : items) {
            out << QStringLiteral("  %1  icon=%2  menu=%3  \"%4\"")
                       .arg(it->service.leftJustified(22),
                            it->iconName.isEmpty() ? QStringLiteral("(pixmap)") : it->iconName)
                       .arg(it->hasMenu ? QStringLiteral("yes") : QStringLiteral("no"),
                            it->tooltipTitle)
                << Qt::endl;
        }
        app.quit();
    });
    timer->start(250);
    return app.exec();
}

} // namespace

int main(int argc, char *argv[])
{
    // Must be decided before QGuiApplication builds the platform integration:
    // our shell integration falls back to xdg-shell for windows that do not ask
    // for a layer. The key comes from src/kdock-layershell.json, compiled in.
    if (qEnvironmentVariableIsSet("WAYLAND_DISPLAY")
        && !qEnvironmentVariableIsSet("QT_WAYLAND_SHELL_INTEGRATION")) {
        qputenv("QT_WAYLAND_SHELL_INTEGRATION", "kdock-layershell");
    }

    QApplication app(argc, argv);

    // Qt has loaded the integration by now. Drop the variable so anything a tray
    // item spawns via us does not inherit it and abort.
    qunsetenv("QT_WAYLAND_SHELL_INTEGRATION");

    app.setApplicationName(QStringLiteral("kdock-systray"));
    app.setOrganizationName(QStringLiteral("kdock"));
    app.setApplicationDisplayName(QStringLiteral("kdock Systray"));
    app.setDesktopFileName(QStringLiteral("kdock-systray"));
    // The window hides instead of closing, and the host must outlive it.
    app.setQuitOnLastWindowClosed(false);

    const QString style = DockConfig::qtStyle();
    if (!style.isEmpty())
        app.setStyle(style);

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

    const QStringList args = app.arguments();

    if (args.contains(QStringLiteral("--dump")))
        return runDump(app);

    const QString screenName = argValue(args, QStringLiteral("--screen"));
    const bool wantSettings = args.contains(QStringLiteral("--settings"));
    const bool wantHide = args.contains(QStringLiteral("--hide"));
    const bool wantToggle = args.contains(QStringLiteral("--toggle"));
    // Bare run (or --show): put the window on screen. --hide comes up resident
    // with nothing shown, which is how the session autostart brings it up.
    const bool wantShow = args.contains(QStringLiteral("--show"))
                          || (!wantSettings && !wantHide && !wantToggle);

    // Owning the bus name is the single-instance lock: forward and get out of
    // the way.
    if (SystrayService::alreadyRunning()) {
        if (wantToggle)
            SystrayService::callToggle(screenName);
        else if (wantShow)
            SystrayService::callShow(screenName);
        if (wantSettings)
            SystrayService::callShowSettings();
        // --hide against a running instance is a no-op (it is already resident).
        return 0;
    }

    // Same translation layer as kdock, following the dock's language.
    Translations translations(Translations::BaseOnly);

    Theme theme;
    SystrayConfig config;
    // The host is built unconditionally, even for --hide: being the SNI
    // host/watcher for the whole session is the reason this process is resident.
    SystrayHost host;
    SystrayModel model(&host, &config);

    SystrayWindow window(&config, &theme, &model, &host);
    QObject::connect(&translations, &Translations::changed, &window,
                     [&window] { window.reloadConfig(); });
    SystrayService service(&window);
    service.registerOnBus();

    if (wantShow || wantToggle)
        window.showOn(screenName);
    if (wantSettings)
        window.openSettings();

    return app.exec();
}
