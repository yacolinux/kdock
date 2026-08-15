// SettingsDialog: cambiar de dock reconstruye las solapas, y ninguna conexión
// puede sobrevivir a esa reconstrucción.
//
// EL BUG QUE ESTO CONGELA (SIGSEGV real, reportado 2026-08-15). Las solapas se
// conectan a señales del DockConfig del dock que se está editando, que es el de
// la ventana viva: sobrevive a cualquier solapa. Con `this` (el diálogo) de
// contexto, esas conexiones sobrevivían al buildTabs() que hace selectDock() —
// y sus lambdas guardan punteros crudos a los botones de la solapa, que
// buildTabs() acababa de borrar. El siguiente pinnedChanged de ese config —o
// sea, el usuario anclando una app desde el dock— entraba al lambda huérfano y
// llamaba setEnabled() sobre memoria liberada. El stack del coredump era
// togglePinned → lambda de createLayoutTab → QWidget::setEnabled.
//
// El test es el escenario tal cual: abrir el diálogo en un dock, saltar a otro
// (el selector de monitor, o el menú Dock → Nombre, que es showMonitorsTab) y
// tocar el config del PRIMERO. Sin el arreglo esto no falla: se cae.
//
// LAS DOS REJAS DE tst_dockmanager VALEN IGUAL, y por lo mismo: el `Shared` va
// casi vacío, así que sync() no puede armar ninguna ventana (un DockWindow con
// los servicios en nullptr es un segfault que parece un bug del código). Se
// evita con monitores inventados (KDOCK_TEST_SCREENS) y atando cada dock a un
// escritorio virtual que no es el actual.

#include "desktopentry.h"
#include "dockconfig.h"
#include "dockmanager.h"
#include "sandbox.h"
#include "settingsdialog.h"
#include "theme.h"

#include <QListWidget>
#include <QSettings>
#include <QTabWidget>
#include <QTest>

namespace {
constexpr int kOtherDesktop = 2; // cualquiera que no sea el actual (0)

QString seedDock(const QString &screen)
{
    const QString id = DockConfig::makeDockId(screen, 0);
    DockConfig::addKnownDock(id);
    DockConfig::setDockEnabled(id, true);
    {
        DockConfig cfg(id);
        cfg.setDockDesktops({kOtherDesktop});
    }
    return id;
}
} // namespace

class TestSettingsDialog : public QObject
{
    Q_OBJECT

private:
    DockManager::Shared m_shared;

    // La lista de anclados, buscada por la solapa que la contiene y no por
    // índice: findChildren recorre el árbol de padres, no el orden en que se
    // construyeron, y el diálogo tiene un montón de QListWidget.
    static QListWidget *pinnedListOf(SettingsDialog *dlg)
    {
        auto *tabs = dlg->findChild<QTabWidget *>();
        if (!tabs)
            return nullptr;
        for (int i = 0; i < tabs->count(); ++i) {
            if (tabs->tabText(i) != QLatin1String("Widgets"))
                continue;
            const auto lists = tabs->widget(i)->findChildren<QListWidget *>();
            return lists.isEmpty() ? nullptr : lists.first();
        }
        return nullptr;
    }

private slots:
    void initTestCase()
    {
        qputenv("KDOCK_TEST_SCREENS", "VIRT-1,VIRT-9");
        // Una pantalla habilitada que no existe: si no, migrateFirstRun()
        // habilita la primaria y el manager arma un dock de verdad.
        QSettings s(DockConfig::settingsFilePath(), QSettings::IniFormat);
        s.setValue(QStringLiteral("enabledScreens"), QStringLiteral("NOEXISTE-0"));
        s.setValue(QStringLiteral("knownScreens"), QStringLiteral("NOEXISTE-0"));
        s.sync();
    }

    // Anclar una app desde el dock después de que el diálogo saltó a otro dock.
    // Esto es literalmente el crash reportado.
    void pinningAfterADockSwitchDoesNotTouchDeletedTabs()
    {
        const QString first = seedDock(QStringLiteral("VIRT-1"));
        const QString second = seedDock(QStringLiteral("VIRT-9"));

        Theme theme;
        DesktopEntryIndex apps;
        m_shared.theme = &theme;
        m_shared.apps = &apps;
        DockManager manager(m_shared);

        DockConfig *firstCfg = manager.configFor(first);
        QVERIFY(firstCfg);

        SettingsDialog dlg(firstCfg, &apps, nullptr, nullptr, &manager, &theme);
        // El salto de dock: destruye todas las solapas y las rehace para el otro
        // dock. Todo lo que la solapa vieja haya conectado tiene que morir acá.
        dlg.showMonitorsTab(second);

        // Y ahora el dock del PRIMER config emite lo suyo, que es lo que hace
        // DockModel::togglePinned() al anclar. Antes del arreglo, acá se moría.
        firstCfg->setPinned({QStringLiteral("firefox.desktop")});
        QCOMPARE(firstCfg->pinned().size(), 1);

        // Las otras señales del mismo grupo (el bucle de createLayoutTab) y las
        // de las demás solapas, por el mismo camino.
        firstCfg->setSeparator1(1);
        firstCfg->setWidgetOrder({QStringLiteral("apps"), QStringLiteral("clock")});
        firstCfg->setMenuFavorites({QStringLiteral("dolphin.desktop")});
        firstCfg->setShowAppIcons(false);
        firstCfg->setPanelMode(true);
        QVERIFY(!firstCfg->showAppIcons());
    }

    // Mudar una conexión a `tab` no puede dejarla muda: la lista de anclados de
    // la solapa Widgets tiene que seguir reflejando un cambio hecho desde el
    // dock. Esto es lo que cubre el otro lado del arreglo — la lista dejó de
    // protegerse de su propia escritura con un disconnect() (que ya no
    // encontraría la conexión, ahora que el receptor es el tab) y pasó a una
    // bandera, así que hay que ver que la señal sigue llegando cuando el cambio
    // viene de afuera.
    void theTabsStillFollowTheirConfig()
    {
        const QString id = DockConfig::makeDockId(QStringLiteral("VIRT-1"), 0);

        Theme theme;
        DesktopEntryIndex apps;
        m_shared.theme = &theme;
        m_shared.apps = &apps;
        DockManager manager(m_shared);

        DockConfig *cfg = manager.configFor(id);
        QVERIFY(cfg);
        cfg->setPinned({});

        SettingsDialog dlg(cfg, &apps, nullptr, nullptr, &manager, &theme);

        QListWidget *pinned = pinnedListOf(&dlg);
        QVERIFY2(pinned, "no se encontro la lista de anclados de la solapa Widgets");
        const int before = pinned->count();

        cfg->setPinned({QStringLiteral("firefox.desktop"), QStringLiteral("kate.desktop")});
        QCOMPARE(pinned->count(), before + 2);

        // Y sigue viva después de un salto de dock y la vuelta, que es cuando el
        // contexto viejo dejaba una copia de más por cada ida y vuelta.
        dlg.showMonitorsTab(DockConfig::makeDockId(QStringLiteral("VIRT-9"), 0));
        dlg.showMonitorsTab(id);
        pinned = pinnedListOf(&dlg); // la solapa es otra: el puntero viejo ya no vale
        QVERIFY(pinned);
        cfg->setPinned({QStringLiteral("firefox.desktop")});
        QCOMPARE(pinned->count(), 1);
    }

    // Varios saltos seguidos: cada uno deja atrás su propia camada de solapas,
    // así que un contexto mal puesto acumula una copia por salto además de
    // colgar punteros muertos.
    void repeatedSwitchesLeaveNothingBehind()
    {
        const QString first = DockConfig::makeDockId(QStringLiteral("VIRT-1"), 0);
        const QString second = DockConfig::makeDockId(QStringLiteral("VIRT-9"), 0);

        Theme theme;
        DesktopEntryIndex apps;
        m_shared.theme = &theme;
        m_shared.apps = &apps;
        DockManager manager(m_shared);

        DockConfig *firstCfg = manager.configFor(first);
        DockConfig *secondCfg = manager.configFor(second);
        QVERIFY(firstCfg && secondCfg);

        SettingsDialog dlg(firstCfg, &apps, nullptr, nullptr, &manager, &theme);
        for (int i = 0; i < 3; ++i) {
            dlg.showMonitorsTab(second);
            dlg.showMonitorsTab(first);
        }

        // Los dos configs, porque cada salto deja huérfano al que se abandona.
        firstCfg->setPinned({QStringLiteral("konsole.desktop")});
        secondCfg->setPinned({QStringLiteral("kate.desktop")});
        QCOMPARE(firstCfg->pinned().size(), 1);
        QCOMPARE(secondCfg->pinned().size(), 1);
    }
};

KDOCK_TEST_MAIN(TestSettingsDialog)
#include "tst_settingsdialog.moc"
