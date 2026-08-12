#include <QApplication>
#include <QDir>
#include <QImage>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QtPlugin>

#include "kwinwindows.h"
#include "dockconfig.h"
#include "previewconfig.h"
#include "previewmanager.h"
#include "previewsservice.h"
#include "screenshotsource.h"
#include "translations.h"
#include "thumbnailsource.h"

// In-tree layer-shell integration, shared verbatim with kdock (src/layershell.cpp).
Q_IMPORT_PLUGIN(KDockLayerShellPlugin)

namespace {

// --dump-captures <dir>: ask KWin for one capture of every window it reports,
// write them as PNGs and exit. This is the cheapest way to prove the ScreenShot2
// path end to end (authorization included) without any UI in the way — a run
// that produces no files and logs NoAuthorized means the .desktop is missing.
int runDumpCaptures(QApplication &app, const QString &dir)
{
    if (!app.platformName().contains(QLatin1String("wayland"))) {
        qWarning("kdock-previews: --dump-captures needs the Wayland session (the window "
                 "list comes from a Wayland protocol).");
        return 1;
    }
    QDir().mkpath(dir);

    auto *windows = new KWinWindows(&app);
    auto *source = new ScreenShotSource(&app);
    auto *pending = new int(0);
    auto *done = new int(0);

    QObject::connect(source, &ThumbnailSource::thumbnailReady, &app,
                     [dir, done](const QString &uuid, const QImage &image) {
                         const QString path = dir + QLatin1Char('/') + uuid + QStringLiteral(".png");
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

    // Give KWin's initial window burst time to drain, then queue one capture per
    // window (the source serializes them).
    QTimer::singleShot(1200, &app, [windows, source, pending] {
        const auto list = windows->windows();
        qInfo("kdock-previews: %lld window(s) reported by the compositor",
              qint64(list.size()));
        for (KWinWindow *w : list) {
            const QRect g = w->geometry();
            qInfo("  %s  [%s]%s%s%s  %dx%d+%d+%d  %s", qPrintable(w->uuid()),
                  qPrintable(w->appId()), w->minimized() ? " MIN" : "",
                  w->active() ? " ACTIVE" : "", w->skipTaskbar() ? " SKIPTASKBAR" : "",
                  g.width(), g.height(), g.x(), g.y(), qPrintable(w->title()));
            // Full size: the point is to see what KWin hands over.
            source->request(w->uuid(), QSize());
            ++*pending;
        }
        if (*pending == 0)
            QCoreApplication::quit();
    });

    // Hard stop: one capture can block on a client that stopped drawing.
    QTimer::singleShot(15000, &app, &QCoreApplication::quit);
    // And a poll that quits as soon as every request has been answered.
    auto *poll = new QTimer(&app);
    poll->setInterval(200);
    QObject::connect(poll, &QTimer::timeout, &app, [pending, done] {
        if (*pending > 0 && *done >= *pending)
            QCoreApplication::quit();
    });
    poll->start();

    return app.exec();
}

} // namespace

int main(int argc, char *argv[])
{
    // Must be decided before QGuiApplication builds the platform integration.
    // Our shell integration falls back to xdg-shell for every window that is not
    // a strip (the settings dialog, menus, popups).
    // The key comes from src/kdock-layershell.json, which is shared with kdock:
    // the plugin class is the same code compiled into this binary.
    if (qEnvironmentVariableIsSet("WAYLAND_DISPLAY")
        && !qEnvironmentVariableIsSet("QT_WAYLAND_SHELL_INTEGRATION")) {
        qputenv("QT_WAYLAND_SHELL_INTEGRATION", "kdock-layershell");
    }

    // Qt Quick Controls must be set before QApplication (see kdock's main.cpp).
    {
        const QString conf = QStandardPaths::writableLocation(
            QStandardPaths::GenericDataLocation) + QStringLiteral("/kdock/kdock.conf");
        QSettings s(conf, QSettings::IniFormat);
        const QString sval = s.value(QStringLiteral("qtStyle")).toString();
        if (sval.isEmpty() || sval == QStringLiteral("Breeze"))
            qputenv("QT_QUICK_CONTROLS_STYLE", "Fusion");
        else
            qputenv("QT_QUICK_CONTROLS_STYLE", sval.toUtf8());
    }

    QApplication app(argc, argv);

    // Qt has loaded the integration by now. Drop the variable so anything this
    // process spawns does not inherit it and abort with 'No shell integration
    // named ... found' (the plugin is static, in-binary, installed nowhere).
    // Same trap as kdock's main.cpp.
    qunsetenv("QT_WAYLAND_SHELL_INTEGRATION");

    app.setApplicationName(QStringLiteral("kdock-previews"));
    app.setOrganizationName(QStringLiteral("kdock"));
    app.setApplicationDisplayName(QStringLiteral("kdock Previews"));
    // Must match the installed .desktop: it is what carries the two privileges
    // this binary cannot work without (see kdock-previews.desktop.in).
    app.setDesktopFileName(QStringLiteral("kdock-previews"));
    app.setQuitOnLastWindowClosed(false);

    const QString style = DockConfig::qtStyle();
    if (!style.isEmpty())
        app.setStyle(style);

    const QStringList args = app.arguments();

    {
        const int i = args.indexOf(QStringLiteral("--dump-captures"));
        if (i >= 0) {
            const QString dir = i + 1 < args.size() ? args.at(i + 1)
                                                    : QStringLiteral("/tmp/kdock-thumbs");
            return runDumpCaptures(app, dir);
        }
    }

    const bool settingsOnly = args.contains(QStringLiteral("--settings"));

    // Owning the bus name is the single-instance lock: forward and get out of
    // the way.
    if (PreviewsService::alreadyRunning()) {
        if (settingsOnly)
            PreviewsService::callShowSettings();
        else
            qInfo("kdock-previews: already running; nothing to do.");
        return 0;
    }

    // Same translation layer as kdock, and the same setting: this binary does
    // not choose a language, it follows the dock's. BaseOnly because an ALT
    // layer only renames the dock's widgets and apps — here it means nothing,
    // so "english-ALT-hacker" loads plain "english".
    Translations translations(Translations::BaseOnly);

    PreviewManager manager;
    QObject::connect(&translations, &Translations::changed, &manager,
                     [&manager] { manager.retranslate(); });
    PreviewsService service(&manager);
    service.registerOnBus();

    if (settingsOnly)
        manager.showSettings();

    return app.exec();
}
