// QtCompat — la traducción del esquema de KDE a la paleta de LXQt.
//
// Todo lo que hay que probar acá es de archivo a archivo: entra un `kdeglobals`
// y sale el grupo `[Palette]` de `lxqt.conf`. No hace falta ni D-Bus ni el
// plugin de LXQt, y el sandbox de `sandbox.h` aísla **`XDG_CONFIG_HOME`**, que
// es justo lo que hace que este test no le cambie los colores a nadie:
// `lxqt.conf` sale de `QSettings::UserScope`, o sea de ahí. Con solo aislar
// `XDG_DATA_HOME` —que es lo que alcanza para casi todo lo demás del proyecto—
// una corrida le repintaría la sesión al que la ejecute.
//
// Las tres cosas que se afirman, y por qué cada una:
//
//   - **El mapeo de las diez claves.** Es la tabla que define la feature; un
//     grupo puesto en la clave equivocada se ve como "los tooltips quedaron del
//     esquema anterior" y no como un bug.
//   - **Que apagado no escriba nada.** La clase toca la sesión real del usuario
//     y no hay caja de arena que la contenga fuera de un test: la reja de que
//     `enabled` arranca en false es lo único que hace inocua a cualquier sonda
//     que la construya.
//   - **Que una reescritura idéntica no toque el archivo.** El tema de
//     plataforma de LXQt solo rearma su paleta cuando alguno de los diez valores
//     cambió (su `paletteChanged_`), así que reescribir lo mismo no repinta nada
//     y solo churnea el archivo cada vez que algo mira `kdeglobals`.

#include "qtcompat.h"
#include "dockconfig.h"
#include "sandbox.h"
#include "theme.h"

#include <QFile>
#include <QFileDevice>
#include <QFileInfo>
#include <QFont>
#include <QGuiApplication>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTextStream>

namespace {

QString kdeglobalsPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
           + QStringLiteral("/kdeglobals");
}

// Un kdeglobals con un color distinto y reconocible por cada grupo, para que un
// mapeo cruzado se lea de la aserción y no haya que ir a mirar el archivo.
// `windowBg` es lo único que varía: es lo que mueve la prueba de propagación.
void writeKdeglobals(const QString &windowBg = QStringLiteral("17,18,19"))
{
    QFile f(kdeglobalsPath());
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
    QTextStream out(&f);
    out << "[Colors:Window]\n"
        << "BackgroundNormal=" << windowBg << "\n"
        << "ForegroundNormal=201,202,203\n"
        << "\n[Colors:View]\n"
        << "BackgroundNormal=31,32,33\n"
        << "ForegroundNormal=211,212,213\n"
        << "ForegroundLink=41,42,43\n"
        << "ForegroundVisited=51,52,53\n"
        << "\n[Colors:Selection]\n"
        << "BackgroundNormal=61,62,63\n"
        << "ForegroundNormal=221,222,223\n"
        << "\n[Colors:Tooltip]\n"
        << "BackgroundNormal=71,72,73\n"
        << "ForegroundNormal=231,232,233\n"
        << "\n[Icons]\n"
        << "Theme=TestIcons\n"
        << "\n[General]\n"
        << "ColorScheme=TestScheme\n";
}

QString paletteValue(const QString &key)
{
    QSettings lxqt(QSettings::UserScope, QStringLiteral("lxqt"), QStringLiteral("lxqt"));
    // sync() y no confiar en el objeto recién construido: QSettings comparte un
    // caché por ruta y lo revalida por (mtime, tamaño). Varias de estas pruebas
    // escriben lxqt.conf con valores del mismo largo y a pocos milisegundos de
    // la lectura anterior, que es justo el caso que el caché no distingue: sin
    // esto el lector ve el valor viejo y la aserción falla contra un archivo
    // que en disco ya está bien.
    lxqt.sync();
    lxqt.beginGroup(QStringLiteral("Palette"));
    return lxqt.value(key).toString();
}

// El registro de las herramientas falsas (tests/lib/fakebin.sh), que es donde se
// ve **la decisión**: qué argumentos recibió kwriteconfig6. El archivo vive al
// lado del binario falso, así que sale del primer directorio del PATH que ctest
// arma para estos tests.
QString callsLogPath()
{
    const QString exe = QStandardPaths::findExecutable(QStringLiteral("kwriteconfig6"));
    if (exe.isEmpty())
        return {};
    return QFileInfo(exe).absolutePath() + QStringLiteral("/calls.log");
}

QString readCallsLog()
{
    QFile f(callsLogPath());
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    return QString::fromUtf8(f.readAll());
}

void clearCallsLog()
{
    QFile f(callsLogPath());
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.close();
}

// Vaciar el registro **después** de dejar aterrizar lo que ya estaba en vuelo.
// Las herramientas salen por QProcess::startDetached, así que las de una prueba
// anterior pueden escribir su línea bien después de que esa prueba terminó: un
// clearCallsLog() a secas deja pasar esas rezagadas y la prueba siguiente las
// cuenta como propias. Le costó dos diagnósticos a la de la casilla de colores,
// que veía tres invocaciones que no había hecho nadie.
void settleAndClearCallsLog()
{
    QTest::qWait(700);
    clearCallsLog();
}

// Una clave suelta de lxqt.conf, con el mismo sync() y por la misma razón.
QString lxqtValue(const QString &key)
{
    QSettings lxqt(QSettings::UserScope, QStringLiteral("lxqt"), QStringLiteral("lxqt"));
    lxqt.sync();
    return lxqt.value(key).toString();
}

} // namespace

class TestQtCompat : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase() { writeKdeglobals(); }

    void cleanup() { writeKdeglobals(); }

    void init()
    {
        // El ajuste es estático y vive en el kdock.conf compartido: sin esto la
        // segunda prueba arranca con lo que dejó la primera.
        QSettings s(DockConfig::settingsFilePath(), QSettings::IniFormat);
        s.remove(QStringLiteral("QtCompat"));
        s.sync();

        // Y **lxqt.conf también hay que vaciarlo**, que es lo que no era obvio:
        // es el archivo de salida, no un ajuste nuestro, así que lo que escribió
        // la prueba anterior sigue ahí. Cualquier aserción de la forma "esta
        // parte está apagada, así que su clave tiene que estar vacía" ve el
        // valor que dejó la corrida de al lado y falla señalando al producto.
        // clear() y no borrar el archivo: así el caché de QSettings del propio
        // proceso queda coherente sin depender de la revalidación por (mtime,
        // tamaño).
        QSettings lxqt(QSettings::UserScope, QStringLiteral("lxqt"), QStringLiteral("lxqt"));
        lxqt.clear();
        lxqt.sync();
    }

    // La tabla de la feature: cada clave de lxqt.conf contra su grupo de
    // kdeglobals.
    void translationMapsEveryGroup()
    {
        QtCompat compat;
        QVariantMap byKey;
        for (const QVariant &e : compat.translation()) {
            const QVariantMap m = e.toMap();
            byKey.insert(m.value(QStringLiteral("key")).toString(),
                         m.value(QStringLiteral("color")).toString());
        }
        QCOMPARE(byKey.size(), 10);
        QCOMPARE(byKey.value(QStringLiteral("window_color")).toString(), QStringLiteral("#111213"));
        QCOMPARE(byKey.value(QStringLiteral("window_text_color")).toString(),
                 QStringLiteral("#c9cacb"));
        QCOMPARE(byKey.value(QStringLiteral("base_color")).toString(), QStringLiteral("#1f2021"));
        QCOMPARE(byKey.value(QStringLiteral("text_color")).toString(), QStringLiteral("#d3d4d5"));
        QCOMPARE(byKey.value(QStringLiteral("highlight_color")).toString(),
                 QStringLiteral("#3d3e3f"));
        QCOMPARE(byKey.value(QStringLiteral("highlighted_text_color")).toString(),
                 QStringLiteral("#dddedf"));
        QCOMPARE(byKey.value(QStringLiteral("link_color")).toString(), QStringLiteral("#292a2b"));
        QCOMPARE(byKey.value(QStringLiteral("link_visited_color")).toString(),
                 QStringLiteral("#333435"));
        QCOMPARE(byKey.value(QStringLiteral("tooltip_base_color")).toString(),
                 QStringLiteral("#474849"));
        QCOMPARE(byKey.value(QStringLiteral("tooltip_text_color")).toString(),
                 QStringLiteral("#e7e8e9"));
    }

    // Apagado es inerte: ni construirlo ni pedirle que aplique escribe nada.
    void offWritesNothing()
    {
        QVERIFY(!QtCompat::enabled());
        QtCompat compat;
        compat.applyNow();
        QVERIFY(paletteValue(QStringLiteral("window_color")).isEmpty());
    }

    void enablingApplies()
    {
        QtCompat compat;
        compat.setEnabled(true);
        QVERIFY(QtCompat::enabled());
        QCOMPARE(paletteValue(QStringLiteral("window_color")), QStringLiteral("#111213"));
        QCOMPARE(paletteValue(QStringLiteral("tooltip_text_color")), QStringLiteral("#e7e8e9"));
    }

    // Reescribir lo mismo no toca el archivo. Se mide por (mtime, tamaño) y no
    // por el contenido: el contenido sería idéntico de las dos formas, que es
    // exactamente lo que esta prueba tiene que distinguir.
    void identicalRewriteDoesNotTouchTheFile()
    {
        QtCompat compat;
        compat.setEnabled(true);

        const QString path = QtCompat::lxqtConfPath();
        const QFileInfo before(path);
        const QDateTime stamp = before.lastModified();
        const qint64 size = before.size();
        QVERIFY(stamp.isValid());

        // Un segundo largo: la resolución de mtime de algunos sistemas de
        // archivos es de un segundo, y sin esto la prueba pasaría igual con la
        // guarda sacada.
        QTest::qSleep(1100);
        compat.applyNow();

        const QFileInfo after(path);
        QCOMPARE(after.lastModified(), stamp);
        QCOMPARE(after.size(), size);
    }

    // Apagar deja puesto lo último aplicado: es la decisión de producto (no hay
    // restauración), y sin una aserción se lee como un olvido.
    void disablingLeavesThePaletteBehind()
    {
        QtCompat compat;
        compat.setEnabled(true);
        QCOMPARE(paletteValue(QStringLiteral("window_color")), QStringLiteral("#111213"));
        compat.setEnabled(false);
        QVERIFY(!QtCompat::enabled());
        QCOMPARE(paletteValue(QStringLiteral("window_color")), QStringLiteral("#111213"));
    }

    // **La mitad que hace útil a la feature.** Nadie llama a apply() a mano en
    // la vida real: el disparador es que alguien haya cambiado el esquema de
    // KDE, y eso llega como un Theme::changed. Sin esta prueba, un QtCompat
    // construido sin Theme (o con la conexión rota) pasaría todo lo de arriba y
    // en la sesión no se movería nada nunca.
    void aSchemeChangeInKdeglobalsPropagates()
    {
        Theme theme;
        QtCompat compat(&theme);
        compat.setEnabled(true);
        QCOMPARE(paletteValue(QStringLiteral("window_color")), QStringLiteral("#111213"));

        // Lo que haría plasma-apply-colorscheme: reescribir kdeglobals. El
        // color nuevo se elige de **otro largo** que el viejo, y va detrás de
        // una pausa: QSettings revalida su caché por (mtime, tamaño) con el
        // mtime en milisegundos, así que una reescritura del mismo tamaño a
        // pocos milisegundos de la anterior es indistinguible de "no cambió
        // nada" y esta prueba salía cara o cruca. Lo que se prueba acá es el
        // cableado (que un cambio en KDE llegue a lxqt.conf), no el caché — de
        // eso se ocupa anExternalEditIsOverwrittenByApplyNow.
        QTest::qSleep(50);
        writeKdeglobals(QStringLiteral("9,9,9"));

        // QTRY_COMPARE y no un QSignalSpy::wait(): el watcher de Theme rearma
        // con un singleShot de 200 ms y QtCompat coalesce otros 250, y con esos
        // tiempos la señal puede llegar *antes* de que empiece el wait() — que
        // devuelve false en ese caso y hace fallar una prueba que en realidad
        // pasó. Lo que importa igual es el resultado, no la señal.
        QTRY_COMPARE_WITH_TIMEOUT(paletteValue(QStringLiteral("window_color")),
                                  QStringLiteral("#090909"), 5000);
    }

    // Lo que hace falta para que el botón "Aplicar ahora" sirva de algo: si
    // otro programa (lxqt-config-appearance) le movió la paleta por afuera, hay
    // que volver a escribirla. Es la otra cara de la guarda de idempotencia, y
    // el pozo es el mismo caché de QSettings: la escritura de acá va por QFile
    // a propósito, que es como llega la de un programa ajeno — con un QSettings
    // el caché del propio proceso se actualizaría solo y la prueba no probaría
    // nada.
    void anExternalEditIsOverwrittenByApplyNow()
    {
        QtCompat compat;
        compat.setEnabled(true);
        QCOMPARE(paletteValue(QStringLiteral("window_color")), QStringLiteral("#111213"));

        // Mismo largo que el valor bueno: es el caso que el caché no ve.
        const QString path = QtCompat::lxqtConfPath();
        QString text;
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::ReadOnly | QIODevice::Text));
            text = QString::fromUtf8(f.readAll());
        }
        text.replace(QStringLiteral("window_color=#111213"),
                     QStringLiteral("window_color=#abcdef"));
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text));
            QVERIFY(f.write(text.toUtf8()) > 0);
        }

        compat.applyNow();
        QCOMPARE(paletteValue(QStringLiteral("window_color")), QStringLiteral("#111213"));
    }

    // ---- Iconset -----------------------------------------------------------
    // Espejo de `Icons/Theme`, con la misma lógica que los colores. La clave del
    // lado de LXQt va **al nivel raíz** de QSettings a propósito: eso es la
    // sección [General] del archivo, que es de donde la lee el plugin.
    void iconThemeIsMirrored()
    {
        QtCompat compat;
        QCOMPARE(compat.iconTheme(), QStringLiteral("TestIcons"));
        compat.setEnabled(true);
        QCOMPARE(lxqtValue(QStringLiteral("icon_theme")), QStringLiteral("TestIcons"));
    }

    // Cada parte se apaga sola sin arrastrar a las otras.
    void iconsCanBeTurnedOffOnTheirOwn()
    {
        QtCompat compat;
        compat.setIconsEnabled(false);
        compat.setEnabled(true);
        QVERIFY(lxqtValue(QStringLiteral("icon_theme")).isEmpty());
        QCOMPARE(paletteValue(QStringLiteral("window_color")), QStringLiteral("#111213"));
    }

    void colorsCanBeTurnedOffOnTheirOwn()
    {
        QtCompat compat;
        compat.setColorsEnabled(false);
        compat.setEnabled(true);
        QVERIFY(paletteValue(QStringLiteral("window_color")).isEmpty());
        QCOMPARE(lxqtValue(QStringLiteral("icon_theme")), QStringLiteral("TestIcons"));
    }

    // Un kdeglobals sin iconset **no** escribe la clave. Escribirla vacía la
    // llevaría al "oxygen" por defecto del plugin, que no es lo que "no se pudo
    // leer el iconset" tiene que hacer.
    void anAbsentIconThemeIsNotWritten()
    {
        {
            QFile f(kdeglobalsPath());
            QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
            QTextStream out(&f);
            out << "[Colors:Window]\nBackgroundNormal=17,18,19\n";
        }
        QtCompat compat;
        QVERIFY(compat.iconTheme().isEmpty());
        compat.setEnabled(true);
        QVERIFY(lxqtValue(QStringLiteral("icon_theme")).isEmpty());
    }

    // ---- La mitad que le habla a las apps de KDE ---------------------------
    // `plasma-apply-colorscheme` escribe [General] ColorScheme y los grupos
    // Colors:*, pero **nunca** [UiSettings] ColorScheme — y KColorSchemeManager,
    // que construye toda app KXmlGui, lee solo esa última. Sin ella decide que
    // no hay nada configurado y pisa la paleta que el tema de plataforma acababa
    // de dar, con su Breeze Light compilado adentro (#eff0f1). Bajo Plasma no se
    // notaba porque el tema de plataforma es plasma-integration; bajo LXQt es el
    // bug entero (2026-08-20, reportado con Dolphin).
    //
    // Acá se prueba **la decisión**, que es lo que suele estar mal: qué
    // argumentos recibe kwriteconfig6. La escritura de verdad no se puede
    // afirmar —es `startDetached` contra una herramienta falsa— y tampoco haría
    // falta: que la clave arregla a Dolphin se midió con el binario de verdad.
    void theUiSettingsKeyIsPropagatedForKdeApps()
    {
        QVERIFY(!callsLogPath().isEmpty());
        settleAndClearCallsLog();
        // Reescribir el kdeglobals acá y no confiar en el cleanup(): la
        // herramienta sale asíncrona, así que una corrida **fuera de ctest**
        // —donde el PATH falso no está y kwriteconfig6 es el de verdad— puede
        // dejar la clave escrita después de que el cleanup pasó. El test se
        // ejecuta por ctest, pero una precondición que depende de eso se lee
        // como un bug del producto cuando alguien corre el binario a mano.
        writeKdeglobals();

        QtCompat compat;
        QCOMPARE(compat.kdeColorScheme(), QStringLiteral("TestScheme"));
        QVERIFY(compat.kdeUiSettingsScheme().isEmpty());
        compat.setEnabled(true);

        // QTRY y no una lectura directa: la herramienta sale por
        // QProcess::startDetached, o sea **asíncrona**, y leer el registro en la
        // línea siguiente gana la carrera solo a veces (medido: pasaba una
        // corrida sí y otra no).
        QTRY_VERIFY_WITH_TIMEOUT(readCallsLog().contains(QStringLiteral("kwriteconfig6 <-")), 5000);
        const QString log = readCallsLog();
        QVERIFY2(log.contains(QStringLiteral("--group UiSettings")), qPrintable(log));
        QVERIFY2(log.contains(QStringLiteral("--key ColorScheme TestScheme")), qPrintable(log));
        // Y en kdeglobals, no en otro archivo.
        QVERIFY2(log.contains(QStringLiteral("--file kdeglobals")), qPrintable(log));
    }

    // Con la casilla de colores apagada no se toca kdeglobals: la clave es parte
    // de aplicar el esquema, no un arreglo global que kdock imponga siempre.
    void theUiSettingsKeyFollowsTheColorsSwitch()
    {
        settleAndClearCallsLog();
        QtCompat compat;
        compat.setColorsEnabled(false);
        compat.setEnabled(true);
        // Espera activa por la razón contraria a la de arriba: hay que darle a
        // la herramienta el tiempo que habría tardado en aparecer, o el "no se
        // llamó" pasa por llegar antes.
        QTest::qWait(600);
        QVERIFY2(!readCallsLog().contains(QStringLiteral("UiSettings")),
                 qPrintable(readCallsLog()));
    }

    // ---- Fuentes -----------------------------------------------------------
    // La única parte con valor propio: kdock no tiene ninguna fuente de
    // aplicación que copiar, así que no hay espejo posible.
    void fontsAreOffAndEmptyByDefault()
    {
        QVERIFY(!QtCompat::fontsEnabled());
        QVERIFY(QtCompat::font(QtCompat::GeneralFont).isEmpty());
        QtCompat compat;
        compat.setEnabled(true);
        QVERIFY(lxqtValue(QStringLiteral("Qt/font")).isEmpty());
    }

    void aFontIsWrittenVerbatim()
    {
        QFont f(QStringLiteral("Test Sans"), 13);
        const QString encoded = f.toString();

        QtCompat compat;
        compat.setFont(QtCompat::GeneralFont, encoded);
        compat.setFontsEnabled(true);
        compat.setEnabled(true);

        // Verbatim: el valor viaja como QFont::toString() de punta a punta, que
        // es la codificación que usa lxqt.conf. Re-serializarlo por el camino
        // perdería lo que kdock no entienda (un peso, un nombre de estilo).
        QCOMPARE(lxqtValue(QStringLiteral("Qt/font")), encoded);
        // Y la monoespaciada, que no se tocó, queda sin escribir: vacío es
        // "dejale la suya a LXQt", no "borrala".
        QVERIFY(lxqtValue(QStringLiteral("Qt/fixedFont")).isEmpty());
    }

    // Con nada guardado, la solapa muestra la fuente que LXQt ya tiene. Sin eso
    // el selector abriría en la fuente por omisión de Qt y el primer OK le
    // cambiaría la fuente al escritorio a uno que el usuario nunca eligió.
    void fontForFallsBackToWhatLxqtHas()
    {
        {
            QSettings lxqt(QSettings::UserScope, QStringLiteral("lxqt"), QStringLiteral("lxqt"));
            lxqt.setValue(QStringLiteral("Qt/font"), QStringLiteral("Preexisting,11,-1,5,400,0,0,0,0,0,0,0,0,0,0,1"));
            lxqt.sync();
        }
        QVERIFY(QtCompat::font(QtCompat::GeneralFont).isEmpty());
        QCOMPARE(QtCompat::fontFor(QtCompat::GeneralFont),
                 QStringLiteral("Preexisting,11,-1,5,400,0,0,0,0,0,0,0,0,0,0,1"));
    }

    // El resto del archivo es del usuario: escribir la paleta no puede llevarse
    // por delante el estilo ni la fuente que puso lxqt-config-appearance.
    void otherGroupsSurvive()
    {
        {
            QSettings lxqt(QSettings::UserScope, QStringLiteral("lxqt"), QStringLiteral("lxqt"));
            lxqt.setValue(QStringLiteral("Qt/style"), QStringLiteral("Breeze"));
            lxqt.setValue(QStringLiteral("General/theme"), QStringLiteral("graphite"));
            lxqt.sync();
        }
        QtCompat compat;
        compat.setEnabled(true);

        QSettings lxqt(QSettings::UserScope, QStringLiteral("lxqt"), QStringLiteral("lxqt"));
        QCOMPARE(lxqt.value(QStringLiteral("Qt/style")).toString(), QStringLiteral("Breeze"));
        QCOMPARE(lxqt.value(QStringLiteral("General/theme")).toString(),
                 QStringLiteral("graphite"));
        QCOMPARE(paletteValue(QStringLiteral("window_color")), QStringLiteral("#111213"));
    }

    // ---- Entorno de activación ----------------------------------------------
    // `updateActivationEnvironment()` escribe en el almacén de activación de la
    // sesión (systemd + D-Bus), que **le sobrevive**: lo que publica lo heredan
    // todas las apps activadas por D-Bus, spectacle incluido. El bug que motivó
    // la lista blanca (2026-08-21) fue `--all`, que subió el entorno entero de
    // un kdock arrancado desde un arnés de Xvfb —DISPLAY=:99, XAUTHORITY de un
    // xvfb-run muerto, XDG_* de un sandbox descartable— a la sesión del usuario.
    // Esta prueba congela las dos mitades del arreglo: la reja por plataforma y
    // la prohibición de `--all`.
    void updateActivationEnvironmentNeverPublishesEverything()
    {
        // Las herramientas falsas van en un directorio descartable primero en el
        // PATH, no en el fakebin de la suite: así, aunque la reja por plataforma
        // regresione, la llamada cae en un fake y nunca en la herramienta de la
        // sesión real. QProcess usa `waitForFinished`, o sea síncrono: alcanza
        // con leer el registro después de la llamada.
        QTemporaryDir fakeDir;
        QVERIFY(fakeDir.isValid());
        const QString logPath = fakeDir.filePath(QStringLiteral("calls.log"));
        for (const char *tool : {"dbus-update-activation-environment", "systemctl"}) {
            const QString path = fakeDir.filePath(QLatin1String(tool));
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
            f.write("#!/bin/sh\n");
            f.write("echo \"$(basename \"$0\") <- $*\" >> "
                    + logPath.toUtf8() + "\n");
            f.write("exit 0\n");
            f.close();
            QVERIFY(f.setPermissions(f.permissions() | QFileDevice::ExeOwner
                                     | QFileDevice::ExeGroup | QFileDevice::ExeOther));
        }

        const QByteArray oldPath = qgetenv("PATH");
        qputenv("PATH", fakeDir.path().toUtf8() + ':' + oldPath);
        QtCompat::updateActivationEnvironment();
        qputenv("PATH", oldPath);

        QFile logFile(logPath);
        const QString log = logFile.open(QIODevice::ReadOnly | QIODevice::Text)
                                ? QString::fromUtf8(logFile.readAll())
                                : QString();

        // Pase lo que pase —la reja corta bajo el arnés o la lista blanca se
        // publica en una sesión Wayland de verdad— lo único que causó el bug no
        // puede aparecer.
        QVERIFY2(!log.contains(QStringLiteral("--all")), qPrintable(log));

        if (QGuiApplication::platformName() == QLatin1String("wayland")) {
            // Sesión real: solo las claves del tema de plataforma, ninguna de
            // sandbox.
            QVERIFY2(log.contains(QStringLiteral("--systemd")), qPrintable(log));
            QVERIFY2(log.contains(QStringLiteral("QT_QPA_PLATFORMTHEME")), qPrintable(log));
            QVERIFY2(log.contains(QStringLiteral("XDG_CURRENT_DESKTOP")), qPrintable(log));
            QVERIFY2(!log.contains(QStringLiteral("XDG_CONFIG_HOME")), qPrintable(log));
        } else {
            // Arnés (xcb/offscreen): inerte, la reja cortó antes de buscar la
            // herramienta.
            QVERIFY2(log.isEmpty(), qPrintable(log));
        }
    }
};

KDOCK_TEST_MAIN(TestQtCompat)
#include "tst_qtcompat.moc"
