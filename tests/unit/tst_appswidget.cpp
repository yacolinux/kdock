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

#include <QSettings>
#include <QTest>

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
};

KDOCK_TEST_MAIN(TestAppsWidget)
#include "tst_appswidget.moc"
