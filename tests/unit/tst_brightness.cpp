// BrightnessControl: a qué monitor le habla la rueda del widget.
//
// Es la lógica que rompió el widget en una sesión con dock station: brightnessctl
// maneja el backlight interno y nada más, así que con el panel del portátil
// apagado (o simplemente no siendo el que se mira) la rueda atenuaba una
// pantalla invisible. Ahora el destino sale de PowerDevil y se elige en la
// solapa VideoEnergía; lo que se prueba acá es esa resolución, que falla
// callada — el widget sigue moviendo *algo*.
//
// La lista de displays entra por la costura KDOCK_TEST_DISPLAYS
// (src/screenbrightness.cpp), así que no hace falta PowerDevil y no se le toca
// el brillo a nadie: sin setters de por medio, esto es todo lectura.

#include "sandbox.h"

#include "brightnesscontrol.h"
#include "screenbrightness.h"

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

class TestBrightness : public QObject
{
    Q_OBJECT

private:
    // Un dock station como el que destapó el bug: PowerDevil ve el monitor
    // externo por DDC y NO ve el panel interno.
    static QByteArray externalOnly()
    {
        return QByteArrayLiteral(
            R"([{"name":"display23","label":"Samsung S22F350","internal":false,)"
            R"("brightness":5000,"max":10000}])");
    }

    // Portátil solo o portátil + externo, con el interno declarado por
    // PowerDevil y en segundo lugar a propósito: "auto" tiene que elegirlo
    // igual, no quedarse con el primero de la lista.
    static QByteArray internalSecond()
    {
        return QByteArrayLiteral(
            R"([{"name":"display1","label":"Samsung S22F350","internal":false,)"
            R"("brightness":10000,"max":10000},)"
            R"({"name":"display2","label":"Pantalla integrada","internal":true,)"
            R"("brightness":3000,"max":10000}])");
    }

    // Un brightnessctl de mentira que imprime una línea -m como la de verdad.
    // Devuelve el PATH que hay que poner antes de construir BrightnessControl
    // (busca el binario en el ctor).
    static QString fakeBrightnessctl(const QString &line)
    {
        static QTemporaryDir dir;
        const QString path = dir.path() + QStringLiteral("/brightnessctl");
        QFile f(path);
        f.open(QIODevice::WriteOnly | QIODevice::Truncate);
        f.write("#!/bin/sh\necho '" + line.toUtf8() + "'\n");
        f.close();
        f.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        return dir.path();
    }

private slots:
    void cleanup() { qunsetenv("KDOCK_TEST_DISPLAYS"); }

    // El bug que dejó la rueda muerta: `brightnessctl -m info` imprime
    // device,class,current,percent,max, y leer el ÚLTIMO campo daba 96000 en vez
    // de 100 — o sea m_brightness=960, con lo que decrease() calculaba
    // 960 - 0.05 y el clamp lo devolvía a 1.0. Bajar el brillo no hacía nada, y
    // desde afuera se veía como "el widget no responde".
    void parsesTheMinusMPercentage()
    {
        const QString dir = fakeBrightnessctl(
            QStringLiteral("intel_backlight,backlight,48000,50%,96000"));
        const QByteArray oldPath = qgetenv("PATH");
        qputenv("PATH", dir.toUtf8() + ":" + oldPath);

        BrightnessControl brightness;
        QSignalSpy spy(&brightness, &BrightnessControl::changed);
        QVERIFY(spy.wait(3000));
        qputenv("PATH", oldPath);

        QVERIFY(brightness.internalAvailable());
        QCOMPARE(brightness.internalBrightness(), 0.5);
        // Y el corolario: con el valor bien leído, un paso de rueda para abajo
        // cae dentro del rango en vez de quedar clampeado arriba.
        QVERIFY(brightness.brightness() - 0.05 >= BrightnessControl::MinBrightness);
    }

    // Sin PowerDevil no cambia nada: sigue siendo brightnessctl, que es lo que
    // hace el widget en una sesión sin PowerDevil (y en CI).
    void noPowerDevil()
    {
        // "[]" y no la variable sin poner: sin la costura, esto le pregunta al
        // PowerDevil de verdad de quien corra los tests y deja de ser un test.
        qputenv("KDOCK_TEST_DISPLAYS", QByteArrayLiteral("[]"));
        ScreenBrightness screens;
        BrightnessControl brightness;
        brightness.setScreens(&screens);
        QVERIFY(brightness.wheelDisplay().isEmpty());
        QVERIFY(brightness.targetLabel().isEmpty());
    }

    // El caso del bug: un solo display, externo. "auto" tiene que agarrarlo —
    // si no, la rueda se queda en el backlight que nadie ve.
    void autoPicksTheOnlyDisplay()
    {
        qputenv("KDOCK_TEST_DISPLAYS", externalOnly());
        ScreenBrightness screens;
        BrightnessControl brightness;
        brightness.setScreens(&screens);
        brightness.setWheelTarget(QString());

        QCOMPARE(brightness.wheelDisplay(), QStringLiteral("display23"));
        QCOMPARE(brightness.targetLabel(), QStringLiteral("Samsung S22F350"));
        QCOMPARE(brightness.brightness(), 0.5);
        QVERIFY(brightness.available());
    }

    void autoPrefersTheInternalOne()
    {
        qputenv("KDOCK_TEST_DISPLAYS", internalSecond());
        ScreenBrightness screens;
        BrightnessControl brightness;
        brightness.setScreens(&screens);
        brightness.setWheelTarget(QString());

        QCOMPARE(brightness.wheelDisplay(), QStringLiteral("display2"));
        QCOMPARE(brightness.brightness(), 0.3);
    }

    // Elegido a mano en la solapa. Se guarda la etiqueta y no el nombre de
    // objeto porque PowerDevil renumera esos nombres cuando un monitor duerme.
    void chosenLabelWins()
    {
        qputenv("KDOCK_TEST_DISPLAYS", internalSecond());
        ScreenBrightness screens;
        BrightnessControl brightness;
        brightness.setScreens(&screens);
        brightness.setWheelTarget(QStringLiteral("Samsung S22F350"));

        QCOMPARE(brightness.wheelDisplay(), QStringLiteral("display1"));
        QCOMPARE(brightness.brightness(), 1.0);
        QCOMPARE(brightness.wheelTarget(), QStringLiteral("Samsung S22F350"));
    }

    // El monitor elegido se desenchufó: cae a auto en vez de dejar la rueda
    // muerta, que es como se vería un destino que ya no existe.
    void missingLabelFallsBackToAuto()
    {
        qputenv("KDOCK_TEST_DISPLAYS", externalOnly());
        ScreenBrightness screens;
        BrightnessControl brightness;
        brightness.setScreens(&screens);
        brightness.setWheelTarget(QStringLiteral("Un monitor que no está"));

        QCOMPARE(brightness.wheelDisplay(), QStringLiteral("display23"));
    }

    // Pinneado al backlight interno: la rueda no toca PowerDevil ni aunque
    // haya displays.
    void internalTargetPinsToBacklight()
    {
        qputenv("KDOCK_TEST_DISPLAYS", externalOnly());
        ScreenBrightness screens;
        BrightnessControl brightness;
        brightness.setScreens(&screens);
        brightness.setWheelTarget(BrightnessControl::InternalTarget);

        QVERIFY(brightness.wheelDisplay().isEmpty());
        QVERIFY(brightness.targetLabel().isEmpty());
    }

    // La elección vive en el .conf compartido, así que sobrevive al reinicio
    // del dock: un BrightnessControl nuevo la relee.
    void targetPersists()
    {
        {
            BrightnessControl first;
            first.setWheelTarget(QStringLiteral("Samsung S22F350"));
        }
        BrightnessControl second;
        QCOMPARE(second.wheelTarget(), QStringLiteral("Samsung S22F350"));
    }
};

KDOCK_TEST_MAIN(TestBrightness)
#include "tst_brightness.moc"
