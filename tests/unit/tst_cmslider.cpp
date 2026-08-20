// El Slider del panel tiene que tener área de clic bajo CUALQUIER estilo QQC2.
//
// Congela el bug del 2026-08-20: los sliders de la sección Audio se dibujaban
// perfectos y no respondían a nada. La causa no estaba en el manejo de eventos
// sino en la geometría: `implicitHeight` de un Slider es
//
//   max(implicitBackgroundHeight, implicitHandleHeight + topPadding + bottomPadding)
//
// y los dos delegates de CmSlider se dimensionan con `width`/`height`, que **no**
// tocan `implicitWidth`/`implicitHeight`. O sea que la altura del control salía
// entera del `padding` del estilo: Basic (y Material, y Universal) ponen 6,
// Fusion no pone ninguno — y Fusion es el que queda por omisión desde que los
// imports dejaron de forzar Basic. Bajo Fusion el Slider medía 0 px de alto:
// seguía dibujándose (los hijos no se recortan) y no recibía un solo evento.
//
// Un test que corre bajo un solo estilo no lo habría visto, así que el binario
// se registra una vez por estilo (KDOCK_TEST_QQC2_STYLE): QQuickStyle::setStyle()
// solo surte efecto una vez por proceso.

#include "sandbox.h"

#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QTest>

class TestCmSlider : public QObject
{
    Q_OBJECT

private:
    // El .qml sale del árbol y no del qrc: vive en el target kdock-controlmanager,
    // que estos tests no linkean. La ruta llega por el entorno porque la de este
    // repo tiene un '#' y CMake descarta cualquier -D que lo contenga.
    static QString sliderPath()
    {
        return QString::fromLocal8Bit(qgetenv("KDOCK_REPO"))
               + QStringLiteral("/controlmanager/qml/CmSlider.qml");
    }

    // Las context properties que CmSlider espera del panel. QVariantMap alcanza:
    // QML lee `theme.foreground` de un mapa igual que de un QObject, y así el
    // test no arrastra ni CmConfig ni Theme.
    static void seedContext(QQmlEngine &engine)
    {
        engine.rootContext()->setContextProperty(
            QStringLiteral("theme"),
            QVariantMap{{QStringLiteral("foreground"), QColor(Qt::white)},
                        {QStringLiteral("highlight"), QColor(Qt::blue)}});
        engine.rootContext()->setContextProperty(
            QStringLiteral("cmConfig"),
            QVariantMap{{QStringLiteral("fontScale"), 1.0},
                        {QStringLiteral("labelBold"), false}});
        engine.rootContext()->setContextProperty(
            QStringLiteral("win"), QVariantMap{{QStringLiteral("iconSuffix"), QString()}});
    }

    static QQuickItem *innerSlider(QQuickItem *row)
    {
        const auto children = row->childItems();
        for (QQuickItem *child : children) {
            if (QString::fromLatin1(child->metaObject()->className())
                    .contains(QStringLiteral("Slider")))
                return child;
        }
        return nullptr;
    }

private slots:
    void initTestCase()
    {
        const QByteArray style = qgetenv("KDOCK_TEST_QQC2_STYLE");
        if (!style.isEmpty())
            QQuickStyle::setStyle(QString::fromLocal8Bit(style));
        QVERIFY2(QFile::exists(sliderPath()), qPrintable(sliderPath()));
    }

    void theHandleHasAHitArea()
    {
        QQmlEngine engine;
        seedContext(engine);

        QQmlComponent component(&engine, QUrl::fromLocalFile(sliderPath()));
        QVERIFY2(!component.isError(), qPrintable(component.errorString()));

        std::unique_ptr<QObject> object(component.create());
        auto *row = qobject_cast<QQuickItem *>(object.get());
        QVERIFY(row);
        row->setWidth(300);

        QQuickItem *slider = innerSlider(row);
        QVERIFY2(slider, "CmSlider dejó de contener un Slider");

        // Lo que importa no es el número exacto sino que haya algo que apretar:
        // con 0 el control es invisible para el mouse y la sección entera queda
        // muerta sin un solo error en la consola.
        QVERIFY2(slider->height() >= 10,
                 qPrintable(QStringLiteral("el Slider mide %1 px de alto con el estilo '%2': "
                                           "sin área de clic (¿un delegate sin implicitHeight?)")
                                .arg(slider->height())
                                .arg(QQuickStyle::name())));
        QVERIFY(slider->width() > 0);
    }

    void theHeightDoesNotDependOnTheStyle()
    {
        QQmlEngine engine;
        seedContext(engine);

        QQmlComponent component(&engine, QUrl::fromLocalFile(sliderPath()));
        std::unique_ptr<QObject> object(component.create());
        auto *row = qobject_cast<QQuickItem *>(object.get());
        QVERIFY(row);
        row->setWidth(300);

        QQuickItem *slider = innerSlider(row);
        QVERIFY(slider);
        // El alto del handle que CmSlider dibuja, y nada más: el padding es
        // propio (0) justamente para que ningún estilo lo corra.
        QCOMPARE(slider->height(), 14.0);
    }
};

KDOCK_TEST_MAIN(TestCmSlider)
#include "tst_cmslider.moc"
