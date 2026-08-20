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
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
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
};

KDOCK_TEST_MAIN(TestQtCompat)
#include "tst_qtcompat.moc"
