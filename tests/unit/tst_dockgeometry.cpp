// kdock::dockRectFor() — dónde queda el dock en pantalla.
//
// Es la entrada del modo *intelligent hide*: si este rectángulo está mal, el
// dock esquiva ventanas que no lo tocan o no esquiva las que sí. Se testea acá,
// sin ventana ni compositor, porque la matemática está separada a propósito
// (src/dockgeometry.h).

#include "dockgeometry.h"
#include "sandbox.h"

#include <QTest>

namespace {
constexpr int Bottom = 0, Top = 1, Left = 2, Right = 3;
constexpr int Start = 0, Center = 1, End = 2;
const QRect kScreen(0, 0, 1920, 1080);
} // namespace

class TestDockGeometry : public QObject
{
    Q_OBJECT

private slots:
    void bottomStart()
    {
        // Pegado a la esquina de inicio, con el margen de por medio, y apoyado
        // en el borde de abajo.
        const QRect r = kdock::dockRectFor(kScreen, Bottom, Start, 4, QSize(356, 52));
        QCOMPARE(r, QRect(4, 1080 - 4 - 52, 356, 52));
    }

    void bottomEnd()
    {
        // El caso que se verificó al píxel contra una captura en la sesión real
        // (2026-08-09): 1920 - 4 - 356 = 1560.
        const QRect r = kdock::dockRectFor(kScreen, Bottom, End, 4, QSize(356, 52));
        QCOMPARE(r, QRect(1560, 1024, 356, 52));
        QCOMPARE(r.right(), 1915); // 4 px de margen contra el borde derecho
    }

    void topStart()
    {
        const QRect r = kdock::dockRectFor(kScreen, Top, Start, 8, QSize(300, 60));
        QCOMPARE(r, QRect(8, 8, 300, 60));
    }

    void leftAndRight()
    {
        // En vertical el "largo" corre por el eje Y y el grosor por el X.
        const QRect l = kdock::dockRectFor(kScreen, Left, Start, 4, QSize(68, 700));
        QCOMPARE(l, QRect(4, 4, 68, 700));

        const QRect r = kdock::dockRectFor(kScreen, Right, End, 4, QSize(68, 700));
        QCOMPARE(r, QRect(1920 - 4 - 68, 1080 - 4 - 700, 68, 700));
    }

    void centeredUsesTheWholeBand()
    {
        // No es una aproximación floja: el compositor centra la superficie dentro
        // de lo que le dejan libre las OTRAS zonas exclusivas y a un cliente
        // Wayland nunca le dicen dónde quedó (medidos 92 px de corrimiento en la
        // sesión real). Devolver la franja entera hace que el dock esquive de
        // más, nunca de menos.
        const QRect r = kdock::dockRectFor(kScreen, Bottom, Center, 4, QSize(356, 52));
        QCOMPARE(r.left(), 0);
        QCOMPARE(r.width(), kScreen.width());
        QCOMPARE(r.top(), 1024);   // el grosor no cambia
        QCOMPARE(r.height(), 52);

        const QRect v = kdock::dockRectFor(kScreen, Left, Center, 4, QSize(68, 700));
        QCOMPARE(v.top(), 0);
        QCOMPARE(v.height(), kScreen.height());
        QCOMPARE(v.width(), 68);
    }

    void marginZeroTouchesTheEdge()
    {
        // Modo compacto: effectiveMargin() es 0 y el dock se apoya en el borde.
        const QRect r = kdock::dockRectFor(kScreen, Bottom, Start, 0, QSize(200, 40));
        QCOMPARE(r.bottom(), 1079);
        QCOMPARE(r.left(), 0);
    }

    void screenOffsetIsHonoured()
    {
        // Un segundo monitor no arranca en 0,0: todo el cálculo es relativo a la
        // geometría de SU pantalla.
        const QRect second(1920, 0, 1280, 1024);
        const QRect r = kdock::dockRectFor(second, Bottom, Start, 4, QSize(300, 50));
        QCOMPARE(r, QRect(1924, 1024 - 4 - 50, 300, 50));
    }

    void degenerateInputsGiveAnEmptyRect()
    {
        // Antes de que la superficie tenga tamaño (o sin pantalla) no hay nada
        // que esquivar: updateWindowsOverlap() trata el rect vacío como "no
        // solapa", que es lo correcto mientras el dock se arma.
        QVERIFY(kdock::dockRectFor(kScreen, Bottom, Start, 4, QSize(0, 52)).isEmpty());
        QVERIFY(kdock::dockRectFor(kScreen, Bottom, Start, 4, QSize(356, 0)).isEmpty());
        QVERIFY(kdock::dockRectFor(QRect(), Bottom, Start, 4, QSize(356, 52)).isEmpty());
    }
};

KDOCK_TEST_MAIN(TestDockGeometry)
#include "tst_dockgeometry.moc"
