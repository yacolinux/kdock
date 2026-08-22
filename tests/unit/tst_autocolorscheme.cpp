// AutoColorScheme — la máquina de estados de ColorAuto, sin plasmashell.
//
// Lo que se prueba acá es la parte persistida: los ajustes, los defaults que se
// guardan al activar y se devuelven al desactivar, y el enclavamiento con el
// modo oscuro. Nada de esto necesita D-Bus, y **el enclavamiento es lo que más
// importa**: mientras ColorAuto tiene el sistema, lo que está puesto es un
// esquema que kdock generó y que se borra en el próximo cambio de fondo, así que
// si el modo oscuro lo fotografía como "lo que tenía el usuario", salir del modo
// oscuro restaura un esquema que ya no existe y no hay forma de volver.
//
// A propósito NO se llama a setEnabled(true) sobre un objeto vivo: eso dispara
// la lectura del fondo por D-Bus, y este test tiene que ser hermético. El estado
// "estaba activo" se siembra por la API estática, que es la misma que persiste.

#include "appearancecontrol.h"
#include "autocolorscheme.h"
#include "darkmodeappearance.h"
#include "dockmanager.h"
#include "dockconfig.h"
#include "sandbox.h"
#include "theme.h"
#include "wallpapercolors.h"

#include <QColor>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QImage>
#include <QSettings>
#include <QStandardPaths>
#include <QTest>

namespace {

QSettings shared()
{
    return QSettings(DockConfig::settingsFilePath(), QSettings::IniFormat);
}

void writeShared(const QString &key, const QVariant &value)
{
    QSettings s = shared();
    s.setValue(key, value);
    s.sync();
}

// Cuál de los dos esquemas quedó aplicado. Es estado persistido, así que se lee
// del archivo y no de la API — el getter es privado, y de todos modos lo que
// importa acá es justamente lo que sobrevive al proceso.
int appliedSlot()
{
    return shared().value(QStringLiteral("ColorAuto/lastSlot"), 1).toInt();
}

// Un PNG de color sólido en el sandbox. Generado y no fixture, por la misma
// razón que en tst_wallpapercolors: lo que se afirma es el color que sale de la
// imagen, así que un blob binario escondería justo el dato bajo prueba.
// Saturado a propósito — el portón de saturación/valor del muestreo descarta
// gris, negro y blanco.
QString solidImage(const QString &name, const QColor &color)
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation);
    QDir().mkpath(dir);
    const QString path = dir + QLatin1Char('/') + name;
    QImage img(64, 64, QImage::Format_RGB32);
    img.fill(color);
    img.save(path, "PNG");
    return path;
}

// El conector del monitor que manda, tal como lo ve este test.
//
// systemScreenName() cae a QGuiApplication::screens() y bajo `offscreen` la
// única pantalla **no tiene nombre**, así que el monitor que manda se llama "".
// Sembrar la fuente con esa clave es lo que hace que el camino "cambió el fondo
// del monitor que manda" se ejercite de verdad; con cualquier otro nombre
// leadImage queda vacío y la mitad de la contabilidad no corre.
const QString kLeadScreen = QString();

QString schemePath(const QString &id)
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
           + QStringLiteral("/color-schemes/") + id + QStringLiteral(".colors");
}

void removeGeneratedSchemes()
{
    QFile::remove(schemePath(AutoColorScheme::kSchemeIdA));
    QFile::remove(schemePath(AutoColorScheme::kSchemeIdB));
}

} // namespace

class TestAutoColorScheme : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        // Cada prueba arranca sin rastro de la anterior: los ajustes son
        // estáticos y viven en el kdock.conf compartido, así que sin esto la
        // segunda corrida empieza con lo que dejó la primera.
        QSettings s = shared();
        s.remove(QStringLiteral("ColorAuto"));
        s.sync();
    }

    // ---- Ajustes ----------------------------------------------------------

    void defaultsAreOff()
    {
        // El interruptor maestro arranca apagado: es lo que hace que un arnés
        // con un XDG_DATA_HOME descartable no le pueda tocar el escritorio a
        // nadie, igual que DesktopWallpapers::enabled().
        QVERIFY(!AutoColorScheme::enabled());
        QVERIFY(!AutoColorScheme::defaultsSaved());
        QVERIFY(!AutoColorScheme::applied());
        // Y las dos mitades vienen prendidas, que es lo útil una vez activado.
        QVERIFY(AutoColorScheme::colorDocks());
        QVERIFY(AutoColorScheme::systemScheme());
    }

    void settingsRoundTrip()
    {
        AutoColorScheme::setColorDocks(false);
        AutoColorScheme::setSystemScheme(false);
        AutoColorScheme::setSystemMonitor(QStringLiteral("DP-3"));
        AutoColorScheme::setLightness(WallpaperColors::Options::ForceDark);
        AutoColorScheme::setSelectionMode(WallpaperColors::Options::FromWallpaper);
        AutoColorScheme::setSelectionLight(QColor(10, 20, 30));
        AutoColorScheme::setSelectionDark(QColor(200, 210, 220));
        AutoColorScheme::setIconsetEnabled(true, true);
        AutoColorScheme::setIconsetValue(true, QStringLiteral("Papirus-Dark"));
        AutoColorScheme::setIconsetValue(false, QStringLiteral("Papirus-Light"));

        QVERIFY(!AutoColorScheme::colorDocks());
        QVERIFY(!AutoColorScheme::systemScheme());
        QCOMPARE(AutoColorScheme::systemMonitor(), QStringLiteral("DP-3"));
        QCOMPARE(AutoColorScheme::lightness(), int(WallpaperColors::Options::ForceDark));
        QCOMPARE(AutoColorScheme::selectionMode(),
                 int(WallpaperColors::Options::FromWallpaper));
        QCOMPARE(AutoColorScheme::selectionLight(), QColor(10, 20, 30));
        QCOMPARE(AutoColorScheme::selectionDark(), QColor(200, 210, 220));
        QVERIFY(AutoColorScheme::iconsetEnabled(true));
        QVERIFY(!AutoColorScheme::iconsetEnabled(false));
        QCOMPARE(AutoColorScheme::iconsetValue(true), QStringLiteral("Papirus-Dark"));
        QCOMPARE(AutoColorScheme::iconsetValue(false), QStringLiteral("Papirus-Light"));
    }

    void settingsLandInTheirOwnSection()
    {
        // QSettings mapea a la sección solo el PRIMER '/', así que todas las
        // claves son "ColorAuto/<nombre>" planas y nada variable entra en la
        // segunda mitad. Si alguna vez alguien mete un conector ahí, esto lo
        // agarra: el archivo dejaría de tener la clave donde se la busca.
        AutoColorScheme::setSystemMonitor(QStringLiteral("DP-3"));
        QSettings s(DockConfig::settingsFilePath(), QSettings::IniFormat);
        s.beginGroup(QStringLiteral("ColorAuto"));
        QVERIFY(s.childKeys().contains(QStringLiteral("systemMonitor")));
        // Y ningún subgrupo: eso sería justamente un '/' de más en una clave.
        QVERIFY(s.childGroups().isEmpty());
        s.endGroup();
    }

    // ---- Identidad de los esquemas generados -------------------------------

    void ownSchemeIdsAreRecognised()
    {
        // La guarda de la que depende DarkModeAppearance.
        QVERIFY(AutoColorScheme::isOwnSchemeId(AutoColorScheme::kSchemeIdA));
        QVERIFY(AutoColorScheme::isOwnSchemeId(AutoColorScheme::kSchemeIdB));
        QVERIFY(!AutoColorScheme::isOwnSchemeId(QStringLiteral("BreezeDark")));
        QVERIFY(!AutoColorScheme::isOwnSchemeId(QString()));
        // Y son dos distintos: uno solo haría que plasma-apply-colorscheme
        // ignore la segunda aplicación ("ya está puesto") y la feature se
        // congelaría en el primer fondo, sin un solo error.
        QVERIFY(AutoColorScheme::kSchemeIdA != AutoColorScheme::kSchemeIdB);
    }

    // ---- Defaults ----------------------------------------------------------

    void captureStoresTheLiveValues()
    {
        Theme theme;
        AppearanceControl appearance(&theme);
        theme.setIconTheme(QStringLiteral("Papirus"));

        AutoColorScheme autoColors(&theme, &appearance, nullptr, nullptr);
        autoColors.captureDefaults();

        QVERIFY(AutoColorScheme::defaultsSaved());
        QCOMPARE(AutoColorScheme::defaultIconTheme(), QStringLiteral("Papirus"));
    }

    void captureKeepsAnEmptyOverrideEmpty()
    {
        // El override de iconset del dock se guarda **verbatim**, vacío
        // incluido: vacío significa "seguir el de KDE", y guardar el id
        // resuelto de KDE convertiría un dock que seguía al sistema en uno
        // clavado al volver atrás.
        Theme theme;
        AppearanceControl appearance(&theme);
        theme.setIconTheme(QString());

        AutoColorScheme autoColors(&theme, &appearance, nullptr, nullptr);
        autoColors.captureDefaults();

        QVERIFY(AutoColorScheme::defaultsSaved());
        QVERIFY(AutoColorScheme::defaultIconTheme().isEmpty());
    }

    void captureNeverStoresOneOfOurs()
    {
        // Si por lo que sea el esquema vivo es uno generado, no puede quedar
        // como "el del usuario": ese archivo se borra en el próximo cambio.
        writeShared(QStringLiteral("ColorAuto/defaultColorScheme"),
                    QStringLiteral("BreezeLight"));
        writeShared(QStringLiteral("ColorAuto/defaultsSaved"), true);

        Theme theme;
        AppearanceControl appearance(&theme);
        // Simula el kdeglobals con nuestro esquema puesto.
        QSettings kde(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
                          + QStringLiteral("/kdeglobals"),
                      QSettings::IniFormat);
        kde.setValue(QStringLiteral("ColorScheme"), AutoColorScheme::kSchemeIdA);
        kde.sync();

        AppearanceControl fresh(&theme);
        QCOMPARE(fresh.currentColorScheme(), AutoColorScheme::kSchemeIdA);

        AutoColorScheme autoColors(&theme, &fresh, nullptr, nullptr);
        autoColors.captureDefaults();
        // Conservó el anterior en vez de guardar el generado.
        QCOMPARE(AutoColorScheme::defaultColorScheme(), QStringLiteral("BreezeLight"));
    }

    void userColorSchemeIsWhatDarkModeMustSnapshot()
    {
        QVERIFY(AutoColorScheme::userColorScheme().isEmpty()); // sin defaults, nada
        writeShared(QStringLiteral("ColorAuto/defaultColorScheme"),
                    QStringLiteral("BreezeLight"));
        writeShared(QStringLiteral("ColorAuto/defaultsSaved"), true);
        QCOMPARE(AutoColorScheme::userColorScheme(), QStringLiteral("BreezeLight"));
    }

    // ---- Enclavamiento con el modo oscuro ----------------------------------

    void darkModeSuspendsAndRemembers()
    {
        Theme theme;
        AppearanceControl appearance(&theme);

        // ColorAuto activo y con defaults guardados, como después de un uso
        // normal. Se siembra por la API estática justamente para no disparar la
        // lectura del fondo por D-Bus.
        writeShared(QStringLiteral("ColorAuto/enabled"), true);
        writeShared(QStringLiteral("ColorAuto/defaultColorScheme"),
                    QStringLiteral("BreezeLight"));
        writeShared(QStringLiteral("ColorAuto/defaultIconTheme"), QStringLiteral("Papirus"));
        writeShared(QStringLiteral("ColorAuto/defaultsSaved"), true);
        writeShared(QStringLiteral("ColorAuto/applied"), true);

        AutoColorScheme autoColors(&theme, &appearance, nullptr, nullptr);
        QVERIFY(AutoColorScheme::enabled());

        // Hace falta un dock vivo: anyDarkModeActive() recorre las instancias de
        // DockConfig, así que sin ninguna el modo oscuro nunca "está activo" y
        // el enclavamiento no se ejercita (daría verde sin probar nada).
        DockConfig dock(QStringLiteral("VIRT-9"));

        // Entra el modo oscuro.
        DockConfig::setDarkModeAllDocks(true);
        DockConfig::setDarkModeGlobal(true);
        QVERIFY(DockConfig::anyDarkModeActive());

        // ColorAuto se apagó solo y anotó que tiene que volver.
        QVERIFY(!AutoColorScheme::enabled());
        QVERIFY(!AutoColorScheme::applied());
        QCOMPARE(shared().value(QStringLiteral("ColorAuto/suspendedByDark")).toBool(), true);
        // Y devolvió el iconset del usuario mientras se iba.
        QCOMPARE(theme.iconTheme(), QStringLiteral("Papirus"));

        // Sale el modo oscuro: la marca se limpia enseguida (el re-encendido
        // va con retardo porque las herramientas de Plasma son startDetached).
        DockConfig::setDarkModeGlobal(false);
        QVERIFY(!DockConfig::anyDarkModeActive());
        QCOMPARE(shared().value(QStringLiteral("ColorAuto/suspendedByDark")).toBool(), false);

        DockConfig::setDarkModeAllDocks(false);
    }

    void darkModeDoesNotResumeWhatWasNeverOn()
    {
        Theme theme;
        AppearanceControl appearance(&theme);
        AutoColorScheme autoColors(&theme, &appearance, nullptr, nullptr);
        QVERIFY(!AutoColorScheme::enabled());

        DockConfig::setDarkModeAllDocks(true);
        DockConfig::setDarkModeGlobal(true);
        QCOMPARE(shared().value(QStringLiteral("ColorAuto/suspendedByDark")).toBool(), false);

        DockConfig::setDarkModeGlobal(false);
        // Sigue apagado: no se enciende solo algo que el usuario tenía apagado.
        QVERIFY(!AutoColorScheme::enabled());

        DockConfig::setDarkModeAllDocks(false);
    }

    void repeatedDarkPingsAreIdempotent()
    {
        // La señal del notificador la emite también el color de acento, así que
        // el manejador tiene que poder correr de más sin efecto.
        Theme theme;
        AppearanceControl appearance(&theme);
        writeShared(QStringLiteral("ColorAuto/enabled"), true);
        writeShared(QStringLiteral("ColorAuto/defaultsSaved"), true);

        AutoColorScheme autoColors(&theme, &appearance, nullptr, nullptr);
        DockConfig dock(QStringLiteral("VIRT-9"));
        DockConfig::setDarkModeAllDocks(true);
        DockConfig::setDarkModeGlobal(true);
        QVERIFY(!AutoColorScheme::enabled());

        // Un cambio de acento: mismo estado de modo oscuro, otro ping.
        const QColor before = DockConfig::darkAccentColor();
        DockConfig::setDarkAccentColor(QColor(1, 2, 3));
        // Sigue suspendido, no "des-suspendido" por el ping de más.
        QCOMPARE(shared().value(QStringLiteral("ColorAuto/suspendedByDark")).toBool(), true);
        QVERIFY(!AutoColorScheme::enabled());

        DockConfig::setDarkAccentColor(before);
        DockConfig::setDarkModeGlobal(false);
        DockConfig::setDarkModeAllDocks(false);
    }

    // ---- Los archivos generados --------------------------------------------

    void disablingRemovesTheGeneratedFiles()
    {
        Theme theme;
        AppearanceControl appearance(&theme);

        const QString dir =
            QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
            + QStringLiteral("/color-schemes/");
        QDir().mkpath(dir);
        const QString a = dir + AutoColorScheme::kSchemeIdA + QStringLiteral(".colors");
        const QString b = dir + AutoColorScheme::kSchemeIdB + QStringLiteral(".colors");
        for (const QString &p : {a, b}) {
            QFile f(p);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("[General]\n");
            f.close();
        }

        writeShared(QStringLiteral("ColorAuto/enabled"), true);
        writeShared(QStringLiteral("ColorAuto/defaultsSaved"), true);
        writeShared(QStringLiteral("ColorAuto/applied"), true);

        AutoColorScheme autoColors(&theme, &appearance, nullptr, nullptr);
        autoColors.setEnabled(false);

        // Temporal quiere decir temporal: no le quedan esquemas de kdock dando
        // vueltas en la lista de Preferencias del Sistema.
        QVERIFY(!QFile::exists(a));
        QVERIFY(!QFile::exists(b));
        QVERIFY(!AutoColorScheme::applied());
    }

    // El control positivo de la trampa principal de toda la feature.
    void darkModeSnapshotsTheUserSchemeNotOurs()
    {
        // Con ColorAuto puesto, lo que hay en kdeglobals es un esquema que kdock
        // generó y que se borra en el próximo cambio de fondo. Si el modo oscuro
        // lo fotografía como "lo que tenía el usuario", salir del modo oscuro
        // restaura un esquema inexistente y no hay forma de recuperarse.
        writeShared(QStringLiteral("ColorAuto/applied"), true);
        writeShared(QStringLiteral("ColorAuto/defaultsSaved"), true);
        writeShared(QStringLiteral("ColorAuto/defaultColorScheme"),
                    QStringLiteral("BreezeLight"));
        // Estado limpio del lado del modo oscuro, o sync() corta temprano.
        DockConfig::setDarkAppearanceApplied(false);
        DockConfig::setDarkAppearancePrevious(DockConfig::SystemColorScheme, QString());

        QSettings kde(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
                          + QStringLiteral("/kdeglobals"),
                      QSettings::IniFormat);
        kde.setValue(QStringLiteral("ColorScheme"), AutoColorScheme::kSchemeIdA);
        kde.sync();

        Theme theme;
        AppearanceControl appearance(&theme);
        QCOMPARE(appearance.currentColorScheme(), AutoColorScheme::kSchemeIdA);

        DockConfig dock(QStringLiteral("VIRT-9"));
        DarkModeAppearance darkAppearance(&theme, &appearance);
        DockConfig::setDarkModeAllDocks(true);
        DockConfig::setDarkModeGlobal(true);
        QVERIFY(DockConfig::anyDarkModeActive());
        darkAppearance.sync();

        // Guardó el default de ColorAuto, no el generado. Sin la guarda de
        // darkmodeappearance.cpp esto sería "KdockColorAuto1".
        QCOMPARE(DockConfig::darkAppearancePrevious(DockConfig::SystemColorScheme),
                 QStringLiteral("BreezeLight"));
        QVERIFY(!AutoColorScheme::isOwnSchemeId(
            DockConfig::darkAppearancePrevious(DockConfig::SystemColorScheme)));

        DockConfig::setDarkModeGlobal(false);
        DockConfig::setDarkModeAllDocks(false);
        DockConfig::setDarkAppearanceApplied(false);
    }

    // ---- Generación manual -------------------------------------------------

    void manualGenerationSurvivesARestart()
    {
        // El rescate de arranque (deshacer un esquema temporal que quedó de una
        // corrida muerta) NO puede deshacer una generación hecha a mano: el
        // usuario la pidió explícitamente con la casilla apagada, y desarmarla
        // en cada reinicio del dock sería lo contrario de lo que promete el
        // botón. Es lo que distingue `manual` de `applied`.
        Theme theme;
        AppearanceControl appearance(&theme);
        theme.setIconTheme(QStringLiteral("Gruvbox-Plus-Dark"));

        writeShared(QStringLiteral("ColorAuto/enabled"), false);
        writeShared(QStringLiteral("ColorAuto/applied"), true);
        writeShared(QStringLiteral("ColorAuto/manual"), true);
        writeShared(QStringLiteral("ColorAuto/defaultsSaved"), true);
        writeShared(QStringLiteral("ColorAuto/defaultIconTheme"), QStringLiteral("Papirus"));

        AutoColorScheme autoColors(&theme, &appearance, nullptr, nullptr);
        // Ni restauró el iconset ni soltó la propiedad del esquema.
        QCOMPARE(theme.iconTheme(), QStringLiteral("Gruvbox-Plus-Dark"));
        QVERIFY(AutoColorScheme::applied());
    }

    void generateBumpsTheVariant()
    {
        // Sin esto, dos clics seguidos sobre el mismo fondo dan el mismo
        // esquema y el botón parece roto — que es justo el síntoma reportado.
        Theme theme;
        AppearanceControl appearance(&theme);
        writeShared(QStringLiteral("ColorAuto/variant"), 0);
        writeShared(QStringLiteral("ColorAuto/defaultsSaved"), true);

        AutoColorScheme autoColors(&theme, &appearance, nullptr, nullptr);
        QVERIFY(!AutoColorScheme::enabled()); // apagado a propósito
        autoColors.generateNow();
        QCOMPARE(shared().value(QStringLiteral("ColorAuto/variant")).toInt(), 1);
        autoColors.generateNow();
        QCOMPARE(shared().value(QStringLiteral("ColorAuto/variant")).toInt(), 2);
    }

    void generateCapturesDefaultsWhenThereAreNone()
    {
        // Generar a mano con la casilla apagada es justo el camino que nunca
        // pasó por setEnabled(true), así que si no captura acá el usuario se
        // queda sin forma de volver.
        Theme theme;
        AppearanceControl appearance(&theme);
        theme.setIconTheme(QStringLiteral("Papirus"));
        QVERIFY(!AutoColorScheme::defaultsSaved());

        AutoColorScheme autoColors(&theme, &appearance, nullptr, nullptr);
        autoColors.generateNow();
        QVERIFY(AutoColorScheme::defaultsSaved());
        QCOMPARE(AutoColorScheme::defaultIconTheme(), QStringLiteral("Papirus"));
    }

    void generateStandsDownForDarkMode()
    {
        // El modo oscuro es dueño de la apariencia mientras está puesto; los dos
        // pisándose sería peor que no hacer nada.
        Theme theme;
        AppearanceControl appearance(&theme);
        AutoColorScheme autoColors(&theme, &appearance, nullptr, nullptr);
        DockConfig dock(QStringLiteral("VIRT-9"));
        DockConfig::setDarkModeAllDocks(true);
        DockConfig::setDarkModeGlobal(true);

        writeShared(QStringLiteral("ColorAuto/variant"), 0);
        autoColors.generateNow();
        QCOMPARE(shared().value(QStringLiteral("ColorAuto/variant")).toInt(), 0);

        DockConfig::setDarkModeGlobal(false);
        DockConfig::setDarkModeAllDocks(false);
    }

    // ---- Guardar -----------------------------------------------------------

    void disablingRestoresOnlyWhatWasOurs()
    {
        // restoreDefaults() está gateado por applied(): si ColorAuto nunca llegó
        // a aplicar nada (o el usuario ya se quedó con un esquema guardado), no
        // tiene por qué pisar el iconset ni el esquema que haya puestos.
        Theme theme;
        AppearanceControl appearance(&theme);
        theme.setIconTheme(QStringLiteral("Adwaita"));

        writeShared(QStringLiteral("ColorAuto/enabled"), true);
        writeShared(QStringLiteral("ColorAuto/applied"), false); // nunca aplicó
        writeShared(QStringLiteral("ColorAuto/defaultsSaved"), true);
        writeShared(QStringLiteral("ColorAuto/defaultIconTheme"), QStringLiteral("Papirus"));

        AutoColorScheme autoColors(&theme, &appearance, nullptr, nullptr);
        autoColors.setEnabled(false);
        QCOMPARE(theme.iconTheme(), QStringLiteral("Adwaita"));

        // Y con applied()=true sí restaura, que es el caso normal.
        writeShared(QStringLiteral("ColorAuto/enabled"), true);
        writeShared(QStringLiteral("ColorAuto/applied"), true);
        AutoColorScheme second(&theme, &appearance, nullptr, nullptr);
        second.setEnabled(false);
        QCOMPARE(theme.iconTheme(), QStringLiteral("Papirus"));
    }

    void saveWithoutAnythingGeneratedDoesNotWrite()
    {
        // Un proceso recién arrancado no tiene nada que guardar: generar es un
        // viaje de ida y vuelta por D-Bus, así que no hay esquema sincrónico.
        Theme theme;
        AppearanceControl appearance(&theme);
        AutoColorScheme autoColors(&theme, &appearance, nullptr, nullptr);
        QVERIFY(autoColors.saveCurrentScheme().isEmpty());
    }

    void savedSchemesAreNumberedAndDoNotOverwrite()
    {
        // Se prueba con el motor directo, que es lo que el guardado usa: el
        // nombre tiene que ser incremental o el segundo "guardar" se come al
        // primero.
        const QString dir =
            QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
            + QStringLiteral("/color-schemes");
        QDir().mkpath(dir);
        const SchemeColors s = WallpaperColors::buildScheme(WallpaperPalette{},
                                                            WallpaperColors::Options());
        for (int n = 1; n <= 2; ++n) {
            const QString id = QStringLiteral("kdock-%1").arg(n);
            QVERIFY(WallpaperColors::writeSchemeFile(
                s, dir + QLatin1Char('/') + id + QStringLiteral(".colors"), id,
                QStringLiteral("kdock %1").arg(n)));
        }
        QVERIFY(QFile::exists(dir + QStringLiteral("/kdock-1.colors")));
        QVERIFY(QFile::exists(dir + QStringLiteral("/kdock-2.colors")));
    }

    // ---- Colores de los docks ----------------------------------------------

    void clearingReachesEveryDockThatCouldHaveOne()
    {
        // El bug del 2026-08-12: los colores se ponían recorriendo enabledDocks()
        // y se sacaban recorriendo knownDocks(). En una config real esas dos
        // listas diferían en cuatro docks, que quedaban con un color generado
        // que ya nada podía cambiar — el override de ColorAuto le gana al
        // panelColor del propio dock, así que ni el menú contextual ni el
        // diálogo servían. Ahora se limpia por los configs que el manager
        // repartió, que es exactamente el conjunto que puede tener uno.
        Theme theme;
        AppearanceControl appearance(&theme);

        // Dos docks sobre monitores que no existen: DockManager no les arma
        // ventana (si no, sync() intentaría con todos los servicios en nullptr
        // y el proceso se cae).
        writeShared(QStringLiteral("enabledScreens"),
                    QStringList{QStringLiteral("VIRT-8"), QStringLiteral("VIRT-9")});
        // knownDocks a propósito INCOMPLETA, que es justo el caso del bug.
        writeShared(QStringLiteral("knownDocks"), QStringList{QStringLiteral("VIRT-8")});

        DockManager::Shared shared;
        shared.theme = &theme;
        shared.appearance = &appearance;
        DockManager manager(shared);

        AutoColorScheme autoColors(&theme, &appearance, &manager, nullptr);

        DockConfig *a = manager.configFor(QStringLiteral("VIRT-8"));
        DockConfig *b = manager.configFor(QStringLiteral("VIRT-9"));
        QVERIFY(a && b);
        a->setAutoColors(QColor(10, 20, 30), QColor(200, 200, 200));
        b->setAutoColors(QColor(10, 20, 30), QColor(200, 200, 200));
        QVERIFY(a->autoColorActive());
        QVERIFY(b->autoColorActive());

        writeShared(QStringLiteral("ColorAuto/enabled"), true);
        writeShared(QStringLiteral("ColorAuto/defaultsSaved"), true);
        autoColors.setEnabled(false);

        // Los dos, no solo el que estaba en knownDocks.
        QVERIFY(!a->autoColorActive());
        QVERIFY2(!b->autoColorActive(),
                 "el dock que faltaba en knownDocks quedó pegado a su color generado");
    }

    void aCrashedRunIsCleanedUpOnStartup()
    {
        // El flag `applied` está persistido justamente para esto: una corrida
        // anterior que murió con un esquema generado puesto tiene que quedar
        // deshecha al arrancar, o el usuario se queda clavado en un esquema
        // temporal para siempre.
        Theme theme;
        AppearanceControl appearance(&theme);
        theme.setIconTheme(QStringLiteral("Adwaita"));

        writeShared(QStringLiteral("ColorAuto/enabled"), false); // el usuario lo apagó
        writeShared(QStringLiteral("ColorAuto/applied"), true);  // pero quedó puesto
        writeShared(QStringLiteral("ColorAuto/defaultsSaved"), true);
        writeShared(QStringLiteral("ColorAuto/defaultIconTheme"), QStringLiteral("Papirus"));
        writeShared(QStringLiteral("ColorAuto/defaultColorScheme"),
                    QStringLiteral("BreezeLight"));

        AutoColorScheme autoColors(&theme, &appearance, nullptr, nullptr);
        QVERIFY(!AutoColorScheme::applied());
        QCOMPARE(theme.iconTheme(), QStringLiteral("Papirus"));
    }

    // ---- La fuente de fondos inyectada (el camino de LXQt) -----------------
    //
    // Es lo único que hace testeable el ciclo leer -> muestrear -> aplicar. Bajo
    // Plasma eso es un viaje de ida y vuelta por D-Bus y este test es hermético,
    // así que hasta acá no había un solo caso que lo cubriera.
    //
    // La costura es la de producción —main() le pasa LxqtWallpapers::currentImages
    // cuando la sesión es LXQt—, y lo único falso es de dónde salen las rutas.
    // De yapa, la fuente es sincrónica: applyPalettes() ya corrió cuando
    // generateNow() vuelve, que es lo que deja afirmar sin bombear el bucle.

    void anInjectedSourceReplacesTheDBusRoundTrip()
    {
        Theme theme;
        AppearanceControl appearance(&theme);
        removeGeneratedSchemes();

        const QString image = solidImage(QStringLiteral("ca-lead.png"), QColor(200, 40, 40));
        AutoColorScheme autoColors(&theme, &appearance, nullptr, nullptr);
        autoColors.setWallpaperSource(
            [image] { return QHash<QString, QString>{{kLeadScreen, image}}; });

        autoColors.generateNow();

        // Un esquema en disco, el sistema marcado como nuestro, y la generación
        // reconocida como manual (que es lo que impide que el rescate de arranque
        // la deshaga en el próximo reinicio del dock).
        QVERIFY(QFile::exists(schemePath(appliedSlot() == 0 ? AutoColorScheme::kSchemeIdA
                                                            : AutoColorScheme::kSchemeIdB)));
        QVERIFY(AutoColorScheme::applied());
        QVERIFY(AutoColorScheme::manual());
        // Y hay algo que mostrar en la vista previa de la solapa, que sale del
        // esquema recién generado y no de leer el .colors del disco.
        QVERIFY(!autoColors.previewEntry().isEmpty());
    }

    void generatingTwiceAlternatesTheTwoSchemeNames()
    {
        // La razón de que haya dos nombres y no uno: plasma-apply-colorscheme no
        // hace nada si el nombre pedido ya es el puesto, así que reescribir el
        // mismo .colors sería un no-op silencioso y la feature se congelaría en
        // el primer fondo.
        Theme theme;
        AppearanceControl appearance(&theme);
        removeGeneratedSchemes();

        const QString image = solidImage(QStringLiteral("ca-ab.png"), QColor(40, 160, 90));
        AutoColorScheme autoColors(&theme, &appearance, nullptr, nullptr);
        autoColors.setWallpaperSource(
            [image] { return QHash<QString, QString>{{kLeadScreen, image}}; });

        autoColors.generateNow();
        const int first = appliedSlot();
        autoColors.generateNow();
        QVERIFY(appliedSlot() != first);
    }

    void anIdenticalAutomaticRunDoesNotReapply()
    {
        // El memo de applyPalettes(): mismos fondos y mismos ajustes quiere decir
        // que lo que está puesto ya es la respuesta. Aplicar igual no es gratis
        // ni inofensivo — reescribe el otro .colors, corre las herramientas de
        // Plasma y hace re-resolver los íconos de todos los docks — y un disparo
        // sin cambio de fondo es el caso normal, no la excepción.
        Theme theme;
        AppearanceControl appearance(&theme);
        removeGeneratedSchemes();
        writeShared(QStringLiteral("ColorAuto/enabled"), true);
        writeShared(QStringLiteral("ColorAuto/defaultsSaved"), true);

        const QString image = solidImage(QStringLiteral("ca-memo.png"), QColor(60, 90, 200));
        AutoColorScheme autoColors(&theme, &appearance, nullptr, nullptr);
        autoColors.setWallpaperSource(
            [image] { return QHash<QString, QString>{{kLeadScreen, image}}; });

        autoColors.refreshNow();
        const int first = appliedSlot();
        autoColors.refreshNow();
        QCOMPARE(appliedSlot(), first);

        // Pero mover cualquier ajuste tiene que invalidarlo solo, sin que nadie
        // se acuerde de limpiar el memo en cada setter: por eso optionsKey()
        // viaja junto con las imágenes.
        AutoColorScheme::setLightness(WallpaperColors::Options::ForceDark);
        autoColors.refreshNow();
        QVERIFY(appliedSlot() != first);
    }

    void aManualRunIsExemptFromThatMemo()
    {
        // "Generar Color" otra vez sobre el mismo fondo es exactamente el pedido
        // de rehacerlo con la variante siguiente, así que el memo no lo frena.
        Theme theme;
        AppearanceControl appearance(&theme);
        removeGeneratedSchemes();
        writeShared(QStringLiteral("ColorAuto/enabled"), true);
        writeShared(QStringLiteral("ColorAuto/defaultsSaved"), true);

        const QString image = solidImage(QStringLiteral("ca-manual.png"), QColor(190, 120, 30));
        AutoColorScheme autoColors(&theme, &appearance, nullptr, nullptr);
        autoColors.setWallpaperSource(
            [image] { return QHash<QString, QString>{{kLeadScreen, image}}; });

        autoColors.refreshNow();
        const int automatic = appliedSlot();
        autoColors.generateNow();
        QVERIFY(appliedSlot() != automatic);
    }

    void anEmptySourceAppliesNothing()
    {
        // Lo que ve ColorAuto bajo LXQt cuando kdock no está dibujando los fondos
        // (el fondo de PCManFM NO es respaldo, por decisión). Tiene que ser un
        // no-op limpio: nada aplicado y nada en disco, para que apagar la solapa
        // Wallpapers no le deje al usuario un esquema generado y sin dueño.
        Theme theme;
        AppearanceControl appearance(&theme);
        removeGeneratedSchemes();

        AutoColorScheme autoColors(&theme, &appearance, nullptr, nullptr);
        autoColors.setWallpaperSource([] { return QHash<QString, QString>(); });

        autoColors.generateNow();

        QVERIFY(!AutoColorScheme::applied());
        QVERIFY(!QFile::exists(schemePath(AutoColorScheme::kSchemeIdA)));
        QVERIFY(!QFile::exists(schemePath(AutoColorScheme::kSchemeIdB)));
    }

    void aNewImageResetsTheVariantCounter()
    {
        // Sin esto, después de cambiar de fondo seguirías viendo la variante 8
        // —del fondo nuevo—, que se lee como "eligió un color raro" en vez de
        // como el predominante. Se juzga por el monitor que manda el esquema del
        // sistema, que es el color que el usuario está mirando.
        Theme theme;
        AppearanceControl appearance(&theme);
        removeGeneratedSchemes();
        writeShared(QStringLiteral("ColorAuto/enabled"), true);
        writeShared(QStringLiteral("ColorAuto/defaultsSaved"), true);

        const QString first = solidImage(QStringLiteral("ca-v1.png"), QColor(210, 60, 140));
        const QString second = solidImage(QStringLiteral("ca-v2.png"), QColor(30, 170, 170));
        QString current = first;
        AutoColorScheme autoColors(&theme, &appearance, nullptr, nullptr);
        autoColors.setWallpaperSource(
            [&current] { return QHash<QString, QString>{{kLeadScreen, current}}; });

        autoColors.refreshNow(); // deja lastImage = first
        writeShared(QStringLiteral("ColorAuto/variant"), 5);
        current = second;
        autoColors.refreshNow();

        QCOMPARE(shared().value(QStringLiteral("ColorAuto/variant")).toInt(), 0);
    }

    void saveReturnsTheIdOnTheFirstPress()
    {
        // Con la fuente sincrónica ya no hace falta el "volvé a apretar Guardar":
        // saveCurrentScheme() genera y, como la generación terminó antes de
        // volver, escribe el permanente en la misma llamada. Bajo Plasma sigue
        // haciendo falta, porque ahí la generación es un viaje por D-Bus.
        Theme theme;
        AppearanceControl appearance(&theme);
        removeGeneratedSchemes();

        const QString image = solidImage(QStringLiteral("ca-save.png"), QColor(120, 70, 200));
        AutoColorScheme autoColors(&theme, &appearance, nullptr, nullptr);
        autoColors.setWallpaperSource(
            [image] { return QHash<QString, QString>{{kLeadScreen, image}}; });

        const QString id = autoColors.saveCurrentScheme();

        QVERIFY(!id.isEmpty());
        QVERIFY(id.startsWith(QStringLiteral("kdock-")));
        QVERIFY(QFile::exists(schemePath(id)));
        // Y guardar suelta la propiedad del sistema: desde acá el esquema es del
        // usuario y ni apagar ColorAuto ni el rescate de arranque lo pisan.
        QVERIFY(!AutoColorScheme::applied());
    }
};

KDOCK_TEST_MAIN(TestAutoColorScheme)
#include "tst_autocolorscheme.moc"
