// El widget "Apps Seleccionables" (tokens appsel1, appsel2…): el bloque de apps
// como sección repetible, con su propia lista de lanzadores.
//
// Lo que se prueba acá es lo que rompe callado:
//
//   - el token tiene que sobrevivir a reconcileWidgetOrder() (que descarta todo
//     token desconocido, y estos se inventan en tiempo de ejecución);
//   - el grosor del dock tiene que contar las celdas de apps aunque el bloque
//     nativo esté apagado, o la zona exclusiva le corta los íconos al widget;
//   - cada instancia escribe SU lista y ninguna otra.
//
// Los DockConfig van sobre monitores inventados (VIRT-*): así ninguna sonda arma
// una ventana y no hace falta ningún backend.

#include "desktopentry.h"
#include "dockconfig.h"
#include "dockmodel.h"
#include "sandbox.h"
#include "windowmonitor.h"

#include <QSettings>
#include <QTest>

namespace {
// Una ventana falsa: WindowMonitor no es abstracto y registerWindow() es
// público, así que el modelo se puede probar con ventanas sin compositor. Es la
// única forma de ejercitar los dos filtros ("solo anclados" y "saltear lo que
// ya dibuja otro widget"), que solo actúan sobre ventanas.
class FakeWindow : public AbstractWindow
{
public:
    explicit FakeWindow(const QString &id) { appId = id; title = id; }
    void activate() override {}
    void minimize() override {}
    void requestClose() override {}
};

// Registra la ventana en el monitor y la deja viva mientras viva el monitor.
FakeWindow *openWindow(WindowMonitor *monitor, const QString &appId)
{
    auto *w = new FakeWindow(appId);
    w->setParent(monitor);
    monitor->registerWindow(w);
    return w;
}
} // namespace

class TestAppsWidget : public QObject
{
    Q_OBJECT

private:
    static QString freshDockId(const char *name)
    {
        return QStringLiteral("VIRT-aw-%1").arg(QString::fromLatin1(name));
    }

private slots:
    void tokenShapeIsWhatTellsAnInstanceApart()
    {
        QVERIFY(DockConfig::isAppsWidgetToken(QStringLiteral("appsel1")));
        QVERIFY(DockConfig::isAppsWidgetToken(QStringLiteral("appsel12")));
        // El token pelado es la clave del catálogo de traducciones, no una
        // instancia; y el bloque nativo sigue siendo "apps".
        QVERIFY(!DockConfig::isAppsWidgetToken(QStringLiteral("appsel")));
        QVERIFY(!DockConfig::isAppsWidgetToken(QStringLiteral("appsel0")));
        QVERIFY(!DockConfig::isAppsWidgetToken(QStringLiteral("apps")));
        // Repetible: es lo que hace que reconcileWidgetOrder no lo descarte y
        // que removeSectionAt lo pueda borrar.
        QVERIFY(DockConfig::isRepeatableToken(QStringLiteral("appsel1")));
    }

    void severalInstancesCoexistAndSurviveAReload()
    {
        const QString id = freshDockId("multi");
        QString first, second;
        {
            DockConfig cfg(id);
            first = cfg.insertAppsWidget(0);
            second = cfg.insertAppsWidget(1);
            QCOMPARE(first, QStringLiteral("appsel1"));
            QCOMPARE(second, QStringLiteral("appsel2"));
            QCOMPARE(cfg.appsWidgetTokens(), QStringList({first, second}));
        }
        {
            // Releído de disco: reconcileWidgetOrder() corre en load() y tira
            // todo token que no conozca. Sin la rama de isRepeatableToken los
            // dos widgets desaparecerían acá, sin imprimir nada.
            DockConfig cfg(id);
            QCOMPARE(cfg.appsWidgetTokens(), QStringList({first, second}));
        }
    }

    void eachInstanceKeepsItsOwnAppsAndFlag()
    {
        const QString id = freshDockId("own");
        DockConfig cfg(id);
        // Los anclados del dock arrancan con el juego por defecto, así que lo
        // que importa no es que estén vacíos sino que nadie los toque.
        const QStringList dockPinned = cfg.pinned();
        const QString a = cfg.insertAppsWidget(0);
        const QString b = cfg.insertAppsWidget(1);

        cfg.setWidgetApps(a, {QStringLiteral("firefox.desktop")});
        cfg.setWidgetApps(b, {QStringLiteral("gimp.desktop"), QStringLiteral("inkscape.desktop")});
        cfg.setWidgetOnlyPinned(b, false);

        QCOMPARE(cfg.widgetApps(a), QStringList({QStringLiteral("firefox.desktop")}));
        QCOMPARE(cfg.widgetApps(b).size(), 2);
        // "Ver solo anclados" viene prendido: un widget que arranca mostrando
        // todas las ventanas es indistinguible del bloque de apps.
        QVERIFY(cfg.widgetOnlyPinned(a));
        QVERIFY(!cfg.widgetOnlyPinned(b));
        // Y nada de esto tocó los anclados del dock.
        QCOMPARE(cfg.pinned(), dockPinned);
    }

    void removingAWidgetForgetsItsGroup()
    {
        // El número vuelve a estar libre, así que el próximo widget lo reusa: si
        // el grupo sobreviviera, aparecería con las apps del anterior adentro.
        const QString id = freshDockId("forget");
        DockConfig cfg(id);
        const QString token = cfg.insertAppsWidget(0);
        cfg.setWidgetApps(token, {QStringLiteral("firefox.desktop")});
        cfg.setWidgetName(token, QStringLiteral("Diseño"));

        const int at = cfg.widgetOrder().indexOf(token);
        QVERIFY(at >= 0);
        cfg.removeSectionAt(at);
        QVERIFY(cfg.appsWidgetTokens().isEmpty());
        QVERIFY(cfg.widgetApps(token).isEmpty());

        const QString again = cfg.insertAppsWidget(0);
        QCOMPARE(again, token);
        QVERIFY(cfg.widgetApps(again).isEmpty());
        QCOMPARE(cfg.widgetName(again), DockConfig::defaultWidgetLabel(again));
    }

    void theWidgetsCellsCountTowardsTheDockThickness()
    {
        // Con el bloque nativo apagado el dock se encoge a sus widgets. Un
        // appsel dibuja celdas de apps igual, y si no entran en dockThickness()
        // la zona exclusiva de layer-shell le corta los íconos.
        const QString id = freshDockId("thick");
        DockConfig cfg(id);
        cfg.setShowAppIcons(false);
        QVERIFY(!cfg.drawsAppCells());
        const int widgetsOnly = cfg.dockThickness();

        cfg.setIconLabelMode(DockConfig::LabelBelow); // celda de app = ícono + nombre
        const QString token = cfg.insertAppsWidget(0);
        QVERIFY(cfg.drawsAppCells());
        QVERIFY2(cfg.dockThickness() > widgetsOnly,
                 "el dock tiene que crecer para las celdas del widget");

        const int at = cfg.widgetOrder().indexOf(token);
        cfg.removeSectionAt(at);
        QVERIFY(!cfg.drawsAppCells());
        QCOMPARE(cfg.dockThickness(), widgetsOnly);
    }

    void theDefaultLabelCarriesTheNumber()
    {
        QCOMPARE(DockConfig::defaultWidgetLabel(QStringLiteral("appsel1")),
                 QStringLiteral("Apps Seleccionables 1"));
        QCOMPARE(DockConfig::defaultWidgetLabel(QStringLiteral("appsel3")),
                 QStringLiteral("Apps Seleccionables 3"));
    }

    void aWidgetModelReadsAndWritesItsOwnList()
    {
        // El modelo del widget y el del dock comparten el DockConfig: si el
        // widget escribiera `pinned` le reordenaría los íconos al bloque nativo.
        const QString id = freshDockId("model");
        DockConfig cfg(id);
        DesktopEntryIndex apps;
        cfg.setPinned({QStringLiteral("dock-a.desktop"), QStringLiteral("dock-b.desktop")});
        const QString token = cfg.insertAppsWidget(0);
        cfg.setWidgetApps(token, {QStringLiteral("w-a.desktop")});

        DockModel dockBlock(&cfg, &apps, nullptr, nullptr, QString());
        DockModel widget(&cfg, &apps, nullptr, nullptr, token);
        QCOMPARE(dockBlock.rowCount(), 2);
        QCOMPARE(widget.rowCount(), 1);
        QCOMPARE(widget.index(0).data(DockModel::NameRole).toString(),
                 QStringLiteral("w-a.desktop"));

        // Desanclar desde el clic derecho del widget: sale de SU lista y los
        // anclados del dock quedan intactos.
        widget.togglePinned(0);
        QCOMPARE(cfg.widgetApps(token), QStringList());
        QCOMPARE(cfg.pinned().size(), 2);
        QCOMPARE(widget.rowCount(), 0);
        QCOMPARE(dockBlock.rowCount(), 2);
    }

    void onlyPinnedDropsTheWindowsThatAreNotItsApps()
    {
        const QString id = freshDockId("strays");
        DockConfig cfg(id);
        DesktopEntryIndex apps;
        WindowMonitor monitor;
        const QString token = cfg.insertAppsWidget(0);
        cfg.setWidgetApps(token, {QStringLiteral("mine.desktop")});

        DockModel widget(&cfg, &apps, &monitor, nullptr, token);
        openWindow(&monitor, QStringLiteral("ajena"));
        // Con "ver solo anclados" (el default) la ventana ajena no entra…
        QCOMPARE(widget.rowCount(), 1);
        // …y apagándolo, sí: el widget pasa a ser un bloque de apps completo.
        cfg.setWidgetOnlyPinned(token, false);
        QCOMPARE(widget.rowCount(), 2);
    }

    void theCatchAllWidgetSkipsWhatAnotherOneAlreadyDraws()
    {
        // El caso de la feature: un widget con la lista de siempre y otro que
        // recoge todo lo demás sin repetir sus íconos.
        const QString id = freshDockId("catchall");
        DockConfig cfg(id);
        DesktopEntryIndex apps;
        WindowMonitor monitor;
        const QString fixed = cfg.insertAppsWidget(0);
        const QString catchAll = cfg.insertAppsWidget(1);
        cfg.setWidgetApps(fixed, {QStringLiteral("mine.desktop")});
        cfg.setWidgetOnlyPinned(catchAll, false);
        cfg.setWidgetExcludeOthers(catchAll, true);

        DockModel widget(&cfg, &apps, &monitor, nullptr, catchAll);
        openWindow(&monitor, QStringLiteral("mine.desktop")); // la dibuja el otro
        openWindow(&monitor, QStringLiteral("otra"));         // no la dibuja nadie
        QCOMPARE(widget.rowCount(), 1);
        QCOMPARE(widget.index(0).data(DockModel::NameRole).toString(), QStringLiteral("otra"));

        // Sin el filtro son las dos, o sea que el widget repetía el ícono.
        cfg.setWidgetExcludeOthers(catchAll, false);
        QCOMPARE(widget.rowCount(), 2);
    }

    void theFilterFollowsTheOtherWidgetsList()
    {
        // La lista de los otros es una entrada de este modelo: sin reconstruir
        // con la señal ajena, el ícono repetido se queda hasta el próximo
        // arranque.
        const QString id = freshDockId("follow");
        DockConfig cfg(id);
        DesktopEntryIndex apps;
        WindowMonitor monitor;
        const QString fixed = cfg.insertAppsWidget(0);
        const QString catchAll = cfg.insertAppsWidget(1);
        cfg.setWidgetOnlyPinned(catchAll, false);
        cfg.setWidgetExcludeOthers(catchAll, true);

        DockModel widget(&cfg, &apps, &monitor, nullptr, catchAll);
        openWindow(&monitor, QStringLiteral("compartida"));
        QCOMPARE(widget.rowCount(), 1);

        // El otro widget se queda con esa app: acá tiene que desaparecer.
        cfg.setWidgetApps(fixed, {QStringLiteral("compartida")});
        QCOMPARE(widget.rowCount(), 0);
        // Y al soltarla, vuelve.
        cfg.setWidgetApps(fixed, {});
        QCOMPARE(widget.rowCount(), 1);
    }

    void theFilterNeverHidesTheWidgetsOwnLaunchers()
    {
        // Decisión explícita: el filtro es para lo que el widget recoge solo. Un
        // lanzador que el usuario le puso a mano se dibuja siempre, aunque esté
        // también en la lista de otro widget.
        const QString id = freshDockId("ownwins");
        DockConfig cfg(id);
        DesktopEntryIndex apps;
        const QString a = cfg.insertAppsWidget(0);
        const QString b = cfg.insertAppsWidget(1);
        cfg.setWidgetApps(a, {QStringLiteral("compartida.desktop")});
        cfg.setWidgetApps(b, {QStringLiteral("compartida.desktop")});
        cfg.setWidgetOnlyPinned(b, false);
        cfg.setWidgetExcludeOthers(b, true);

        DockModel widget(&cfg, &apps, nullptr, nullptr, b);
        QCOMPARE(widget.rowCount(), 1);
        QCOMPARE(cfg.appsPinnedElsewhere(b),
                 QStringList({QStringLiteral("compartida.desktop")}));
    }

    void theMonitorFilterReachesTheOtherDocksOfTheScreen()
    {
        // Lo que distingue al filtro nuevo del viejo: la lista que lo alimenta
        // vive en OTRO DockConfig. Dos docks del mismo monitor son dos dockIds
        // que comparten el nombre de pantalla (slot 0 pelado, slot 1 con "#1").
        const QString screen = freshDockId("mon");
        DockConfig a(screen);
        DockConfig b(DockConfig::makeDockId(screen, 1));
        QCOMPARE(DockConfig::screenOfDockId(b.dockId()), screen);

        DesktopEntryIndex apps;
        WindowMonitor monitor;
        const QString fixed = a.insertAppsWidget(0);
        const QString catchAll = b.insertAppsWidget(0);
        b.setWidgetOnlyPinned(catchAll, false);
        b.setWidgetExcludeMonitor(catchAll, true);

        DockModel widget(&b, &apps, &monitor, nullptr, catchAll);
        openWindow(&monitor, QStringLiteral("compartida"));
        openWindow(&monitor, QStringLiteral("propia"));
        QCOMPARE(widget.rowCount(), 2);

        // El dock vecino se queda con esa app: acá tiene que desaparecer, y sin
        // que nadie reconstruya el modelo a mano (la señal viaja entre configs).
        a.setWidgetApps(fixed, {QStringLiteral("compartida")});
        QCOMPARE(widget.rowCount(), 1);
        QCOMPARE(widget.index(0).data(DockModel::NameRole).toString(),
                 QStringLiteral("propia"));
        // Y al soltarla, vuelve.
        a.setWidgetApps(fixed, {});
        QCOMPARE(widget.rowCount(), 2);
    }

    void theMonitorFilterIgnoresTheDocksOfOtherScreens()
    {
        // El acotamiento *es* la feature: un dock de otro monitor no entra en el
        // barrido, por más que tenga la app anclada.
        const QString here = freshDockId("here");
        DockConfig mine(here);
        DockConfig alien(freshDockId("there"));

        DesktopEntryIndex apps;
        WindowMonitor monitor;
        const QString alienToken = alien.insertAppsWidget(0);
        alien.setWidgetApps(alienToken, {QStringLiteral("compartida")});
        const QString catchAll = mine.insertAppsWidget(0);
        mine.setWidgetOnlyPinned(catchAll, false);
        mine.setWidgetExcludeMonitor(catchAll, true);

        DockModel widget(&mine, &apps, &monitor, nullptr, catchAll);
        openWindow(&monitor, QStringLiteral("compartida"));
        QCOMPARE(widget.rowCount(), 1);
        QVERIFY(!mine.appsPinnedOnMonitor(catchAll).contains(QStringLiteral("compartida")));
    }

    void theMonitorFilterIsASupersetOfTheDockLocalOne()
    {
        // Los dos widgets del mismo dock también cuentan, que es por qué el
        // diálogo apaga "otros Seleccionables" al prender este. Y las dos
        // banderas viven en claves distintas: prender una no toca la otra.
        const QString id = freshDockId("superset");
        DockConfig cfg(id);
        DesktopEntryIndex apps;
        WindowMonitor monitor;
        const QString fixed = cfg.insertAppsWidget(0);
        const QString catchAll = cfg.insertAppsWidget(1);
        cfg.setWidgetApps(fixed, {QStringLiteral("compartida")});
        cfg.setWidgetOnlyPinned(catchAll, false);
        cfg.setWidgetExcludeMonitor(catchAll, true);

        DockModel widget(&cfg, &apps, &monitor, nullptr, catchAll);
        openWindow(&monitor, QStringLiteral("compartida"));
        openWindow(&monitor, QStringLiteral("otra"));
        QCOMPARE(widget.rowCount(), 1);
        // Sin haber tocado el filtro viejo, que sigue apagado y guardado así.
        QVERIFY(!cfg.widgetExcludeOthers(catchAll));
        QVERIFY(cfg.widgetExcludeMonitor(catchAll));

        // Y apagarlo devuelve el ícono, o sea que era él quien filtraba.
        cfg.setWidgetExcludeMonitor(catchAll, false);
        QCOMPARE(widget.rowCount(), 2);
    }

    void theMonitorFilterNeverHidesTheWidgetsOwnLaunchers()
    {
        // Misma decisión que en el filtro local: un lanzador puesto a mano se
        // dibuja siempre, esté donde esté anclado en el monitor.
        const QString screen = freshDockId("monown");
        DockConfig a(screen);
        DockConfig b(DockConfig::makeDockId(screen, 1));
        DesktopEntryIndex apps;
        const QString fixed = a.insertAppsWidget(0);
        const QString mine = b.insertAppsWidget(0);
        a.setWidgetApps(fixed, {QStringLiteral("compartida.desktop")});
        b.setWidgetApps(mine, {QStringLiteral("compartida.desktop")});
        b.setWidgetOnlyPinned(mine, false);
        b.setWidgetExcludeMonitor(mine, true);

        DockModel widget(&b, &apps, nullptr, nullptr, mine);
        QCOMPARE(widget.rowCount(), 1);
        QCOMPARE(b.appsPinnedOnMonitor(mine),
                 QStringList({QStringLiteral("compartida.desktop")}));
    }

    void aWidgetDrawsNoAppsBlockSeparators()
    {
        // Los dos separadores estáticos son del bloque de apps: sus índices no
        // significan nada en la lista del widget, y dibujarlos ahí también
        // metería dos filas de más en cada widget.
        const QString id = freshDockId("seps");
        DockConfig cfg(id);
        DesktopEntryIndex apps;
        cfg.setPinned({QStringLiteral("a.desktop"), QStringLiteral("b.desktop")});
        cfg.setSeparator1(1);
        const QString token = cfg.insertAppsWidget(0);
        cfg.setWidgetApps(token, {QStringLiteral("a.desktop"), QStringLiteral("b.desktop")});

        DockModel dockBlock(&cfg, &apps, nullptr, nullptr, QString());
        DockModel widget(&cfg, &apps, nullptr, nullptr, token);
        QCOMPARE(dockBlock.rowCount(), 3); // dos apps + el separador
        QCOMPARE(widget.rowCount(), 2);
    }

    void ungroupGivesEachWindowItsOwnIcon()
    {
        // El caso de la feature: el navegador (o konsole) está en la lista del
        // widget y tiene dos ventanas. Con "ver solo anclados" —que es el
        // default— la segunda ventana es justo la que se descartaba.
        const QString id = freshDockId("ungroup");
        DockConfig cfg(id);
        DesktopEntryIndex apps;
        WindowMonitor monitor;
        const QString token = cfg.insertAppsWidget(0);
        cfg.setWidgetApps(token, {QStringLiteral("mine.desktop")});
        QVERIFY(cfg.widgetOnlyPinned(token));
        QVERIFY(!cfg.widgetUngroupWindows(token)); // apagado por defecto

        DockModel widget(&cfg, &apps, &monitor, nullptr, token);
        openWindow(&monitor, QStringLiteral("mine.desktop"));
        openWindow(&monitor, QStringLiteral("mine.desktop"));
        // Agrupado: un solo ícono con las dos ventanas.
        QCOMPARE(widget.rowCount(), 1);
        QCOMPARE(widget.index(0).data(DockModel::WindowCountRole).toInt(), 2);

        // Y desagregando, dos íconos de una ventana cada uno, con el nombre de
        // la app en los dos (el título de la ventana va en el tooltip).
        cfg.setWidgetUngroupWindows(token, true);
        QCOMPARE(widget.rowCount(), 2);
        QCOMPARE(widget.index(0).data(DockModel::WindowCountRole).toInt(), 1);
        QCOMPARE(widget.index(1).data(DockModel::WindowCountRole).toInt(), 1);
        QCOMPARE(widget.index(1).data(DockModel::NameRole).toString(),
                 widget.index(0).data(DockModel::NameRole).toString());
    }

    void ungroupIsPerWidgetAndDoesNotLeak()
    {
        // La bandera es de la instancia: el bloque de apps del dock y el otro
        // widget siguen agrupando. Es lo que la hace distinta de la casilla
        // "Agrupar ventanas" del dock.
        const QString id = freshDockId("ungroupown");
        DockConfig cfg(id);
        DesktopEntryIndex apps;
        WindowMonitor monitor;
        cfg.setPinned({QStringLiteral("mine.desktop")});
        const QString a = cfg.insertAppsWidget(0);
        const QString b = cfg.insertAppsWidget(1);
        cfg.setWidgetApps(a, {QStringLiteral("mine.desktop")});
        cfg.setWidgetApps(b, {QStringLiteral("mine.desktop")});
        cfg.setWidgetUngroupWindows(a, true);

        DockModel dockBlock(&cfg, &apps, &monitor, nullptr, QString());
        DockModel ungrouped(&cfg, &apps, &monitor, nullptr, a);
        DockModel grouped(&cfg, &apps, &monitor, nullptr, b);
        openWindow(&monitor, QStringLiteral("mine.desktop"));
        openWindow(&monitor, QStringLiteral("mine.desktop"));
        QCOMPARE(ungrouped.rowCount(), 2);
        QCOMPARE(grouped.rowCount(), 1);
        QCOMPARE(dockBlock.rowCount(), 1);
        QVERIFY(!cfg.widgetUngroupWindows(b));
    }

    void ungroupDoesNotLetInWindowsTheWidgetWasFiltering()
    {
        // La exención es para las apps de SU lista: una ventana ajena sigue
        // afuera con "ver solo anclados", y sigue afuera con los filtros.
        const QString id = freshDockId("ungroupfilter");
        DockConfig cfg(id);
        DesktopEntryIndex apps;
        WindowMonitor monitor;
        const QString fixed = cfg.insertAppsWidget(0);
        const QString mine = cfg.insertAppsWidget(1);
        cfg.setWidgetApps(fixed, {QStringLiteral("ajena.desktop")});
        cfg.setWidgetApps(mine, {QStringLiteral("mine.desktop")});
        cfg.setWidgetUngroupWindows(mine, true);

        DockModel widget(&cfg, &apps, &monitor, nullptr, mine);
        openWindow(&monitor, QStringLiteral("mine.desktop"));
        openWindow(&monitor, QStringLiteral("mine.desktop"));
        openWindow(&monitor, QStringLiteral("ajena.desktop"));
        openWindow(&monitor, QStringLiteral("suelta"));
        QCOMPARE(widget.rowCount(), 2); // las dos suyas y nada más

        // Y como bloque de sobrantes: recoge lo demás, pero la que ya dibuja el
        // otro widget sigue afuera — desagregar no abre esa puerta.
        cfg.setWidgetOnlyPinned(mine, false);
        cfg.setWidgetExcludeOthers(mine, true);
        QCOMPARE(widget.rowCount(), 3); // + "suelta"
    }

    void theDockWideFlagAlreadyUngroups()
    {
        // Un widget solo puede desagregar de más: con la casilla del dock
        // apagada ya no hay nada que agrupar, y prender la del widget no cambia
        // nada (es lo que la UI dice grisándola).
        const QString id = freshDockId("ungroupdock");
        DockConfig cfg(id);
        DesktopEntryIndex apps;
        WindowMonitor monitor;
        cfg.setGroupWindows(false);
        const QString token = cfg.insertAppsWidget(0);
        cfg.setWidgetApps(token, {QStringLiteral("mine.desktop")});

        DockModel widget(&cfg, &apps, &monitor, nullptr, token);
        openWindow(&monitor, QStringLiteral("mine.desktop"));
        openWindow(&monitor, QStringLiteral("mine.desktop"));
        QCOMPARE(widget.rowCount(), 2);
        cfg.setWidgetUngroupWindows(token, true);
        QCOMPARE(widget.rowCount(), 2);
    }

    void everyIconOfAnUngroupedAppReadsAsPinned()
    {
        // Sin esto, el clic derecho de la ventana extra ofrece "Anclar" sobre
        // una app que YA está en la lista: anclarla dejaba una segunda fila
        // anclada detrás de una sola entrada, que sobrevive a su ventana como
        // lanzador huérfano y relanza en cada clic.
        const QString id = freshDockId("ungrouppin");
        DockConfig cfg(id);
        DesktopEntryIndex apps;
        WindowMonitor monitor;
        const QString token = cfg.insertAppsWidget(0);
        cfg.setWidgetApps(token, {QStringLiteral("mine.desktop")});
        cfg.setWidgetUngroupWindows(token, true);

        DockModel widget(&cfg, &apps, &monitor, nullptr, token);
        openWindow(&monitor, QStringLiteral("mine.desktop"));
        openWindow(&monitor, QStringLiteral("mine.desktop"));
        QCOMPARE(widget.rowCount(), 2);
        QVERIFY(widget.index(0).data(DockModel::PinnedRole).toBool());
        QVERIFY(widget.index(1).data(DockModel::PinnedRole).toBool());

        // Desanclar desde la ventana extra saca la app de la lista, y como las
        // dos filas tienen ventana ninguna desaparece.
        widget.togglePinned(1);
        QCOMPARE(cfg.widgetApps(token), QStringList());
        QCOMPARE(widget.rowCount(), 2);
        QVERIFY(!widget.index(0).data(DockModel::PinnedRole).toBool());
        QVERIFY(!widget.index(1).data(DockModel::PinnedRole).toBool());
    }
};

KDOCK_TEST_MAIN(TestAppsWidget)
#include "tst_appswidget.moc"
