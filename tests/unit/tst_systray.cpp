// SystrayHost: registrar un ícono de bandeja no puede detener la GUI.
//
// EL BUG QUE ESTO CONGELA (medido 2026-08-21, "kdock no recibe clicks 5-8 s tras
// reiniciar, y el escritorio entero tampoco"). `SystrayItem::readProperties()`
// eran una docena de `QDBusInterface::property()` **bloqueantes**, y corrían
// desde adentro del handler de `RegisterStatusNotifierItem`. El cliente que se
// registra normalmente lo hace con una llamada sincrónica y se queda adentro de
// ella hasta que el watcher le contesta — o sea que, mientras kdock le
// preguntaba, el otro no podía contestar. Los dos se esperaban hasta el timeout
// del bus. Del `dbus-monitor` de ese día:
//
//   :1.1598 -> kdock   RegisterStatusNotifierItem "/org/blueman/sni"  25.18 s
//   kdock   -> :1.1598 Properties.Get /org/blueman/sni                24.14 s
//
// 24 s de dock congelado en cada arranque, y detrás de kdock quedaban encoladas
// las registraciones de todos los demás (xdg-desktop-portal-kde esperó 24,65 s),
// que es por qué se veía como "todo el escritorio no responde".
//
// El test es ese escenario reducido a lo esencial: un cliente que toma un nombre
// en el bus y **no atiende nada** (un hilo que duerme, sin bucle de eventos), y
// la pregunta de si el bucle del host sigue latiendo mientras tanto. Se afirma
// sobre los LATIDOS y sobre lo que tarda `registerItem()` en volver: con el
// código viejo la llamada se lleva el hilo hasta el timeout del bus.
//
// Corre bajo `dbus-run-session` y no puede correr de otra forma: `SystrayHost`
// toma `org.kde.StatusNotifierWatcher`, así que en el bus de sesión de verdad le
// pelearía la bandeja al kdock del usuario (ver tests/CMakeLists.txt).

#include "systray.h"
#include "sandbox.h"

#include <QDBusAbstractAdaptor>
#include <QDBusConnection>
#include <QElapsedTimer>
#include <QTest>
#include <QThread>
#include <QTimer>

#include <memory>

namespace {
constexpr int kBeatMs = 50;
// Más largo que el watchdog de 4 s de SystrayItem sería innecesario: alcanza con
// cubrir de sobra el tiempo que el host tarda en darse por vencido con el
// cliente mudo.
constexpr int kClientSleepMs = 2500;
constexpr int kWatchMs = 2000;
constexpr int kExpectedBeats = kWatchMs / kBeatMs; // 40
// El código arreglado vuelve en microsegundos; el viejo se iba a 25 s. Cualquier
// número entre medio delata que algo volvió a hacer una llamada bloqueante.
constexpr qint64 kMaxRegisterMs = 500;
} // namespace

// El ícono de bandeja del cliente mudo. Tiene que existir de verdad: si el
// objeto no estuviera registrado, QtDBus contestaría "no such object" desde su
// propio hilo interno y el cliente no sería mudo en absoluto — el control
// positivo pasaba por eso. Con el objeto registrado, la pregunta se le encola al
// hilo dueño, que está durmiendo, y nadie contesta nunca.
class SniItemAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.StatusNotifierItem")
    Q_PROPERTY(QString IconName READ iconName)
    Q_PROPERTY(QString Status READ status)
    Q_PROPERTY(QString Title READ title)
public:
    explicit SniItemAdaptor(QObject *parent) : QDBusAbstractAdaptor(parent) {}
    QString iconName() const { return QStringLiteral("kdock-test-icon"); }
    QString status() const { return QStringLiteral("Active"); }
    QString title() const { return QStringLiteral("kdock test"); }
};

// Un cliente de bandeja que toma su nombre en el bus, exporta su ícono y después
// no atiende nada: sin bucle de eventos, todo lo que se le pregunte se queda
// esperando. Es exactamente lo que le pasa a un cliente que se registró con una
// llamada sincrónica — está adentro de ella y no puede contestar.
class MuteTrayClient : public QThread
{
    Q_OBJECT
public:
    QString serviceName;
    QAtomicInt ready{0};

protected:
    void run() override
    {
        const QString conn = QStringLiteral("kdock-test-mute-item");
        {
            QDBusConnection bus = QDBusConnection::connectToBus(QDBusConnection::SessionBus, conn);
            // El objeto se crea en ESTE hilo, que es lo que obliga a QtDBus a
            // encolarle los mensajes en vez de contestarlos por su cuenta.
            QObject item;
            new SniItemAdaptor(&item);
            bus.registerObject(QStringLiteral("/StatusNotifierItem"), &item,
                               QDBusConnection::ExportAdaptors);
            serviceName = bus.baseService();
            ready.storeRelease(1);
            // Nada de exec(): dormir es justamente el punto.
            QThread::msleep(kClientSleepMs);
            bus.unregisterObject(QStringLiteral("/StatusNotifierItem"));
        }
        QDBusConnection::disconnectFromBus(conn);
    }
};

// A client that exports a perfectly valid SNI object but never calls the
// watcher's RegisterStatusNotifierItem method.  This is the state left by
// clients such as blueman/kdeconnect after the host process is restarted.
class PassiveTrayClient
{
public:
    PassiveTrayClient()
        : m_bus(QDBusConnection::connectToBus(
            QDBusConnection::SessionBus, QStringLiteral("kdock-test-passive-sni")))
    {
    }

    ~PassiveTrayClient()
    {
        if (m_bus.isConnected()) {
            m_bus.unregisterObject(QStringLiteral("/org/blueman/sni"));
            QDBusConnection::disconnectFromBus(QStringLiteral("kdock-test-passive-sni"));
        }
    }

    bool registerObject()
    {
        m_adaptor = new SniItemAdaptor(&m_object);
        if (!m_bus.registerObject(QStringLiteral("/org/blueman/sni"), &m_object,
                                  QDBusConnection::ExportAdaptors))
            return false;
        serviceName = m_bus.baseService();
        return !serviceName.isEmpty();
    }

    QString serviceName;

private:
    QDBusConnection m_bus;
    QObject m_object;
    SniItemAdaptor *m_adaptor = nullptr;
};

class TestSystray : public QObject
{
    Q_OBJECT

private slots:
    // Un solo host para todo el caso de prueba: el nombre del bus lo toma el
    // *proceso*, y no se suelta al destruir el objeto, así que un segundo
    // SystrayHost se encontraría con el watcher del primero y sería un host
    // común — o sea, otra cosa que la que hay que probar.
    void initTestCase()
    {
        if (!QDBusConnection::sessionBus().isConnected())
            QSKIP("no hay bus de sesión (¿falta dbus-run-session?)");
        m_passive = std::make_unique<PassiveTrayClient>();
        QVERIFY(m_passive->registerObject());
        m_host = std::make_unique<SystrayHost>();
    }

    void cleanupTestCase()
    {
        m_passive.reset();
        m_host.reset();
    }

    void recoversAnItemThatNeverReRegistered()
    {
        QVERIFY(m_host->active());
        QTRY_COMPARE_WITH_TIMEOUT(m_host->items().size(), 1, 5000);
        QCOMPARE(m_host->items().first()->service, m_passive->serviceName);
    }

    // Sin nadie más en el bus, el host se convierte en el watcher — que es el
    // caso de esta sesión (LXQt) y el único en el que kdock atiende
    // RegisterStatusNotifierItem.
    void becomesTheWatcherOnAnEmptyBus()
    {
        QVERIFY(m_host->isWatcher());
        QVERIFY(m_host->active());
    }

    void registeringAMuteItemDoesNotBlockTheEventLoop()
    {
        SystrayHost &host = *m_host;

        // The recovery test deliberately leaves an SNI object behind without
        // a watcher registration.  Remove it before testing the single-item
        // registration path below.
        m_passive.reset();
        QTRY_COMPARE_WITH_TIMEOUT(host.items().size(), 0, 2000);

        MuteTrayClient client;
        client.start();
        QTRY_VERIFY_WITH_TIMEOUT(client.ready.loadAcquire() == 1, 5000);
        QVERIFY(!client.serviceName.isEmpty());

        int beats = 0;
        QTimer beat;
        beat.setInterval(kBeatMs);
        connect(&beat, &QTimer::timeout, this, [&beats] { ++beats; });
        beat.start();

        // El camino exacto del adaptador: esto es lo que corría adentro del
        // handler de D-Bus y se llevaba el hilo.
        QElapsedTimer timer;
        timer.start();
        host.registerItem(client.serviceName, QString());
        const qint64 registerMs = timer.elapsed();

        // El bucle tiene que seguir girando mientras el cliente sigue mudo.
        QTest::qWait(kWatchMs);
        beat.stop();

        QVERIFY2(registerMs < kMaxRegisterMs,
                 qPrintable(QStringLiteral("registerItem() tardó %1 ms: alguien volvió a "
                                           "hacer una llamada D-Bus bloqueante")
                                .arg(registerMs)));
        QVERIFY2(beats >= kExpectedBeats / 2,
                 qPrintable(QStringLiteral("solo %1 de ~%2 latidos: el bucle de eventos "
                                           "estuvo detenido")
                                .arg(beats)
                                .arg(kExpectedBeats)));
        // Y el ítem quedó en la lista aunque no haya contestado una sola
        // propiedad: un cliente mudo no puede hacer desaparecer la bandeja.
        QCOMPARE(host.items().size(), 1);

        client.wait();
    }

private:
    std::unique_ptr<SystrayHost> m_host;
    std::unique_ptr<PassiveTrayClient> m_passive;
};

KDOCK_TEST_MAIN(TestSystray)
#include "tst_systray.moc"
