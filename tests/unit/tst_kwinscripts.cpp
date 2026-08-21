// KWinScripts::refresh() no puede emitir changed() cuando nada cambió.
//
// EL BUG QUE ESTO CONGELA (SIGSEGV real, reportado 2026-08-21: el usuario tocó
// "Activar" sobre el script Truely Maximized y el dock se colgó y se cerró).
// La solapa del diálogo se conecta a changed() para repintar su lista, y el
// repintado llamaba refresh() para tener datos frescos:
//
//     rebuildKWinScriptsList() -> refresh() -> changed() -> rebuildKWinScriptsList()
//
// El primer clic entraba a ese ciclo y no salía nunca. Cada vuelta hace un
// scan() —listar dos directorios más una llamada D-Bus bloqueante por script—
// así que primero se ve como un cuelgue y termina en desbordamiento de pila. El
// coredump son cientos de marcos de rebuildKWinScriptsList sobre QDir::entryList.
//
// El arreglo tiene dos mitades y ésta es la general: refresh() compara con el
// caché y sólo emite si el resultado cambió, así que un oyente que refresca de
// vuelta converge en una vuelta en lugar de girar para siempre. La otra mitad
// —que el repintado del diálogo no refresque— no se puede probar acá: depende
// de que org.kde.KWin esté en el bus, y en CI no está.
//
// El script sembrado es lo que hace determinístico al test: sin él el sandbox
// puede no tener ningún script y las dos corridas de scan() darían vacío, con
// lo cual el ciclo no se ejercita y el test pasaría sin probar nada.

#include "kwinscripts.h"
#include "sandbox.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>

namespace {

QString scriptsRoot()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation))
        .filePath(QStringLiteral("kwin/scripts"));
}

// Un paquete KWin/Script mínimo: lo único que scan() mira es el metadata.json.
bool seedScript(const QString &id, const QString &version)
{
    const QString dir = QDir(scriptsRoot()).filePath(id);
    if (!QDir().mkpath(dir))
        return false;
    QFile f(QDir(dir).filePath(QStringLiteral("metadata.json")));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    const QString json = QStringLiteral(
                             "{\n"
                             "  \"KPackageStructure\": \"KWin/Script\",\n"
                             "  \"X-Plasma-API\": \"javascript\",\n"
                             "  \"KPlugin\": {\n"
                             "    \"Id\": \"%1\",\n"
                             "    \"Name\": \"Prueba %1\",\n"
                             "    \"Version\": \"%2\"\n"
                             "  }\n"
                             "}\n")
                             .arg(id, version);
    return f.write(json.toUtf8()) > 0;
}

} // namespace

class TestKWinScripts : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QVERIFY(seedScript(QStringLiteral("kdock-test-script"), QStringLiteral("1.0")));
    }

    // El ciclo del crash, tal cual: el oyente de changed() vuelve a refrescar.
    // Sin el arreglo esto no falla, se cae.
    void aListenerThatRefreshesBackTerminates()
    {
        KWinScripts ks;
        int emissions = 0;
        connect(&ks, &KWinScripts::changed, &ks, [&ks, &emissions] {
            ++emissions;
            QVERIFY2(emissions < 50, "refresh() sigue emitiendo changed() sin que nada cambie");
            ks.refresh(); // <- el sitio de llamada que mataba al dock
        });

        ks.refresh();

        // Una sola vuelta: el caché arranca vacío, el primer scan lo llena y
        // emite; el refresh del oyente encuentra exactamente lo mismo y calla.
        QCOMPARE(emissions, 1);
    }

    // Y la otra cara: si algo cambió de verdad, changed() tiene que salir.
    // Un guardia que nunca emite haría pasar el test de arriba y dejaría la
    // lista del diálogo congelada para siempre.
    void refreshEmitsWhenTheScanDiffers()
    {
        KWinScripts ks;
        ks.refresh(); // llena el caché
        QSignalSpy spy(&ks, &KWinScripts::changed);

        ks.refresh();
        QCOMPARE(spy.count(), 0);

        QVERIFY(seedScript(QStringLiteral("kdock-test-script"), QStringLiteral("2.0")));
        ks.refresh();
        QCOMPARE(spy.count(), 1);
    }

    void cleanupTestCase()
    {
        QDir(QDir(scriptsRoot()).filePath(QStringLiteral("kdock-test-script"))).removeRecursively();
    }
};

KDOCK_TEST_MAIN(TestKWinScripts)
#include "tst_kwinscripts.moc"
