#include <QApplication>
#include <QCommandLineParser>
#include <QDate>
#include <QScreen>
#include <QSettings>
#include <QStandardPaths>

#include "calendarwidget.h"

int main(int argc, char *argv[])
{
    // A manual run from a shell that still has kdock's shell integration set
    // would abort with 'No shell integration named "kdock-layershell" found'.
    // This binary is an ordinary toplevel and needs no shell integration.
    if (qgetenv("QT_WAYLAND_SHELL_INTEGRATION") == "kdock-layershell")
        qunsetenv("QT_WAYLAND_SHELL_INTEGRATION");

    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("kdock-calendar"));
    app.setOrganizationName(QStringLiteral("kdock"));
    app.setApplicationDisplayName(QStringLiteral("Calendario"));
    app.setDesktopFileName(QStringLiteral("kdock-calendar"));

    {
        const QString style = QSettings(
            QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                + QStringLiteral("/kdock.conf"),
            QSettings::IniFormat).value(QStringLiteral("qtStyle")).toString();
        if (!style.isEmpty())
            app.setStyle(style);
    }

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral(
        "Calendario de mes estilo KDE, standalone: pensado para lanzarse desde el "
        "widget del reloj (o un Script Runner) sin tocar el propio widget."));
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption monthOption(
        QStringLiteral("month"),
        QStringLiteral("Mes inicial en formato AAAA-MM (por defecto, el actual)."),
        QStringLiteral("AAAA-MM"));
    parser.addOption(monthOption);
    parser.process(app);

    QDate initial = QDate::currentDate();
    const QString monthArg = parser.value(monthOption);
    if (!monthArg.isEmpty()) {
        const QDate parsed = QDate::fromString(monthArg, QStringLiteral("yyyy-MM"));
        if (parsed.isValid())
            initial = parsed;
    }

    CalendarWidget window;
    window.setMonth(initial);
    // Wide and tall: the whole point is the big numbers in a generous grid.
    window.resize(430, 520);
    const QRect avail = window.screen()->availableGeometry();
    window.move(avail.center() - QPoint(window.width() / 2, window.height() / 2));
    window.show();

    return app.exec();
}
