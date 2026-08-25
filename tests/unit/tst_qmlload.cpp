// El QML empaquetado en el qrc compila con el Qt con el que se linkeó.
//
// Es la reja que faltaba entre qmllint y el smoke: qmllint mira los archivos del
// árbol (no lo que quedó DENTRO del binario) y el smoke necesita Xvfb, un
// compositor de mentira y 20 segundos. Esto instancia el componente de verdad,
// desde el recurso de verdad, y falla imprimiendo los errores del motor.
//
// Lo que cubre y ninguna otra cosa cubría:
//   - que el .qml esté en el qrc (un archivo nuevo que nadie agregó al
//     qt_add_resources compila igual y desaparece en runtime);
//   - que los `import` existan en ESTA instalación de Qt, que es justo lo que
//     cambia entre la máquina de desarrollo y un container de CI con otra versión;
//   - que no haya errores de compilación de QML.
//
// No cubre los errores de binding en runtime (las context properties no están
// puestas acá): eso es trabajo del smoke.

#include "sandbox.h"

#include <QQmlComponent>
#include <QQmlEngine>
#include <QTest>

class TestQmlLoad : public QObject
{
    Q_OBJECT

private:
    // Cada .qml del dock que es un componente de primer nivel. Los que solo se
    // instancian como delegates o submenús entran igual: compilar es compilar.
    static QStringList resources()
    {
        return {
            QStringLiteral("qrc:/qml/Dock.qml"),
            QStringLiteral("qrc:/qml/AppPreviewWindow.qml"),
            QStringLiteral("qrc:/qml/AppMenuPopup.qml"),
            QStringLiteral("qrc:/qml/ClipboardPopup.qml"),
            QStringLiteral("qrc:/qml/DisksPopup.qml"),
            QStringLiteral("qrc:/qml/NetworkPopup.qml"),
            QStringLiteral("qrc:/qml/ThemeListPopup.qml"),
            QStringLiteral("qrc:/qml/RelanzadorWidget.qml"),
            QStringLiteral("qrc:/qml/RelanzadorPopup.qml"),
            QStringLiteral("qrc:/qml/ScriptRunnerWidget.qml"),
            QStringLiteral("qrc:/qml/IconMenuItem.qml"),
            QStringLiteral("qrc:/qml/SubMenuDelegate.qml"),
            QStringLiteral("qrc:/qml/BackgroundColorMenu.qml"),
            QStringLiteral("qrc:/qml/ModeMenu.qml"),
            QStringLiteral("qrc:/qml/IconLabelMenu.qml"),
            QStringLiteral("qrc:/qml/WidgetLabelMenu.qml"),
        };
    }

private slots:
    void everyQmlFileIsInTheResource()
    {
        for (const QString &url : resources()) {
            const QString path = QStringLiteral(":") + QUrl(url).path();
            QVERIFY2(QFile::exists(path),
                     qPrintable(QStringLiteral("no está en el qrc: %1 "
                                               "(¿falta en qt_add_resources?)").arg(url)));
        }
    }

    void everyQmlFileCompiles()
    {
        QQmlEngine engine;
        QStringList failures;
        for (const QString &url : resources()) {
            QQmlComponent component(&engine, QUrl(url));
            // El motor puede resolver imports de forma diferida, pero los errores
            // de compilación y de import salen acá.
            if (component.isError()) {
                for (const QQmlError &e : component.errors())
                    failures << QStringLiteral("%1: %2").arg(url, e.toString());
            }
        }
        QVERIFY2(failures.isEmpty(), qPrintable(QStringLiteral("\n  ")
                                                + failures.join(QStringLiteral("\n  "))));
    }

    void theDockRootIsAnItemWithASize()
    {
        // El smoke se apoya en esto: si la raíz no dimensiona la QQuickView, la
        // ventana se queda en los 160x160 por defecto y parece que el QML no
        // cargó. Acá se ve la causa de verdad, sin Xvfb.
        QQmlEngine engine;
        QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/qml/Dock.qml")));
        QVERIFY2(!component.isError(), qPrintable(component.errorString()));
        QCOMPARE(component.status(), QQmlComponent::Ready);
    }
};

KDOCK_TEST_MAIN(TestQmlLoad)
#include "tst_qmlload.moc"
