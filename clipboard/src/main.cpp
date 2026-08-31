#include <QApplication>
#include <QCommandLineParser>
#include <QQuickStyle>
#include <QQmlEngine>

#include "clipboardhistory.h"
#include "clipboardservice.h"
#include "clipboardwindow.h"
#include "translations.h"

namespace {
QString argValue(const QStringList &args, const QString &flag)
{
    const int i = args.indexOf(flag);
    if (i < 0 || i + 1 >= args.size())
        return {};
    const QString value = args.at(i + 1);
    return value.startsWith(QLatin1String("--")) ? QString() : value;
}

} // namespace

int main(int argc, char *argv[])
{
    if (qgetenv("QT_WAYLAND_SHELL_INTEGRATION") == "kdock-layershell")
        qunsetenv("QT_WAYLAND_SHELL_INTEGRATION");

    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("kdock-clipboard"));
    app.setOrganizationName(QStringLiteral("kdock"));
    app.setApplicationDisplayName(QStringLiteral("kdock Clipboard"));
    app.setDesktopFileName(QStringLiteral("kdock-clipboard"));
    app.setQuitOnLastWindowClosed(false);
    QQuickStyle::setStyle(QStringLiteral("Fusion"));

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption(QCommandLineOption(QStringLiteral("toggle"),
                                        QStringLiteral("Mostrar u ocultar la ventana")));
    parser.addOption(QCommandLineOption(QStringLiteral("hide"),
                                        QStringLiteral("Iniciar sin mostrar la ventana")));
    parser.addOption(QCommandLineOption(QStringLiteral("screen"),
                                        QStringLiteral("Pantalla donde ubicar la ventana"),
                                        QStringLiteral("name")));
    parser.process(app);
    const QStringList args = app.arguments();
    const bool toggle = args.contains(QStringLiteral("--toggle"));
    const QString screenName = argValue(args, QStringLiteral("--screen"));
    if (ClipboardService::alreadyRunning()) {
        if (toggle)
            ClipboardService::callToggle(screenName);
        return 0;
    }

    Translations translations(Translations::BaseOnly);
    ClipboardHistory history;
    ClipboardWindow window(&history);
    QObject::connect(&translations, &Translations::changed, &window,
                     [&window] { window.engine()->retranslate(); });
    ClipboardService service(&window);
    if (!service.registerOnBus())
        return 1;

    if (toggle || (!args.contains(QStringLiteral("--hide"))))
        window.showWindow(screenName);

    return app.exec();
}
