// ConfigArchive: el zip de configuración completa y los presets que se guardan
// con él.
//
// Los casos son las dos formas en que esto rompe callado: un archivo que se
// exporta y el import descarta (pasó de verdad con controlmanager.conf, que
// estaba en los globs y no en el regex de entrada), y un preset sobrescrito que
// se queda con las dos copias adentro porque QZipWriter agrega en vez de pisar.

#include "configarchive.h"
#include "dockconfig.h"
#include "sandbox.h"

#include <QDir>
#include <QFile>
#include <QSettings>
#include <QTest>

class TestConfigArchive : public QObject
{
    Q_OBJECT

private:
    // Escribe <configDir>/<name> con una clave reconocible.
    static void seed(const QString &name, const QString &marker)
    {
        QSettings s(ConfigArchive::configDir() + QLatin1Char('/') + name, QSettings::IniFormat);
        s.setValue(QStringLiteral("marker"), marker);
        s.sync();
    }

    static QString markerOf(const QString &name)
    {
        QSettings s(ConfigArchive::configDir() + QLatin1Char('/') + name, QSettings::IniFormat);
        return s.value(QStringLiteral("marker")).toString();
    }

    // Deja el directorio de config con un juego conocido de archivos.
    static void seedAll(const QString &marker)
    {
        QDir().mkpath(ConfigArchive::configDir());
        seed(QStringLiteral("kdock.conf"), marker);
        seed(QStringLiteral("kdock-VIRT-1.conf"), marker);
        seed(QStringLiteral("previews.conf"), marker);
        seed(QStringLiteral("tilemenu.conf"), marker);
        seed(QStringLiteral("controlmanager.conf"), marker);
        seed(QStringLiteral("weather.conf"), marker);
    }

private slots:
    void exportImportKeepsEveryBinarysConfig()
    {
        // El caso que ya rompió: controlmanager.conf viajaba en el .zip y el
        // import lo tiraba, así que restaurar dejaba el panel de control con la
        // configuración vieja y nadie se enteraba.
        seedAll(QStringLiteral("A"));
        const QString zip = ConfigArchive::configDir() + QStringLiteral("/round.zip");
        QString err;
        QVERIFY2(ConfigArchive::exportTo(zip, &err), qPrintable(err));
        QVERIFY(ConfigArchive::isConfigArchive(zip));

        seedAll(QStringLiteral("B"));
        QVERIFY2(ConfigArchive::importFrom(zip, &err), qPrintable(err));

        for (const QString &name : {QStringLiteral("kdock.conf"),
                                    QStringLiteral("kdock-VIRT-1.conf"),
                                    QStringLiteral("previews.conf"),
                                    QStringLiteral("tilemenu.conf"),
                                    QStringLiteral("controlmanager.conf"),
                                    QStringLiteral("weather.conf")}) {
            QCOMPARE(markerOf(name), QStringLiteral("A"));
        }
        QFile::remove(zip);
    }

    void importRejectsAnArchiveWithoutTheSharedConfig()
    {
        const QString zip = ConfigArchive::configDir() + QStringLiteral("/bogus.zip");
        QFile f(zip);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write("not a zip");
        f.close();
        QVERIFY(!ConfigArchive::isConfigArchive(zip));
        QString err;
        QVERIFY(!ConfigArchive::importFrom(zip, &err));
        QVERIFY(!err.isEmpty());
        QFile::remove(zip);
    }

    void savedPresetsAreListedAndApplyBack()
    {
        seedAll(QStringLiteral("saved"));
        QString err;
        QVERIFY2(ConfigArchive::savePreset(QStringLiteral("Trabajo"), &err), qPrintable(err));
        QVERIFY(ConfigArchive::presetNames().contains(QStringLiteral("Trabajo")));
        QVERIFY(QFile::exists(ConfigArchive::presetPath(QStringLiteral("Trabajo"))));

        seedAll(QStringLiteral("otra"));
        QVERIFY2(ConfigArchive::importFrom(ConfigArchive::presetPath(QStringLiteral("Trabajo")),
                                           &err),
                 qPrintable(err));
        QCOMPARE(markerOf(QStringLiteral("kdock.conf")), QStringLiteral("saved"));
        QCOMPARE(markerOf(QStringLiteral("controlmanager.conf")), QStringLiteral("saved"));

        QVERIFY(ConfigArchive::deletePreset(QStringLiteral("Trabajo"), &err));
        QVERIFY(!ConfigArchive::presetNames().contains(QStringLiteral("Trabajo")));
    }

    void overwritingAPresetReplacesItInsteadOfAppending()
    {
        // QZipWriter agrega al archivo que encuentra: sin el remove previo, el
        // preset guardado dos veces tiene las dos copias de cada .conf y el
        // import restaura la primera, o sea la vieja.
        seedAll(QStringLiteral("v1"));
        QString err;
        QVERIFY2(ConfigArchive::savePreset(QStringLiteral("dupe"), &err), qPrintable(err));
        seedAll(QStringLiteral("v2"));
        QVERIFY2(ConfigArchive::savePreset(QStringLiteral("dupe"), &err), qPrintable(err));

        seedAll(QStringLiteral("v3"));
        QVERIFY2(ConfigArchive::importFrom(ConfigArchive::presetPath(QStringLiteral("dupe")), &err),
                 qPrintable(err));
        QCOMPARE(markerOf(QStringLiteral("kdock.conf")), QStringLiteral("v2"));
        QVERIFY(ConfigArchive::deletePreset(QStringLiteral("dupe"), &err));
    }

    void renameMovesTheFileAndKeepsTheContent()
    {
        seedAll(QStringLiteral("ren"));
        QString err;
        QVERIFY2(ConfigArchive::savePreset(QStringLiteral("viejo"), &err), qPrintable(err));
        QVERIFY2(ConfigArchive::renamePreset(QStringLiteral("viejo"), QStringLiteral("nuevo"), &err),
                 qPrintable(err));
        const QStringList names = ConfigArchive::presetNames();
        QVERIFY(names.contains(QStringLiteral("nuevo")));
        QVERIFY(!names.contains(QStringLiteral("viejo")));
        QVERIFY(ConfigArchive::isConfigArchive(ConfigArchive::presetPath(QStringLiteral("nuevo"))));
        QVERIFY(ConfigArchive::deletePreset(QStringLiteral("nuevo"), &err));
    }

    void presetNamesCannotEscapeTheDirectory()
    {
        // El nombre lo escribe el usuario y termina siendo un nombre de archivo.
        // Las barras se reemplazan y los puntos del principio se caen (un
        // preset oculto no se puede ni borrar desde la solapa).
        QCOMPARE(ConfigArchive::sanitizePresetName(QStringLiteral("../../evil")),
                 QStringLiteral("_.._evil"));
        QCOMPARE(ConfigArchive::sanitizePresetName(QStringLiteral("  con espacios  ")),
                 QStringLiteral("con espacios"));
        QVERIFY(ConfigArchive::sanitizePresetName(QStringLiteral("   ")).isEmpty());
        QVERIFY(ConfigArchive::sanitizePresetName(QStringLiteral("...")).isEmpty());

        QString err;
        QVERIFY(!ConfigArchive::savePreset(QStringLiteral("  "), &err));
        seedAll(QStringLiteral("esc"));
        QVERIFY2(ConfigArchive::savePreset(QStringLiteral("a/b"), &err), qPrintable(err));
        QVERIFY(QFile::exists(ConfigArchive::presetsDir() + QStringLiteral("/a_b.zip")));
        QVERIFY(ConfigArchive::deletePreset(QStringLiteral("a/b"), &err));
    }

    void importPresetRefusesSomethingThatIsNotAKdockArchive()
    {
        const QString path = ConfigArchive::configDir() + QStringLiteral("/foreign.zip");
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write("PK not really");
        f.close();
        QString err;
        QVERIFY(!ConfigArchive::importPreset(path, QStringLiteral("foreign"), &err));
        QVERIFY(!ConfigArchive::presetNames().contains(QStringLiteral("foreign")));

        seedAll(QStringLiteral("imp"));
        const QString good = ConfigArchive::configDir() + QStringLiteral("/good.zip");
        QVERIFY2(ConfigArchive::exportTo(good, &err), qPrintable(err));
        QVERIFY2(ConfigArchive::importPreset(good, QStringLiteral("desde-zip"), &err),
                 qPrintable(err));
        QVERIFY(ConfigArchive::presetNames().contains(QStringLiteral("desde-zip")));
        QVERIFY(ConfigArchive::deletePreset(QStringLiteral("desde-zip"), &err));
        QFile::remove(path);
        QFile::remove(good);
    }

    void theNoPromptFlagSurvivesAReread()
    {
        // Vive en el kdock.conf compartido y se lee de disco en cada apertura
        // del diálogo, así que un sync que falte se ve como una casilla que se
        // desmarca sola.
        DockConfig::setPresetApplyNoPrompt(true);
        QVERIFY(DockConfig::presetApplyNoPrompt());
        DockConfig::setPresetApplyNoPrompt(false);
        QVERIFY(!DockConfig::presetApplyNoPrompt());
    }
};

KDOCK_TEST_MAIN(TestConfigArchive)
#include "tst_configarchive.moc"
