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

#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QSettings>
#include <QTabWidget>
#include <QTest>

namespace {
constexpr int kOtherDesktop = 2; // cualquiera que no sea el actual (0)

QString seedDock(const QString &screen, int slot = 0)
{
    const QString id = DockConfig::makeDockId(screen, slot);
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

    // El combo de "Dock N" (el slot) de la barra de arriba, identificado por su
    // forma: tiene exactamente kMaxDocksPerScreen entradas con data 0..N-1.
    static QComboBox *slotSelectorOf(SettingsDialog *dlg)
    {
        for (QComboBox *c : dlg->findChildren<QComboBox *>()) {
            if (c->count() != DockConfig::kMaxDocksPerScreen)
                continue;
            if (c->itemData(0).toInt() == 0 && c->itemData(1).toInt() == 1)
                return c;
        }
        return nullptr;
    }

    // El combo de monitor de la barra de arriba: es el único que lista los
    // monitores, así que se lo identifica por sus datos y no por posición.
    // Buscarlo por índice agarraría cualquiera de los combos de las solapas.
    static QComboBox *monitorSelectorOf(SettingsDialog *dlg)
    {
        for (QComboBox *c : dlg->findChildren<QComboBox *>()) {
            QStringList data;
            for (int i = 0; i < c->count(); ++i)
                data << c->itemData(i).toString();
            if (data.contains(QStringLiteral("VIRT-1")) && data.contains(QStringLiteral("VIRT-9")))
                return c;
        }
        return nullptr;
    }

    // Un casillero por su etiqueta. Nunca por índice: findChildren recorre el
    // árbol de padres, no el orden de construcción, y el diálogo tiene decenas
    // de QCheckBox.
    static QCheckBox *checkBoxNamed(SettingsDialog *dlg, const QString &text)
    {
        for (QCheckBox *cb : dlg->findChildren<QCheckBox *>()) {
            if (cb->text() == text)
                return cb;
        }
        return nullptr;
    }

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

        SettingsDialog dlg(firstCfg, &apps, nullptr, &manager, &theme);
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

        SettingsDialog dlg(cfg, &apps, nullptr, &manager, &theme);

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

    // Borrar el dock que el diálogo está editando. removeDock() hace delete de
    // su DockConfig, y el diálogo apunta ahí: sin la defensa, la próxima vez que
    // se lo toca lee un objeto liberado. La cura es que salte a otro dock.
    void removingTheEditedDockMovesTheDialogOff()
    {
        // Dos docks en el MISMO monitor: así se ve además que el salto prefiere
        // un vecino de pantalla en vez de teletransportar el diálogo a otra.
        const QString first = seedDock(QStringLiteral("VIRT-1"), 0);
        const QString second = seedDock(QStringLiteral("VIRT-1"), 1);

        Theme theme;
        DesktopEntryIndex apps;
        m_shared.theme = &theme;
        m_shared.apps = &apps;
        DockManager manager(m_shared);

        SettingsDialog dlg(manager.configFor(first), &apps, nullptr, &manager, &theme);

        QComboBox *monitor = monitorSelectorOf(&dlg);
        QComboBox *slot = slotSelectorOf(&dlg);
        QVERIFY2(monitor && slot, "no se encontraron los combos de la barra");
        QCOMPARE(monitor->currentData().toString(), QStringLiteral("VIRT-1"));
        QCOMPARE(slot->currentData().toInt(), 0);

        manager.removeDock(first);
        // La defensa es diferida a propósito (llega desde adentro del handler
        // del botón que borró el dock), así que hay que dejar correr el loop.
        QTest::qWait(50);

        // La aserción va sobre lo OBSERVABLE —a qué dock quedó apuntando el
        // diálogo— y no sobre "no se cayó": leer un DockConfig liberado no
        // segfaultea de forma confiable, así que un test escrito de esa manera
        // pasa igual sin el arreglo (comprobado: el control positivo recién
        // falló cuando la aserción pasó a mirar los combos).
        monitor = monitorSelectorOf(&dlg);
        slot = slotSelectorOf(&dlg);
        QVERIFY(monitor && slot);
        QCOMPARE(monitor->currentData().toString(), QStringLiteral("VIRT-1"));
        QCOMPARE(slot->currentData().toInt(), 1); // el vecino, no el borrado

        // Y sigue respondiendo: el config del que quedó se toca sin nada podrido.
        manager.configFor(second)->setPinned({QStringLiteral("firefox.desktop")});
        QCOMPARE(manager.configFor(second)->pinned().size(), 1);
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

        SettingsDialog dlg(firstCfg, &apps, nullptr, &manager, &theme);
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

    // La búsqueda de opciones: con menos de 4 caracteres no filtra; con 4 o más
    // deja solo las solapas cuyo texto coincide; sin ninguna coincidencia llega
    // al estado "sin resultados" (barra vacía + aviso); y limpiar el campo lo
    // devuelve todo.
    void searchFiltersTheTabs()
    {
        const QString id = seedDock(QStringLiteral("VIRT-1"));

        Theme theme;
        DesktopEntryIndex apps;
        m_shared.theme = &theme;
        m_shared.apps = &apps;
        DockManager manager(m_shared);

        SettingsDialog dlg(manager.configFor(id), &apps, nullptr, &manager, &theme);

        QLineEdit *search = dlg.findChild<QLineEdit *>(QStringLiteral("settingsSearch"));
        QTabWidget *tabs = dlg.findChild<QTabWidget *>(QStringLiteral("settingsTabs"));
        QLabel *notice = dlg.findChild<QLabel *>(QStringLiteral("settingsSearchNoResults"));
        QVERIFY2(search && tabs && notice, "no se encontró el buscador o el tab widget");
        QVERIFY(tabs->count() >= 3);

        // isVisible() de un widget exige que toda la cadena de ancestros sea
        // visible, así que sin mostrar el diálogo el aviso siempre diría "no
        // visible" aunque esté marcado para mostrarse.
        dlg.show();
        QTest::qWait(50);
        QVERIFY(dlg.isVisible());

        // El buscador vive en una fila propia arriba del tab widget, alineado a
        // la izquierda con el ancho de la columna de solapas: la geometría lo
        // confirma sin necesidad de capturas. Se compara en coordenadas del
        // diálogo (el ancestro común de ambos).
        const QPoint searchInDlg = search->mapTo(&dlg, QPoint(0, 0));
        const QPoint tabbarInDlg = tabs->tabBar()->mapTo(&dlg, QPoint(0, 0));
        QVERIFY2(searchInDlg.x() >= tabbarInDlg.x() - 2,
                 "el buscador debería quedar alineado con la columna de solapas");
        QVERIFY2(searchInDlg.y() < tabbarInDlg.y(),
                 "el buscador debería quedar ARRIBA de la primera solapa");

        // Visual check on demand: KDOCK_DUMP_DIALOG=<dir> saves the dialog at
        // the three search states so the layout can be eyeballed (the offscreen
        // platform renders QWidget::grab() fine, no X server needed).
        //
        // The value is read into a QString of our own on purpose. qgetenv()
        // returns a temporary QByteArray that dies at the end of the condition,
        // so its constData() dangled inside the body — and it is never nullptr
        // even when the variable is unset (it points at the static empty
        // string), so the branch used to run on every single test run.
        if (const QString dir = qEnvironmentVariable("KDOCK_DUMP_DIALOG"); !dir.isEmpty()) {
            dlg.grab().save(dir + QStringLiteral("/dlg_initial.png"));
            search->setText(QStringLiteral("opacity"));
            QTest::qWait(200);
            dlg.grab().save(dir + QStringLiteral("/dlg_filtered.png"));
            search->setText(QStringLiteral("zzzz-no-existe"));
            QTest::qWait(200);
            dlg.grab().save(dir + QStringLiteral("/dlg_noresult.png"));
            search->clear();
            QTest::qWait(200);
        }

        const auto allVisible = [tabs] {
            for (int i = 0; i < tabs->count(); ++i) {
                if (!tabs->isTabVisible(i))
                    return false;
            }
            return true;
        };
        QVERIFY(allVisible());

        // Menos del umbral: nada se oculta.
        search->setText(QStringLiteral("opa"));
        QTest::qWait(200); // el debounce del buscador
        QVERIFY(allVisible());

        // Con 4+: solo las solapas que contienen "opacity" quedan. El umbral es
        // el del texto del usuario, no el de la cadena buscada.
        search->setText(QStringLiteral("opacity"));
        QTest::qWait(200);
        bool sawVisible = false;
        bool sawHidden = false;
        for (int i = 0; i < tabs->count(); ++i) {
            if (tabs->isTabVisible(i))
                sawVisible = true;
            else
                sawHidden = true;
        }
        QVERIFY(sawVisible && sawHidden);

        // Sin coincidencias: estado explícito, barra vacía + aviso visible.
        search->setText(QStringLiteral("zzzz-no-existe"));
        QTest::qWait(200);
        for (int i = 0; i < tabs->count(); ++i)
            QVERIFY2(!tabs->isTabVisible(i), "con búsqueda sin resultados deberían ocultarse todas");
        QVERIFY2(notice->isVisible(), "debería aparecer el aviso de 'sin resultados'");

        // Limpiar restaura todo (el botón de limpiar del QLineEdit va por el
        // mismo setText("")).
        search->clear();
        QTest::qWait(200);
        QVERIFY(allVisible());
        QVERIFY(!notice->isVisible());
    }

    // La casilla de los botones de la vista previa: escribe la clave compartida
    // y sigue al interruptor maestro. Manejar el diálogo desde código es lo
    // único que prueba el cableado — una captura muestra el casillero igual si
    // el connect no está.
    void previewButtonsCheckboxFollowsItsMaster()
    {
        const QString id = seedDock(QStringLiteral("VIRT-1"));

        Theme theme;
        DesktopEntryIndex apps;
        m_shared.theme = &theme;
        m_shared.apps = &apps;
        DockManager manager(m_shared);

        SettingsDialog dlg(manager.configFor(id), &apps, nullptr, &manager, &theme);

        QCheckBox *master = checkBoxNamed(&dlg, QStringLiteral("Vista previa de la ventana al pasar el mouse"));
        QCheckBox *buttons = checkBoxNamed(&dlg, QStringLiteral("Minimizar, maximizar y cerrar"));
        QVERIFY2(master && buttons, "no se encontraron las casillas de la vista previa");

        // La vista previa arranca apagada, así que el casillero de los botones
        // arranca deshabilitado aunque su propia clave esté en true.
        QVERIFY(!master->isChecked());
        QVERIFY(!buttons->isEnabled());
        QVERIFY(buttons->isChecked());

        master->setChecked(true);
        QVERIFY(DockConfig::appPreview());
        QVERIFY(buttons->isEnabled());

        buttons->setChecked(false);
        QVERIFY(!DockConfig::appPreviewButtons());
        buttons->setChecked(true);
        QVERIFY(DockConfig::appPreviewButtons());
    }
};

KDOCK_TEST_MAIN(TestSettingsDialog)
#include "tst_settingsdialog.moc"
