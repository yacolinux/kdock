// kdock-setwallpaper: set one image as the wallpaper of the *current* monitor.
//
// This is the companion of the "next wallpaper" advance (WallpaperControl): the
// same idea — change a monitor's wallpaper without touching anything else — but
// instead of picking the next file of a slideshow it takes the image as an
// argument. The point is to register a ".desktop" so the image shows up under
// "Abrir con" in Dolphin and PCManFM-Qt, and one click sets it as the wallpaper
// of the monitor the user is on.
//
// It does nothing else. If the current desktop is a slideshow, the image only
// shows until the slideshow's next step overwrites it; it can also be replaced
// by the dock's advance-wallpaper buttons. That is by design.
//
// Two ways to get there, tried in order:
//   1. Ask the running kdock (org.kdock.Dock.setWallpaper): under LXQt kdock is
//      the one drawing the wallpapers on its own surfaces, so only it can put
//      one up. This is the primary path.
//   2. If kdock is not running / not drawing wallpapers, fall back to driving
//      Plasma directly (org.kde.PlasmaShell.evaluateScript), the same API
//      WallpaperControl uses, so the tool still works on a plain Plasma desktop.

#include <QCommandLineParser>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusReply>
#include <QFileInfo>
#include <QGuiApplication>
#include <QRect>
#include <QScreen>
#include <QTextStream>
#include <QUrl>

#include "plasmascript.h"

namespace {

QTextStream &err()
{
    static QTextStream s(stderr);
    return s;
}

// The connector of the monitor the user is on: KWin's active output (the one
// with the focused window). Empty if KWin does not answer, in which case the
// caller falls back to the primary screen.
QString activeOutputName()
{
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.kde.KWin"), QStringLiteral("/KWin"),
        QStringLiteral("org.kde.KWin"), QStringLiteral("activeOutputName"));
    QDBusReply<QString> reply = QDBusConnection::sessionBus().call(msg);
    return reply.isValid() ? reply.value() : QString();
}

// Path -> QScreen geometry. Plasma's scripting matches containments by the
// geometry origin (see WallpaperControl), so that is what the fallback needs.
QRect geometryFor(const QString &screenName)
{
    for (QScreen *s : QGuiApplication::screens()) {
        if (s->name() == screenName)
            return s->geometry();
    }
    return QRect();
}

// Fallback: set the image straight on the Plasma containment at (x, y). This is
// the standard "set as wallpaper" — it switches the plugin to org.kde.image,
// which a slideshow monitor was not; that is the one bit of extra state, and it
// only runs when kdock is not the one in charge (i.e. not under LXQt).
bool setViaPlasma(const QRect &geom, const QString &fileUrl)
{
    if (!geom.isValid())
        return false;
    const QString script = QStringLiteral(
        "var t=-1;for(var i=0;i<screenCount;i++){var g=screenGeometry(i);"
        "if(g.x===%1&&g.y===%2){t=i;break;}}"
        "if(t>=0){var ds=desktops();for(var j=0;j<ds.length;j++){var d=ds[j];"
        "if(d.screen===t){d.wallpaperPlugin='org.kde.image';"
        "d.currentConfigGroup=['Wallpaper','org.kde.image','General'];"
        "d.writeConfig('Image','%3');d.reloadConfig();break;}}}")
        .arg(geom.x())
        .arg(geom.y())
        .arg(PlasmaScript::escapeJs(fileUrl));

    // Blocking: this process has no event loop running (no exec()), so a
    // fire-and-forget async call would be dropped before it reached plasmashell.
    QDBusMessage reply = PlasmaScript::evaluateBlocking(script);
    return reply.type() == QDBusMessage::ReplyMessage;
}

} // namespace

int main(int argc, char *argv[])
{
    // A manual run inherits kdock's shell integration otherwise and aborts with
    // 'No shell integration named "kdock-layershell" found'. This binary shows
    // no window; it only needs QGuiApplication for the screen list.
    if (qgetenv("QT_WAYLAND_SHELL_INTEGRATION") == "kdock-layershell")
        qunsetenv("QT_WAYLAND_SHELL_INTEGRATION");

    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("kdock-setwallpaper"));
    app.setOrganizationName(QStringLiteral("kdock"));
    app.setDesktopFileName(QStringLiteral("kdock-setwallpaper"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral(
        "Setea una imagen como wallpaper de kdock en el monitor actual. Pensado "
        "para 'Abrir con' en Dolphin / PCManFM-Qt: se le pasa la imagen y la pone "
        "de fondo del monitor donde está el usuario."));
    parser.addHelpOption();
    parser.addPositionalArgument(
        QStringLiteral("imagen"),
        QStringLiteral("Ruta (o file:// URL) de la imagen a poner de wallpaper."));
    QCommandLineOption screenOption(
        QStringLiteral("screen"),
        QStringLiteral("Conector del monitor (p. ej. DP-1). Por defecto, el monitor activo."),
        QStringLiteral("conector"));
    parser.addOption(screenOption);
    parser.process(app);

    const QStringList args = parser.positionalArguments();
    if (args.isEmpty()) {
        err() << "Falta la imagen. Uso: kdock-setwallpaper <imagen>\n";
        return 2;
    }

    // Accept both a plain path and a file:// URL (Dolphin may pass either).
    const QString arg = args.first();
    QString path = arg;
    if (arg.startsWith(QLatin1String("file://")))
        path = QUrl(arg).toLocalFile();
    const QFileInfo fi(path);
    if (!fi.exists() || !fi.isFile()) {
        err() << "No existe el archivo: " << path << "\n";
        return 2;
    }
    path = fi.absoluteFilePath();

    QString screen = parser.value(screenOption);
    if (screen.isEmpty())
        screen = activeOutputName();
    if (screen.isEmpty() && QGuiApplication::primaryScreen())
        screen = QGuiApplication::primaryScreen()->name();

    // Path 1: the running kdock (LXQt engine).
    {
        QDBusMessage msg = QDBusMessage::createMethodCall(
            QStringLiteral("org.kdock.Dock"), QStringLiteral("/Dock"),
            QStringLiteral("org.kdock.Dock"), QStringLiteral("setWallpaper"));
        msg << screen << path;
        QDBusReply<bool> reply = QDBusConnection::sessionBus().call(msg);
        if (reply.isValid() && reply.value())
            return 0; // kdock put it up
        // reply.isValid() && !value -> kdock is running but not drawing the
        // wallpapers (feature off). reply invalid -> kdock not on the bus.
        // Either way, try Plasma below.
    }

    // Path 2: drive Plasma directly.
    if (setViaPlasma(geometryFor(screen), QUrl::fromLocalFile(path).toString()))
        return 0;

    err() << "No se pudo setear el wallpaper. ¿Está kdock corriendo con la "
             "función de fondos activada, o hay un plasmashell?\n";
    return 1;
}
