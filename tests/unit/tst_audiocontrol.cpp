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
#include "sandbox.h"

#include <QElapsedTimer>
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
    void initTestCase()
    {
        // Antes de construir AudioControl: el ctor ya dispara un refresh().
        qputenv("KDOCK_FAKE_PACTL_DELAY", kSlowPactlSeconds);
    }

    void cleanupTestCase() { qunsetenv("KDOCK_FAKE_PACTL_DELAY"); }

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
};

KDOCK_TEST_MAIN(TestAudioControl)
#include "tst_audiocontrol.moc"
