// AudioControl: consultar el estado del audio no puede detener la GUI.
//
// EL BUG QUE ESTO CONGELA (reportado 2026-08-16, "kdock colgado luego de
// manipular el volumen"). refresh() eran CINCO llamadas a pactl seguidas, cada
// una con un waitForFinished(800) sobre el hilo de la GUI: hasta cuatro
// segundos de dock congelado por refresco. Con el servidor de audio respondiendo
// rápido no se notaba nunca; el día que se enchufó un monitor cuyo sink HDMI
// entra al grafo, pactl se puso lento **y** `pactl subscribe` largó una
// avalancha de eventos, así que el debounce de 150 ms compraba otro bloqueo de
// cuatro segundos apenas terminaba el anterior. El dock parecía colgado hasta
// que el grafo se calmaba.
//
// El test es ese escenario: un pactl deliberadamente lento
// (KDOCK_FAKE_PACTL_DELAY, ver tests/lib/fakebin.sh) y la pregunta de si el
// bucle de eventos sigue latiendo. Se afirma sobre los LATIDOS y no sobre lo que
// tarda refresh() en volver, porque medir la llamada sola no distingue "no
// bloquea" de "bloquea en otro lado del ciclo".

#include "audiocontrol.h"
#include "volumecontrol.h"
#include <QFile>
#include <QTemporaryDir>
#include "dockconfig.h"
#include "sandbox.h"

#include <QElapsedTimer>
#include <QSettings>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>

namespace {
// Más largo que el watchdog de 800 ms de AudioControl, para que cada consulta
// llegue hasta el final del camino de error.
constexpr const char *kSlowPactlSeconds = "2";
constexpr int kBeatMs = 50;
constexpr int kWatchMs = 2000;
constexpr int kExpectedBeats = kWatchMs / kBeatMs; // 40
} // namespace

class TestAudioControl : public QObject
{
    Q_OBJECT

private slots:
    void volumeQueriesAreBoundedAndRecoverAfterTimeout()
    {
        QTemporaryDir bin;
        QVERIFY(bin.isValid());
        QFile command(bin.filePath(QStringLiteral("wpctl")));
        QVERIFY(command.open(QIODevice::WriteOnly));
        command.write("#!/bin/sh\nexec /bin/sleep 30\n");
        command.close();
        QVERIFY(command.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        const QByteArray oldPath = qgetenv("PATH");
        qputenv("PATH", bin.path().toUtf8() + ':' + oldPath);
        VolumeControl volume;
        qputenv("PATH", oldPath);
        for (int i = 0; i < 40; ++i)
            volume.refresh();
        // One running query, no matter how many hotplug notifications arrive.
        QCOMPARE(volume.findChildren<QProcess *>().size(), 1);
        QTest::qWait(200);
        QVERIFY(command.open(QIODevice::WriteOnly | QIODevice::Truncate));
        command.write("#!/bin/sh\necho 'Volume: 0.42 [MUTED]'\n");
        command.close();
        // The sleeping child must time out, then the queued refresh must succeed.
        QTRY_VERIFY_WITH_TIMEOUT(volume.available(), 3000);
        QCOMPARE(volume.volume(), 0.42);
        QVERIFY(volume.muted());
        QVERIFY(command.remove());
        volume.refresh();
        QTRY_VERIFY_WITH_TIMEOUT(!volume.available(), 1500);
        QVERIFY(command.open(QIODevice::WriteOnly));
        command.write("#!/bin/sh\necho 'Volume: 0.60'\n");
        command.close();
        QVERIFY(command.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        volume.refresh();
        QTRY_VERIFY_WITH_TIMEOUT(volume.available(), 1500);
        QCOMPARE(volume.volume(), 0.60);
    }

    void initTestCase()
    {
        // Antes de construir AudioControl: el ctor ya dispara un refresh().
        qputenv("KDOCK_FAKE_PACTL_DELAY", kSlowPactlSeconds);
    }

    void cleanupTestCase() { qunsetenv("KDOCK_FAKE_PACTL_DELAY"); }

    void maxVolumeSettingLivesInTheArchiveableSharedConfig()
    {
        static const auto key = QStringLiteral("audio/maxVolume");
        QSettings shared(DockConfig::settingsFilePath(), QSettings::IniFormat);
        shared.remove(key);
        shared.sync();

        AudioControl audio;
        audio.setMaxVolume(true);
        shared.sync();
        QVERIFY(shared.value(key).toBool());

        audio.setMaxVolume(false);
        shared.sync();
        QVERIFY(!shared.value(key).toBool());
    }

    // Con pactl lento, ni el constructor ni refresh() pueden quedarse con el
    // hilo. El control positivo (volver a la versión con waitForFinished) da
    // ~4000 ms acá y 0 latidos abajo.
    void refreshDoesNotBlockTheCaller()
    {
        QElapsedTimer t;
        t.start();
        AudioControl audio;
        const qint64 ctorMs = t.elapsed();

        t.restart();
        audio.refresh();
        const qint64 refreshMs = t.elapsed();

        QVERIFY2(ctorMs < 500,
                 qPrintable(QStringLiteral("el ctor bloqueó %1 ms con pactl lento").arg(ctorMs)));
        QVERIFY2(refreshMs < 500,
                 qPrintable(QStringLiteral("refresh() bloqueó %1 ms").arg(refreshMs)));
    }

    // Lo que de verdad importa: que el bucle de eventos —o sea, el dock— siga
    // atendiendo mientras las consultas están en vuelo.
    void theEventLoopKeepsRunningDuringARefreshStorm()
    {
        AudioControl audio;

        int beats = 0;
        QTimer beat;
        beat.setInterval(kBeatMs);
        connect(&beat, &QTimer::timeout, this, [&beats] { ++beats; });
        beat.start();

        // La avalancha de `pactl subscribe`: muchos refrescos encima del
        // anterior, que es justo lo que apilaba bloqueos.
        for (int i = 0; i < 20; ++i)
            QTimer::singleShot(i * 10, &audio, [&audio] { audio.refresh(); });

        QTest::qWait(kWatchMs);

        // Holgura amplia a propósito: la aserción distingue "late" de "no late",
        // no mide precisión de timers en una máquina cargada.
        QVERIFY2(beats > kExpectedBeats / 2,
                 qPrintable(QStringLiteral("solo %1 de ~%2 latidos: el bucle estuvo bloqueado")
                                .arg(beats)
                                .arg(kExpectedBeats)));
    }

    // Y los refrescos se fusionan en vez de apilarse: uno en vuelo y uno
    // recordado. Sin esto, veinte eventos serían cien procesos pactl.
    void refreshesCoalesceInsteadOfPilingUp()
    {
        AudioControl audio;

        int changes = 0;
        connect(&audio, &AudioControl::changed, this, [&changes] { ++changes; });

        for (int i = 0; i < 20; ++i)
            audio.refresh();

        QTest::qWait(kWatchMs);

        QVERIFY2(changes <= 3,
                 qPrintable(QStringLiteral("%1 changed() para 20 refrescos: no se fusionaron")
                                .arg(changes)));
    }

    // El parser de perfiles: una tarjeta con dos perfiles disponibles (analógico
    // y HDMI) y uno sin conectar. El punto entero de la feature es que el HDMI
    // aparezca AUNQUE su sink todavía no exista — existe recién cuando el perfil
    // se activa —, así que la disponibilidad sale del puerto, no de la lista de
    // sinks. El fake de tests/lib/fakebin.sh emite esta tarjeta para `list cards`.
    void cardsAndProfilesAreParsedFromPactl()
    {
        // Los tests de arriba corren con pactl deliberadamente lento; acá solo
        // se necesita el parser, así que la demora se cae para este slot.
        qunsetenv("KDOCK_FAKE_PACTL_DELAY");

        AudioControl audio;
        QSignalSpy spy(&audio, &AudioControl::changed);
        QTRY_VERIFY_WITH_TIMEOUT(!audio.cards().isEmpty(), 5000);

        const auto cards = audio.cards();
        QCOMPARE(cards.size(), 1);
        const AudioControl::Card &card = cards.first();
        QCOMPARE(card.index, 1);
        QCOMPARE(card.name, QStringLiteral("alsa_card.pci-0000_00_1f.3"));
        QCOMPARE(card.description, QStringLiteral("Test Card"));
        QCOMPARE(card.activeProfile, QStringLiteral("output:analog-stereo+input:analog-stereo"));

        QCOMPARE(card.profiles.size(), 3);
        QCOMPARE(card.profiles.at(0).name,
                 QStringLiteral("output:analog-stereo+input:analog-stereo"));
        QVERIFY(card.profiles.at(0).available);
        QCOMPARE(card.profiles.at(1).name,
                 QStringLiteral("output:hdmi-stereo+input:analog-stereo"));
        QVERIFY(card.profiles.at(1).available); // el puerto HDMI está conectado
        QCOMPARE(card.profiles.at(2).name, QStringLiteral("output:hdmi-stereo-extra1"));
        QVERIFY(!card.profiles.at(2).available); // el puerto HDMI 2 no lo está
        // La sección Ports: del listado no puede colarse como perfil.
        for (const AudioControl::Profile &p : card.profiles)
            QVERIFY2(!p.name.startsWith(QLatin1String("analog-input-mic")),
                     qPrintable(QStringLiteral("el puerto %1 se parseó como perfil").arg(p.name)));
    }
};

KDOCK_TEST_MAIN(TestAudioControl)
#include "tst_audiocontrol.moc"
