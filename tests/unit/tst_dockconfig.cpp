// DockConfig: migraciones, orden de secciones y grosor.
//
// Los casos no son un barrido de la clase: son las cosas que ya se rompieron o
// que rompen callado. Cada uno dice cuál.

#include "dockconfig.h"
#include "sandbox.h"

#include <QSettings>
#include <QTest>

class TestDockConfig : public QObject
{
    Q_OBJECT

private:
    // Un dockId distinto por caso: DockConfig persiste, y un test que hereda lo
    // que escribió el anterior falla por una razón que no tiene que ver.
    static QString freshDockId(const char *name)
    {
        return QStringLiteral("VIRT-%1").arg(QString::fromLatin1(name));
    }

private slots:
    void hideModeDefaultsToAlwaysVisible()
    {
        DockConfig cfg(freshDockId("hm-default"));
        QCOMPARE(cfg.hideMode(), int(DockConfig::AlwaysVisible));
        QVERIFY(!cfg.autohide());
        // La zona exclusiva es la única pregunta que hace DockWindow: solo el
        // modo 0 reserva espacio.
        QVERIFY(cfg.reservesSpace());
    }

    void hideModeMigratesFromTheOldAutohideBool()
    {
        // Una config escrita antes de que existiera hideMode solo tiene
        // `autohide`. Si la migración no corre, el dock del usuario deja de
        // ocultarse de un día para el otro.
        const QString id = freshDockId("hm-migrate");
        {
            QSettings s(DockConfig::instanceSettingsFilePath(id), QSettings::IniFormat);
            s.setValue(QStringLiteral("autohide"), true);
            s.sync();
        }
        DockConfig cfg(id);
        QCOMPARE(cfg.hideMode(), int(DockConfig::AutoHide));
        QVERIFY(cfg.autohide());
        QVERIFY(!cfg.reservesSpace());
    }

    void hideModeIsPersistedAndKeepsTheOldKeyInSync()
    {
        const QString id = freshDockId("hm-persist");
        {
            DockConfig cfg(id);
            cfg.setHideMode(DockConfig::DodgeWindows);
        }
        {
            // Releída de disco: es lo que distingue "lo cambió en memoria" de
            // "lo guardó".
            DockConfig cfg(id);
            QCOMPARE(cfg.hideMode(), int(DockConfig::DodgeWindows));
            QVERIFY(!cfg.autohide());   // dodge no es auto-hide
            QVERIFY(!cfg.reservesSpace());
        }
        // La clave vieja se sigue escribiendo, para que una config que vuelva a
        // un kdock anterior se siga ocultando.
        QSettings s(DockConfig::instanceSettingsFilePath(id), QSettings::IniFormat);
        QCOMPARE(s.value(QStringLiteral("hideMode")).toInt(), int(DockConfig::DodgeWindows));
        QCOMPARE(s.value(QStringLiteral("autohide")).toBool(), false);
    }

    void autohideIsATwoStateShortcutOverTheMode()
    {
        // El widget y el menú del dock togglean `autohide`, no `hideMode`.
        DockConfig cfg(freshDockId("hm-toggle"));
        cfg.setHideMode(DockConfig::WindowsBelow);
        cfg.setAutohide(true);
        QCOMPARE(cfg.hideMode(), int(DockConfig::AutoHide));
        cfg.setAutohide(false);
        QCOMPARE(cfg.hideMode(), int(DockConfig::AlwaysVisible));
    }

    void hideModeRejectsGarbage()
    {
        // Un .conf editado a mano no puede dejar el dock en un modo inexistente.
        DockConfig cfg(freshDockId("hm-garbage"));
        cfg.setHideMode(99);
        QCOMPARE(cfg.hideMode(), int(DockConfig::AlwaysVisible));
    }

    void unknownSectionTokensAreDropped()
    {
        // reconcileWidgetOrder() descarta lo que no esté en knownWidgetTokens.
        DockConfig cfg(freshDockId("order-unknown"));
        cfg.setWidgetOrder({QStringLiteral("clock"), QStringLiteral("noexiste"),
                            QStringLiteral("volume")});
        QVERIFY(!cfg.widgetOrder().contains(QStringLiteral("noexiste")));
        QVERIFY(cfg.widgetOrder().contains(QStringLiteral("clock")));
    }

    void repeatableTokensSurviveDuplicated()
    {
        // spring/sep/gap NO están en knownWidgetTokens (esa lista se deduplica):
        // el permiso sale de isRepeatableToken(). Si se olvida, el token
        // desaparece en el próximo load sin imprimir nada.
        DockConfig cfg(freshDockId("order-repeat"));
        cfg.setWidgetOrder({QStringLiteral("clock"), QStringLiteral("spring"),
                            QStringLiteral("volume"), QStringLiteral("spring"),
                            QStringLiteral("sep"), QStringLiteral("gap")});
        const QStringList order = cfg.widgetOrder();
        QCOMPARE(order.count(QStringLiteral("spring")), 2);
        QCOMPARE(order.count(QStringLiteral("sep")), 1);
        QCOMPARE(order.count(QStringLiteral("gap")), 1);
    }

    void bareGapTokensAreNumberedOnLoad()
    {
        // Los "gap" pelados de cualquier config anterior al ancho por instancia
        // tienen que quedar numerados: sin identidad no hay dónde guardar el
        // ancho, y dos "gap" en widgetOrder son indistinguibles. La migración
        // corre en load(), así que lo que la prueba es una SEGUNDA instancia
        // sobre el mismo archivo — la primera solo siembra.
        const QString id = freshDockId("gap-migrate");
        {
            QSettings s(DockConfig::instanceSettingsFilePath(id), QSettings::IniFormat);
            s.setValue(QStringLiteral("widgetOrder"),
                       QStringList{QStringLiteral("menu"), QStringLiteral("gap"),
                                   QStringLiteral("apps"), QStringLiteral("gap")});
        }
        DockConfig cfg(id);
        const QStringList order = cfg.widgetOrder();
        QVERIFY(!order.contains(QStringLiteral("gap")));
        QCOMPARE(cfg.gapTokens(),
                 QStringList({QStringLiteral("gap1"), QStringLiteral("gap2")}));
        // Y en el orden en que estaban, que es como se numeran en la solapa.
        QCOMPARE(order.indexOf(QStringLiteral("gap1")), 1);
        QCOMPARE(order.indexOf(QStringLiteral("gap2")), 3);
    }

    void gapWidthIsPerInstanceAndRecycledOnRemoval()
    {
        DockConfig cfg(freshDockId("gap-width"));
        cfg.setWidgetOrder({QStringLiteral("clock")});
        const QString first = cfg.insertGap(0);
        const QString second = cfg.insertGap(1);
        QCOMPARE(first, QStringLiteral("gap1"));
        QCOMPARE(second, QStringLiteral("gap2"));

        cfg.setGapFixedWidth(first, true);
        cfg.setGapSize(first, 90);
        // Cada instancia con lo suyo: el ancho de una no puede pisar a la otra.
        QVERIFY(cfg.gapFixedWidth(first));
        QCOMPARE(cfg.gapSize(first), 90);
        QVERIFY(!cfg.gapFixedWidth(second));
        QCOMPARE(cfg.gapSize(second), DockConfig::kGapDefaultSize);
        // Acotado al rango, o el dock se come la pantalla con un typo.
        cfg.setGapSize(second, 99999);
        QCOMPARE(cfg.gapSize(second), DockConfig::kGapMaxSize);

        // Sacarlo devuelve el número a la circulación **sin** su ancho: heredar
        // el del anterior se leería como "el separador nuevo salió mal".
        cfg.removeSectionAt(cfg.widgetOrder().indexOf(first));
        QCOMPARE(cfg.insertGap(0), first);
        QVERIFY(!cfg.gapFixedWidth(first));
        QCOMPARE(cfg.gapSize(first), DockConfig::kGapDefaultSize);
    }

    void knownTokensAreAppendedToAnOldOrder()
    {
        // Un widget nuevo se auto-agrega al widgetOrder ya guardado: por eso no
        // hace falta migración cuando se suma uno.
        DockConfig cfg(freshDockId("order-append"));
        cfg.setWidgetOrder({QStringLiteral("clock")});
        const QStringList order = cfg.widgetOrder();
        for (const QString &token : DockConfig::knownWidgetTokens())
            QVERIFY2(order.contains(token), qPrintable(QStringLiteral("falta ") + token));
        QCOMPARE(order.first(), QStringLiteral("clock")); // respeta lo guardado
    }

    void everyKnownTokenHasADefaultLabel()
    {
        // Sin etiqueta el nombre del widget sale como el token pelado.
        for (const QString &token : DockConfig::knownWidgetTokens()) {
            const QString label = DockConfig::defaultWidgetLabel(token);
            QVERIFY2(!label.isEmpty(), qPrintable(QStringLiteral("sin label: ") + token));
        }
    }

    void thicknessFollowsIconsLabelsAndLines()
    {
        // Grosor = zona exclusiva. Si una opción que agranda la celda no entra
        // en la fórmula, el dock tapa las ventanas maximizadas.
        DockConfig cfg(freshDockId("thickness"));
        cfg.setIconSize(48);
        cfg.setIconLabelMode(0); // solo ícono
        const int bare = cfg.dockThickness();

        cfg.setIconLabelMode(1); // nombre debajo
        const int withLabel = cfg.dockThickness();
        QVERIFY2(withLabel > bare, "el nombre debajo tiene que engordar el dock");

        cfg.setLabelLines(2);
        QVERIFY2(cfg.dockThickness() > withLabel,
                 "dos renglones reservan dos alturas de línea, no una");

        // Sin íconos de apps el dock es una barra de solo widgets.
        cfg.setLabelLines(1);
        cfg.setShowAppIcons(false);
        QVERIFY(cfg.dockThickness() > 0);
    }

    void qsettingsMapsGeneralToTheRootLevel()
    {
        // La trampa que costó meses: QSettings mapea la sección [General] de un
        // INI al nivel raíz, así que value("General/Clave") no lee NADA —sin
        // error ni advertencia— aunque el archivo tenga la clave ahí. Es la
        // razón por la que el picker de esquemas mostró "(sin definir)" para
        // siempre. Se deja acá para que nadie "arregle" un lector volviendo a
        // la forma con prefijo.
        const QString path = kdocktest::sandboxDir().path() + QStringLiteral("/ini-trap.conf");
        {
            QSettings s(path, QSettings::IniFormat);
            s.setValue(QStringLiteral("ColorScheme"), QStringLiteral("BreezeDark"));
            s.setValue(QStringLiteral("Icons/Theme"), QStringLiteral("breeze"));
            s.sync();
        }
        QSettings s(path, QSettings::IniFormat);
        QCOMPARE(s.value(QStringLiteral("ColorScheme")).toString(), QStringLiteral("BreezeDark"));
        QVERIFY2(!s.value(QStringLiteral("General/ColorScheme")).isValid(),
                 "value(\"General/...\") no lee la seccion [General]: usa la clave pelada");
        // Las demás secciones sí se direccionan normal.
        QCOMPARE(s.value(QStringLiteral("Icons/Theme")).toString(), QStringLiteral("breeze"));
    }
};

KDOCK_TEST_MAIN(TestDockConfig)
#include "tst_dockconfig.moc"
