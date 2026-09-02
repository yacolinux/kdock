// DockManager: mover y copiar un dock entre monitores, y la exclusividad de la
// bandeja.
//
// DOS REJAS QUE HAY QUE RESPETAR PARA QUE ESTO CORRA (las dos se aprendieron a
// los golpes; si las sacás, el test se cae y parece un bug del código):
//
//   1. El `Shared` va vacío, así que sync() NO puede armar ninguna ventana: un
//      DockWindow con todos los servicios en nullptr es un segmentation fault.
//      Se evita atando cada dock a un escritorio virtual que no es el actual
//      (sin VirtualDesktops en el Shared, currentDesktop() es 0, y wantedDocks()
//      deja afuera todo lo que tenga escritorios propios).
//   2. Las aserciones sobre ARCHIVOS van después de soltar el DockConfig:
//      QSettings escribe diferido, así que en la primera pasada el .conf todavía
//      no existe en disco y un QFile::exists() da false sin que nada esté mal.
//
// Los monitores son falsos: KDOCK_TEST_SCREENS (la costura de
// DockManager::connectedScreens), porque bajo Xvfb hay una sola pantalla y no
// hay forma de simular la segunda.

#include "dockconfig.h"
#include "dockmanager.h"
#include "sandbox.h"

#include <QFile>
#include <QGuiApplication>
#include <QSignalSpy>
#include <QSettings>
#include <QTest>

namespace {
constexpr int kOtherDesktop = 2; // cualquiera que no sea el actual (0)

QString confPath(const QString &dockId)
{
    return DockConfig::instanceSettingsFilePath(dockId);
}
} // namespace

class TestDockManager : public QObject
{
    Q_OBJECT

private:
    DockManager::Shared m_shared; // a propósito vacío, ver la cabecera

    // Siembra un dock habilitado en VIRT-1, atado a un escritorio que no es el
    // actual, y devuelve su id.
    //
    // El DockConfig es local a propósito: al destruirse arrastra su QSettings, y
    // eso es lo que vuelca el .conf a disco. Sembrar a través del DockManager
    // dejaría las claves en el buffer y el rename a .tmp del move no encontraría
    // el archivo (reja 2 de la cabecera).
    static QString seedDock(const QString &alias, int iconSize)
    {
        const QString id = DockConfig::makeDockId(QStringLiteral("VIRT-1"), 0);
        DockConfig::addKnownDock(id);
        DockConfig::setDockEnabled(id, true);
        {
            DockConfig cfg(id);
            cfg.setDockDesktops({kOtherDesktop});
            cfg.setAlias(alias);
            cfg.setIconSize(iconSize);
        }
        return id;
    }

private slots:
    void initTestCase()
    {
        // Dos monitores inventados. VIRT-9 es el "siguiente" de VIRT-1.
        qputenv("KDOCK_TEST_SCREENS", "VIRT-1,VIRT-9");
        // Una pantalla habilitada que no existe: si no, migrateFirstRun()
        // habilita la primaria y el manager arma un dock de verdad.
        QSettings s(DockConfig::settingsFilePath(), QSettings::IniFormat);
        s.setValue(QStringLiteral("enabledScreens"), QStringLiteral("NOEXISTE-0"));
        s.setValue(QStringLiteral("knownScreens"), QStringLiteral("NOEXISTE-0"));
        s.sync();
    }

    void copyKeepsTheOriginal()
    {
        const QString src = seedDock(QStringLiteral("Original"), 37);
        DockManager mgr(m_shared);

        const QString copy = mgr.copyDockToNextMonitor(src);
        QVERIFY2(!copy.isEmpty(), "la copia tendría que haber caído en el otro monitor");
        QCOMPARE(DockConfig::screenOfDockId(copy), QStringLiteral("VIRT-9"));

        // El original sigue en pie: es toda la diferencia con el move.
        QVERIFY(DockConfig::enabledDocks().contains(src));
        QVERIFY(DockConfig::knownDocks().contains(src));
        QVERIFY(DockConfig::enabledDocks().contains(copy));

        // Los settings viajaron, apuntados al monitor nuevo.
        QCOMPARE(mgr.configFor(copy)->iconSize(), 37);
        QCOMPARE(mgr.configFor(copy)->screenName(), QStringLiteral("VIRT-9"));

        // El alias es del original, por diseño de copySettingsTo().
        QVERIFY(mgr.configFor(copy)->alias().isEmpty());
    }

    void copyLeavesNoTmpFile()
    {
        const QString src = seedDock(QStringLiteral("SinTmp"), 40);
        DockManager mgr(m_shared);
        const QString copy = mgr.copyDockToNextMonitor(src);
        QVERIFY(!copy.isEmpty());
        QVERIFY2(!QFile::exists(confPath(src) + QStringLiteral(".tmp")),
                 "el barrido de .tmp es cosa del move; una copia no deja ninguno");
    }

    void moveDisablesTheSourceAndRenamesItsConfig()
    {
        // Regresión del camino compartido: copiar y mover son el mismo cuerpo
        // (cloneToNextMonitor) y solo cambian en esto.
        const QString src = seedDock(QStringLiteral("AMover"), 44);
        DockManager mgr(m_shared);
        // Ya está en disco porque seedDock() soltó su DockConfig (reja 2).
        QVERIFY(QFile::exists(confPath(src)));

        const QString moved = mgr.moveDockToNextMonitor(src);
        QVERIFY(!moved.isEmpty());
        QCOMPARE(DockConfig::screenOfDockId(moved), QStringLiteral("VIRT-9"));

        QVERIFY2(!DockConfig::enabledDocks().contains(src), "el origen queda deshabilitado");
        QVERIFY2(!DockConfig::knownDocks().contains(src), "y sale de la lista de la solapa");
        QVERIFY2(QFile::exists(confPath(src) + QStringLiteral(".tmp")),
                 "su .conf se renombra, no se borra: es el respaldo para recuperarlo a mano");
        QCOMPARE(mgr.configFor(moved)->iconSize(), 44);
    }

    void withASingleMonitorNeitherMoveNorCopyDoAnything()
    {
        qputenv("KDOCK_TEST_SCREENS", "VIRT-1");
        const QString src = seedDock(QStringLiteral("Solo"), 32);
        DockManager mgr(m_shared);
        QVERIFY(mgr.moveDockToNextMonitor(src).isEmpty());
        QVERIFY(mgr.copyDockToNextMonitor(src).isEmpty());
        QVERIFY2(DockConfig::enabledDocks().contains(src),
                 "y sobre todo: no se pierde el dock que había");
        qputenv("KDOCK_TEST_SCREENS", "VIRT-1,VIRT-9");
    }

    void hotplugCoalescesAndTracksArbitraryScreens()
    {
        // No dock lives on these outputs: this is deliberately a topology-only
        // test, so the empty Shared never constructs a DockWindow.  The names
        // include characters which prove no connector-name table is involved.
        QSettings s(DockConfig::settingsFilePath(), QSettings::IniFormat);
        s.setValue(QStringLiteral("enabledScreens"), QStringLiteral("NOEXISTE-0"));
        s.setValue(QStringLiteral("knownScreens"), QStringLiteral("NOEXISTE-0"));
        s.sync();
        const QString parkedDock = QStringLiteral("USB-C-Á-42");
        DockConfig::addKnownDock(parkedDock);
        DockConfig::setDockEnabled(parkedDock, true);
        {
            // Desktop 2 keeps the test dock out of the current desktop (0),
            // while wantedDocks(2) lets us assert the actual lifecycle
            // selection without building a QML window from the empty Shared.
            DockConfig cfg(parkedDock);
            cfg.setDockDesktops({kOtherDesktop});
        }
        qputenv("KDOCK_TEST_SCREENS", "DP-1");

        DockManager mgr(m_shared);
        QSignalSpy topology(&mgr, &DockManager::screenTopologyChanged);
        QVERIFY(DockConfig::knownScreens().contains(QStringLiteral("DP-1")));
        QVERIFY(!mgr.wantedDocks(kOtherDesktop).contains(parkedDock));

        // A removal is followed by primary-screen churn on real Wayland
        // sessions. Both notifications must collapse into one settled sync.
        qputenv("KDOCK_TEST_SCREENS", "");
        Q_EMIT qGuiApp->screenRemoved(nullptr);
        Q_EMIT qGuiApp->primaryScreenChanged(nullptr);
        QTRY_COMPARE(topology.count(), 1);
        QVERIFY(!mgr.connectedScreens().contains(QStringLiteral("DP-1")));

        // Any connector name that Qt reports is accepted; there is no fixed
        // monitor list in the manager.
        qputenv("KDOCK_TEST_SCREENS", "USB-C-Á-42,HDMI-A-9");
        Q_EMIT qGuiApp->screenAdded(nullptr);
        QTRY_COMPARE(topology.count(), 2);
        const QStringList screens = mgr.connectedScreens();
        QVERIFY(screens.contains(QStringLiteral("USB-C-Á-42")));
        QVERIFY(screens.contains(QStringLiteral("HDMI-A-9")));
        QVERIFY(mgr.wantedDocks(kOtherDesktop).contains(parkedDock));
        QVERIFY(DockConfig::knownScreens().contains(QStringLiteral("USB-C-Á-42")));
        QVERIFY(DockConfig::knownScreens().contains(QStringLiteral("HDMI-A-9")));

        qputenv("KDOCK_TEST_SCREENS", "VIRT-1,VIRT-9");
    }

};

KDOCK_TEST_MAIN(TestDockManager)
#include "tst_dockmanager.moc"
