// La vista previa de ventana al hoverear un ícono: el contrato entre
// DockModel y qml/AppPreviewWindow.qml.
//
// Lo que se prueba acá es lo que rompe callado, y son dos cosas distintas:
//
//   - previewWindow() tiene que devolver TODO lo que los botones dibujan
//     (windowIndex, minimized, maximized, maximizable) además del uuid y la
//     geometría, y tiene que elegir la ventana **activa** cuando hay varias:
//     si eligiera otra, los botones actuarían sobre una ventana que el usuario
//     no está viendo;
//   - los tres invocables por ventana tienen que pegarle a la ventana que el
//     índice nombra y ser **no-op** con índices viejos. QML se queda con el par
//     (fila, índice) mientras el preview está en pantalla, y un rebuild del
//     modelo entre medio lo deja apuntando a cualquier lado.
//
// Sin compositor: WindowMonitor no es abstracto y registerWindow() es público,
// así que el modelo se prueba con ventanas falsas que anotan lo que les piden.
// Los DockConfig van sobre monitores inventados (VIRT-*), como en tst_appswidget.

#include "desktopentry.h"
#include "dockconfig.h"
#include "dockmodel.h"
#include "sandbox.h"
#include "windowmonitor.h"

#include <QTest>

namespace {
// Una ventana falsa que además lleva la cuenta de lo que le pidieron. El uuid
// no es decorativo: previewWindow() devuelve un mapa vacío sin él (es el caso
// del backend wlroots, que no tiene ScreenShot2), así que sin uuid este test
// no probaría nada.
class FakeWindow : public AbstractWindow
{
public:
    FakeWindow(const QString &id, const QString &windowUuid)
    {
        appId = id;
        title = id;
        uuid = windowUuid;
        geometry = QRect(0, 0, 1600, 900);
    }

    void activate() override { ++activateCalls; }
    void minimize() override { ++minimizeCalls; minimized = true; }
    void unminimize() override { ++unminimizeCalls; minimized = false; }
    void requestClose() override { ++closeCalls; }
    void setMaximized(bool on) override { ++maximizeCalls; maximized = on; }

    int activateCalls = 0;
    int minimizeCalls = 0;
    int unminimizeCalls = 0;
    int closeCalls = 0;
    int maximizeCalls = 0;
};

FakeWindow *openWindow(WindowMonitor *monitor, const QString &appId, const QString &uuid)
{
    auto *w = new FakeWindow(appId, uuid);
    w->setParent(monitor);
    monitor->registerWindow(w);
    return w;
}
} // namespace

class TestAppPreview : public QObject
{
    Q_OBJECT

private:
    static QString freshDockId(const char *name)
    {
        return QStringLiteral("VIRT-ap-%1").arg(QString::fromLatin1(name));
    }

private slots:
    void noWindowNoPreview()
    {
        // El "no hay nada que mostrar" sale del mapa vacío, no de un caso
        // especial en QML: showAppPreview() corta con `!info.uuid`.
        const QString id = freshDockId("empty");
        DockConfig cfg(id);
        DesktopEntryIndex apps;
        WindowMonitor monitor;
        DockModel model(&cfg, &apps, &monitor, nullptr, QString());

        // El bloque de apps arranca con lanzadores y dos separadores, y ninguno
        // tiene ventana: ninguna fila puede dar preview.
        QVERIFY(model.rowCount() > 0);
        for (int row = 0; row < model.rowCount(); ++row)
            QVERIFY(model.previewWindow(row).isEmpty());
        // Y una fila que no existe tampoco explota.
        QVERIFY(model.previewWindow(-1).isEmpty());
        QVERIFY(model.previewWindow(model.rowCount()).isEmpty());
    }

    void previewCarriesEverythingTheButtonsDraw()
    {
        const QString id = freshDockId("fields");
        DockConfig cfg(id);
        DesktopEntryIndex apps;
        WindowMonitor monitor;
        const QString token = cfg.insertAppsWidget(0);
        cfg.setWidgetOnlyPinned(token, false);
        DockModel model(&cfg, &apps, &monitor, nullptr, token);

        FakeWindow *w = openWindow(&monitor, QStringLiteral("sola"), QStringLiteral("{u-1}"));
        w->maximizable = true;
        w->maximized = true;
        QCOMPARE(model.rowCount(), 1);

        const QVariantMap info = model.previewWindow(0);
        QCOMPARE(info.value(QStringLiteral("uuid")).toString(), QStringLiteral("{u-1}"));
        QCOMPARE(info.value(QStringLiteral("width")).toInt(), 1600);
        QCOMPARE(info.value(QStringLiteral("height")).toInt(), 900);
        QCOMPARE(info.value(QStringLiteral("windowIndex")).toInt(), 0);
        QCOMPARE(info.value(QStringLiteral("title")).toString(), QStringLiteral("sola"));
        QVERIFY(!info.value(QStringLiteral("minimized")).toBool());
        QVERIFY(info.value(QStringLiteral("maximized")).toBool());
        QVERIFY(info.value(QStringLiteral("maximizable")).toBool());
    }

    void previewFollowsTheActiveWindowOfTheGroup()
    {
        const QString id = freshDockId("group");
        DockConfig cfg(id);
        DesktopEntryIndex apps;
        WindowMonitor monitor;
        const QString token = cfg.insertAppsWidget(0);
        cfg.setWidgetOnlyPinned(token, false);
        DockModel model(&cfg, &apps, &monitor, nullptr, token);

        openWindow(&monitor, QStringLiteral("misma"), QStringLiteral("{u-1}"));
        FakeWindow *second =
            openWindow(&monitor, QStringLiteral("misma"), QStringLiteral("{u-2}"));
        QCOMPARE(model.rowCount(), 1); // agrupadas bajo un ícono

        // Sin ninguna activa gana la primera…
        QCOMPARE(model.previewWindow(0).value(QStringLiteral("windowIndex")).toInt(), 0);
        QCOMPARE(model.previewWindow(0).value(QStringLiteral("uuid")).toString(),
                 QStringLiteral("{u-1}"));

        // …y con una activa, esa. El windowIndex es lo que hace que los botones
        // le peguen a la que se está viendo y no a la otra.
        second->activated = true;
        const QVariantMap info = model.previewWindow(0);
        QCOMPARE(info.value(QStringLiteral("uuid")).toString(), QStringLiteral("{u-2}"));
        QCOMPARE(info.value(QStringLiteral("windowIndex")).toInt(), 1);
    }

    void buttonsActOnTheWindowTheIndexNames()
    {
        const QString id = freshDockId("actions");
        DockConfig cfg(id);
        DesktopEntryIndex apps;
        WindowMonitor monitor;
        const QString token = cfg.insertAppsWidget(0);
        cfg.setWidgetOnlyPinned(token, false);
        DockModel model(&cfg, &apps, &monitor, nullptr, token);

        FakeWindow *first =
            openWindow(&monitor, QStringLiteral("misma"), QStringLiteral("{u-1}"));
        FakeWindow *second =
            openWindow(&monitor, QStringLiteral("misma"), QStringLiteral("{u-2}"));
        QCOMPARE(model.rowCount(), 1);

        model.setWindowMinimized(0, 1, true);
        QCOMPARE(second->minimizeCalls, 1);
        QCOMPARE(first->minimizeCalls, 0);

        // Restaurar no es solo des-minimizar: una ventana que vuelve detrás de
        // todas las demás se lee como un botón que no hizo nada.
        model.setWindowMinimized(0, 1, false);
        QCOMPARE(second->unminimizeCalls, 1);
        QCOMPARE(second->activateCalls, 1);

        model.setWindowMaximized(0, 0, true);
        QCOMPARE(first->maximizeCalls, 1);
        QVERIFY(first->maximized);
        QCOMPARE(second->maximizeCalls, 0);

        model.closeWindow(0, 0);
        QCOMPARE(first->closeCalls, 1);
        QCOMPARE(second->closeCalls, 0);
    }

    void staleCoordinatesAreNoOpsAndNotCrashes()
    {
        // QML se queda con (fila, índice) mientras el preview está arriba: si el
        // modelo se reconstruye entre medio, el par apunta a cualquier lado y
        // esto tiene que no hacer nada.
        const QString id = freshDockId("stale");
        DockConfig cfg(id);
        DesktopEntryIndex apps;
        WindowMonitor monitor;
        const QString token = cfg.insertAppsWidget(0);
        cfg.setWidgetOnlyPinned(token, false);
        DockModel model(&cfg, &apps, &monitor, nullptr, token);

        FakeWindow *w = openWindow(&monitor, QStringLiteral("sola"), QStringLiteral("{u-1}"));
        QCOMPARE(model.rowCount(), 1);

        model.closeWindow(0, 5);
        model.closeWindow(0, -1);
        model.closeWindow(9, 0);
        model.closeWindow(-1, 0);
        model.setWindowMinimized(9, 9, true);
        model.setWindowMaximized(9, 9, true);
        model.activateWindow(9, 9);
        QCOMPARE(w->closeCalls, 0);
        QCOMPARE(w->minimizeCalls, 0);
        QCOMPARE(w->maximizeCalls, 0);
        QCOMPARE(w->activateCalls, 0);
    }

    void buttonSwitchIsSharedAndOnByDefault()
    {
        // Compartida como appPreview(), pero al revés en el default: quien
        // prendió la vista previa se encuentra los botones sin buscar un
        // segundo interruptor.
        QVERIFY(DockConfig::appPreviewButtons());
        DockConfig::setAppPreviewButtons(false);
        QVERIFY(!DockConfig::appPreviewButtons());
        DockConfig::setAppPreviewButtons(true);
        QVERIFY(DockConfig::appPreviewButtons());
    }
};

KDOCK_TEST_MAIN(TestAppPreview)
#include "tst_apppreview.moc"
