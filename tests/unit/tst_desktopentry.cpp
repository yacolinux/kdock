// DesktopEntryIndex::forAppId() — que una ventana encuentre su lanzador.
//
// Cuando esto falla el síntoma es siempre el mismo y no parece un bug de
// búsqueda: el ícono anclado no captura su ventana, la ventana se dibuja aparte
// con ícono propio, y el lanzador —que se sigue viendo vacío— abre otra
// instancia en cada clic.
//
// Los .desktop se escriben en el sandbox en vez de vivir como fixtures: la
// heurística del directorio de instalación necesita un ejecutable de verdad para
// resolver con canonicalFilePath(), y eso solo se puede armar en tiempo de
// ejecución.

#include "desktopentry.h"
#include "sandbox.h"

#include <QDir>
#include <QFile>
#include <QTest>

namespace {
// El id de extensión de una PWA de Chromium: 32 letras de la 'a' a la 'p'.
const QString kExtId = QStringLiteral("lgnggepjiihbfdbedefdhcffnmhcahbm");
const QString kLegacyExtId = QStringLiteral("abcdefghijklmnopabcdefghijklmnop");

void writeDesktop(const QString &path, const QString &body)
{
    QFile f(path);
    QVERIFY2(f.open(QIODevice::WriteOnly | QIODevice::Text), qPrintable(path));
    f.write(body.toUtf8());
}
} // namespace

class TestDesktopEntry : public QObject
{
    Q_OBJECT

private:
    QString m_appsDir;

private slots:
    void initTestCase()
    {
        const QString root = kdocktest::sandboxDir().path();
        m_appsDir = root + QStringLiteral("/data/applications");
        QDir().mkpath(m_appsDir);

        // Un binario real dentro de un directorio llamado "msedge": es la
        // identidad que Chromium le da a sus propias ventanas.
        const QString binDir = root + QStringLiteral("/opt/msedge");
        QDir().mkpath(binDir);
        const QString binary = binDir + QStringLiteral("/microsoft-edge");
        QFile bin(binary);
        QVERIFY(bin.open(QIODevice::WriteOnly));
        bin.write("#!/bin/sh\n");
        bin.close();
        bin.setPermissions(bin.permissions() | QFileDevice::ExeOwner);

        writeDesktop(m_appsDir + QStringLiteral("/org.kde.dolphin.desktop"),
                     QStringLiteral("[Desktop Entry]\nType=Application\nName=Dolphin\n"
                                    "Exec=dolphin %u\nIcon=system-file-manager\n"));
        writeDesktop(m_appsDir + QStringLiteral("/microsoft-edge.desktop"),
                     QStringLiteral("[Desktop Entry]\nType=Application\nName=Microsoft Edge\n"
                                    "Exec=%1 %U\nIcon=microsoft-edge\n").arg(binary));
        // La PWA visible lleva el guion bajo que usan algunas instalaciones de
        // Chromium, pero también existe una copia exacta NoDisplay con el id
        // que KWin entrega actualmente. El índice debe elegir la visible.
        writeDesktop(m_appsDir + QStringLiteral("/msedge-_%1-Default.desktop").arg(kExtId),
                     QStringLiteral("[Desktop Entry]\nType=Application\nName=Reddit ED\n"
                                    "Exec=%1 --app-id=%2\nIcon=reddit\n"
                                    "StartupWMClass=crx__%2\n").arg(kExtId, kExtId));
        writeDesktop(m_appsDir + QStringLiteral("/msedge-%1-Default.desktop").arg(kExtId),
                     QStringLiteral("[Desktop Entry]\nType=Application\nName=Reddit oculto\n"
                                    "NoDisplay=true\nExec=%1 --app-id=%2\nIcon=reddit\n"
                                    "StartupWMClass=crx__%2\n").arg(kExtId, kExtId));
        // Mantiene cubierto el caso inverso: una ventana con guion bajo y un
        // lanzador visible cuyo id no lo tiene.
        writeDesktop(m_appsDir + QStringLiteral("/msedge-%1-Default.desktop").arg(kLegacyExtId),
                     QStringLiteral("[Desktop Entry]\nType=Application\nName=Reddit legacy\n"
                                    "Exec=msedge --app-id=%1\nIcon=reddit\n"
                                    "StartupWMClass=crx__%1\n").arg(kLegacyExtId));
    }

    void exactAndLastSegmentMatches()
    {
        DesktopEntryIndex apps;
        QCOMPARE(apps.forAppId(QStringLiteral("org.kde.dolphin")).id,
                 QStringLiteral("org.kde.dolphin"));
        // "dolphin" -> "org.kde.dolphin" y al revés: los dos sentidos.
        QCOMPARE(apps.forAppId(QStringLiteral("dolphin")).id, QStringLiteral("org.kde.dolphin"));
        // Y sin importar mayúsculas, que es como llegan algunos app_id.
        QCOMPARE(apps.forAppId(QStringLiteral("ORG.KDE.Dolphin")).id,
                 QStringLiteral("org.kde.dolphin"));
    }

    void edgeWindowArrivesAsItsInstallDirectory()
    {
        // Una ventana recién abierta de Edge llega como "msedge" (el directorio
        // del binario real), nunca como "microsoft-edge", y KWin **no manda una
        // corrección después**: las que ya estaban abiertas al arrancar el dock
        // sí traen el id completo, y por eso "reiniciar y probar" esconde el bug.
        DesktopEntryIndex apps;
        QCOMPARE(apps.forAppId(QStringLiteral("msedge")).id, QStringLiteral("microsoft-edge"));
    }

    void pwaArrivesWithAnExtraUnderscore()
    {
        // KWin también puede entregar la variante con guion bajo.
        DesktopEntryIndex apps;
        const DesktopEntry e =
            apps.forAppId(QStringLiteral("msedge-_%1-Default").arg(kLegacyExtId));
        QVERIFY2(e.isValid(), "la PWA tiene que caer en su lanzador");
        QCOMPARE(e.id, QStringLiteral("msedge-%1-Default").arg(kLegacyExtId));
        QCOMPARE(e.name, QStringLiteral("Reddit legacy"));
    }

    void pwaPrefersVisibleOverExactNoDisplayDuplicate()
    {
        // La ventana actual llega como "msedge-<extid>-Default", que coincide
        // exactamente con la copia oculta. Esa copia no puede ser la identidad
        // del launcher: el modelo necesita la entrada visible que el usuario
        // tiene anclada.
        DesktopEntryIndex apps;
        const DesktopEntry e =
            apps.forAppId(QStringLiteral("msedge-%1-Default").arg(kExtId));
        QVERIFY2(e.isValid(), "la PWA tiene que caer en su lanzador visible");
        QCOMPARE(e.id, QStringLiteral("msedge-_%1-Default").arg(kExtId));
        QCOMPARE(e.name, QStringLiteral("Reddit ED"));
        QVERIFY(!e.noDisplay);
    }

    void anUnknownAppIdResolvesToNothing()
    {
        // Importa tanto como los aciertos: si esto devolviera cualquier cosa,
        // dos apps distintas se agruparían en la misma fila.
        DesktopEntryIndex apps;
        QVERIFY(!apps.forAppId(QStringLiteral("no-existe-esta-app")).isValid());
    }
};

KDOCK_TEST_MAIN(TestDesktopEntry)
#include "tst_desktopentry.moc"
