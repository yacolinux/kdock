// KeyboardControl: la distribución de teclado de una sesión Wayland.
//
// EL BUG QUE ESTO CONGELA (reportado 2026-08-22). Bajo Wayland el mapa de
// teclas lo arma el compositor. En esta sesión el compositor es KWin, que lo
// saca del grupo [Layout] de kxkbrc e ignora tanto /etc/default/keyboard
// (XKBLAYOUT=latam) como lo que aplique lxqt-config-input, que usa setxkbmap y
// eso es X11. Resultado: una máquina cuyo único archivo que decía "es" era
// kxkbrc escribía como teclado de España y no había forma de moverla desde el
// centro de control de LXQt.
//
// LO QUE ESTE TEST CUBRE Y NO ES OBVIO, en orden de cuánto costó descubrirlo:
//
//   1. **La notificación de D-Bus tiene que marshalarse bien.** Lo que hace que
//      KWin recompile el mapa NO es org.kde.KWin.reconfigure (ese método
//      reparsea kwinrc y deja el teclado como estaba, verificado a mano): es la
//      señal org.kde.kconfig.notify.ConfigChanged sobre /kxkbrc, que es a la
//      que KConfigWatcher está suscripto. Su argumento es a{saay}, y registrar
//      sólo el QHash deja QByteArrayList sin registrar: QtDBus emite la señal
//      con firma vacía y del otro lado eso es idéntico a no haberla mandado
//      nunca. El archivo, mientras tanto, queda perfecto — o sea que un test
//      que sólo mire kxkbrc da verde sobre la mitad rota de la feature.
//   2. **Qué significa "vacío" para cada clave**, que no es lo mismo en las
//      cuatro: VariantList vacío es un valor (lo que limpia una variante
//      vieja), mientras que Model y Options vacíos tienen que *borrar* la
//      clave, porque un Options en blanco no es lo mismo que no tener Options.
//   3. **Inerte mientras está apagado.** kxkbrc vive en XDG_CONFIG_HOME y la
//      notificación va al bus de sesión: ningún sandbox de XDG_DATA_HOME frena
//      esto, así que construir la clase no puede cambiarle el teclado a nadie.
//
// EL CATÁLOGO SE LEE DE UN FIXTURE (KDOCK_TEST_XKB_RULES) y no de
// /usr/share/X11/xkb: así el parser se prueba en CI, donde no hay xkb-data, y
// las aserciones no dependen de la versión que tenga instalada el que corre.
//
// VA ENVUELTO EN dbus-run-session, como tst_systray: sin bus propio la señal
// saldría al bus real del desarrollador (donde le hablaría a su KWin) y, peor,
// no habría forma de recibirla para comprobar el punto 1.

#include "desktopentry.h"
#include "dockconfig.h"
#include "dockmanager.h"
#include "keyboardcontrol.h"
#include "qtcompat.h"
#include "sandbox.h"
#include "settingsdialog.h"
#include "theme.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QFileInfo>
#include <QGroupBox>
#include <QSettings>
#include <QStandardPaths>
#include <QTest>

using KConfigNotifyMap = QHash<QString, QByteArrayList>;
Q_DECLARE_METATYPE(KConfigNotifyMap)

namespace {

QString callsLog()
{
    // El fakebin está primero en el PATH que pone set_tests_properties; su
    // directorio es el que contiene la herramienta falsa.
    const QString tool = QStandardPaths::findExecutable(QStringLiteral("kwriteconfig6"));
    return QFileInfo(tool).absolutePath() + QStringLiteral("/calls.log");
}

QString readCalls()
{
    QFile f(callsLog());
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    return QString::fromUtf8(f.readAll());
}

void clearCalls()
{
    QFile f(callsLog());
    f.open(QIODevice::WriteOnly | QIODevice::Truncate);
}

// Escribe kxkbrc a mano, que es lo que el kwriteconfig6 falso no hace: sirve
// para poner el archivo en el estado "ya está bien" y ver que apply() no
// escribe de nuevo.
void seedKxkbrc(const QVariantMap &values)
{
    const QString path = KeyboardControl::kxkbrcPath();
    QFile::remove(path);
    if (values.isEmpty())
        return;
    QSettings s(path, QSettings::IniFormat);
    for (auto it = values.cbegin(); it != values.cend(); ++it)
        s.setValue(QStringLiteral("Layout/") + it.key(), it.value());
    s.sync();
}

// Recibe la señal que KWin escucha.
//
// **Toma el QDBusMessage crudo y no un KConfigNotifyMap ya demarshalado**, y
// eso no es un detalle de estilo: el registro de metatipos de QtDBus es global
// al proceso, así que un spy que llame a qDBusRegisterMetaType<QByteArrayList>()
// para poder recibir el tipo cómodo **le registra el tipo también al código de
// producción** y tapa exactamente el bug que este test existe para congelar.
// Pasó tal cual: con el registro adentro del spy, sacar la línea de
// keyboardcontrol.cpp dejaba el test en verde.
//
// Con el mensaje crudo la aserción es sobre la firma que viajó por el cable
// ("a{saay}"), que es lo único que el receptor de verdad ve.
class NotifySpy : public QObject
{
    Q_OBJECT
public:
    NotifySpy()
    {
        QDBusConnection::sessionBus().connect(
            QString(), QStringLiteral("/kxkbrc"), QStringLiteral("org.kde.kconfig.notify"),
            QStringLiteral("ConfigChanged"), this, SLOT(onChanged(QDBusMessage)));
    }

    struct Notification
    {
        QString signature;
        QHash<QString, QByteArrayList> groups;
    };
    QList<Notification> received;

public slots:
    void onChanged(const QDBusMessage &msg)
    {
        Notification n;
        n.signature = msg.signature();
        if (msg.arguments().isEmpty()) {
            received.append(n);
            return;
        }
        // Desmarshalado a mano, sin registrar nada, por la razón de arriba.
        const QDBusArgument arg = msg.arguments().constFirst().value<QDBusArgument>();
        if (arg.currentType() == QDBusArgument::MapType) {
            arg.beginMap();
            while (!arg.atEnd()) {
                QString group;
                QByteArrayList keys;
                arg.beginMapEntry();
                arg >> group;
                arg.beginArray();
                while (!arg.atEnd()) {
                    QByteArray key;
                    arg >> key;
                    keys << key;
                }
                arg.endArray();
                arg.endMapEntry();
                n.groups.insert(group, keys);
            }
            arg.endMap();
        }
        received.append(n);
    }
};

} // namespace

class TestKeyboard : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        const QByteArray fixtures = qgetenv("KDOCK_FIXTURES");
        if (fixtures.isEmpty())
            QSKIP("KDOCK_FIXTURES sin definir");
        qputenv("KDOCK_TEST_XKB_RULES", fixtures + "/xkb/rules.lst");
        if (!QDBusConnection::sessionBus().isConnected())
            QSKIP("sin bus de sesión: correr el test bajo dbus-run-session");
    }

    void init()
    {
        // Cada prueba arranca con el interruptor apagado y sin ajustes: son
        // estáticos (viven en el kdock.conf compartido), así que lo que dejó la
        // anterior es lo que va a leer la siguiente.
        QSettings s(DockConfig::settingsFilePath(), QSettings::IniFormat);
        s.remove(QStringLiteral("Keyboard"));
        s.sync();
        seedKxkbrc({});
        clearCalls();
    }

    // ---- el catálogo ------------------------------------------------------

    void theRulesFileIsParsedIntoLayoutsModelsAndVariants()
    {
        const auto layouts = KeyboardControl::availableLayouts();
        QStringList ids;
        for (const auto &l : layouts)
            ids << l.id;
        // "latam" y "ara" están DESPUÉS de la línea de asignación del fixture:
        // si el parser la tomó por encabezado de sección, acá faltan.
        QCOMPARE(ids, QStringList({QStringLiteral("us"), QStringLiteral("es"),
                                   QStringLiteral("latam"), QStringLiteral("ara")}));
        QCOMPARE(layouts.at(2).name, QStringLiteral("Spanish (Latin American)"));

        QStringList models;
        for (const auto &m : KeyboardControl::availableModels())
            models << m.id;
        QCOMPARE(models, QStringList({QStringLiteral("pc105"), QStringLiteral("pc104"),
                                      QStringLiteral("applealu_jis")}));
    }

    // El id de una variante es ambiguo ("nodeadkeys" existe para media docena
    // de distribuciones): lo que dice de cuál es, es el prefijo "<layout>:" de
    // su descripción, y ese prefijo no tiene que quedar en el nombre mostrado.
    void variantsAreFilteredByLayoutAndLoseTheirPrefix()
    {
        const auto latam = KeyboardControl::availableVariants(QStringLiteral("latam"));
        QCOMPARE(latam.size(), 2);
        QCOMPARE(latam.at(0).id, QStringLiteral("nodeadkeys"));
        QCOMPARE(latam.at(0).name, QStringLiteral("Spanish (Latin American, no dead keys)"));
        QCOMPARE(latam.at(1).id, QStringLiteral("deadtilde"));

        const auto es = KeyboardControl::availableVariants(QStringLiteral("es"));
        QCOMPARE(es.size(), 2);
        QCOMPARE(es.at(0).name, QStringLiteral("Spanish (no dead keys)"));

        // Sin distribución no hay variantes que ofrecer, y desde luego no las
        // de todas juntas.
        QVERIFY(KeyboardControl::availableVariants(QString()).isEmpty());
    }

    // ---- inerte mientras está apagado -------------------------------------

    void nothingIsWrittenWhileTheFeatureIsOff()
    {
        NotifySpy spy;
        KeyboardControl kb;
        kb.setLayout(QStringLiteral("latam"));
        kb.setModel(QStringLiteral("pc105"));
        kb.applyNow(); // force: ni así

        QTest::qWait(300);
        QVERIFY2(readCalls().isEmpty(), qPrintable(readCalls()));
        QVERIFY(spy.received.isEmpty());
        QVERIFY(!QFile::exists(KeyboardControl::kxkbrcPath()));
    }

    // ---- qué se escribe ---------------------------------------------------

    void enablingWritesTheFourKeysAndNotifiesOnce()
    {
        NotifySpy spy;
        KeyboardControl kb;
        kb.setLayout(QStringLiteral("latam"));
        kb.setVariant(QStringLiteral("nodeadkeys"));
        kb.setModel(QStringLiteral("pc105"));
        kb.setOptions(QStringLiteral("grp:alt_shift_toggle"));
        clearCalls();
        spy.received.clear();

        kb.setEnabled(true);
        QTest::qWait(300);

        const QString calls = readCalls();
        QVERIFY2(calls.contains(QLatin1String("--key LayoutList latam")), qPrintable(calls));
        QVERIFY2(calls.contains(QLatin1String("--key VariantList nodeadkeys")), qPrintable(calls));
        QVERIFY2(calls.contains(QLatin1String("--key Model pc105")), qPrintable(calls));
        QVERIFY2(calls.contains(QLatin1String("--key Options grp:alt_shift_toggle")),
                 qPrintable(calls));
        QVERIFY2(calls.contains(QLatin1String("--file kxkbrc")), qPrintable(calls));

        // Una sola notificación para el lote: KWin recompila el mapa una vez y
        // no cuatro. Y con las cuatro claves adentro, que es lo que la vuelve
        // verificable.
        QCOMPARE(spy.received.size(), 1);
        // La firma del cable. Con QByteArrayList sin registrar QtDBus manda la
        // señal con firma vacía y el archivo igual queda perfecto: es la mitad
        // de la feature que sólo se ve desde acá.
        QCOMPARE(spy.received.constFirst().signature, QStringLiteral("a{saay}"));
        const QByteArrayList keys =
            spy.received.constFirst().groups.value(QStringLiteral("Layout"));
        QCOMPARE(keys.size(), 4);
        QVERIFY(keys.contains(QByteArrayLiteral("LayoutList")));
        QVERIFY(keys.contains(QByteArrayLiteral("Options")));
    }

    // Punto 2: las cuatro claves no tratan "vacío" igual.
    void anEmptyVariantIsWrittenButAnEmptyModelIsDeleted()
    {
        // El archivo trae de antes una variante, un modelo y unas opciones.
        seedKxkbrc({{QStringLiteral("LayoutList"), QStringLiteral("es")},
                    {QStringLiteral("VariantList"), QStringLiteral("winkeys")},
                    {QStringLiteral("Model"), QStringLiteral("pc104")},
                    {QStringLiteral("Options"), QStringLiteral("grp:alt_shift_toggle")}});

        KeyboardControl kb;
        kb.setLayout(QStringLiteral("latam"));
        kb.setVariant(QString());
        kb.setModel(QString());
        kb.setOptions(QString());
        clearCalls();
        kb.setEnabled(true);
        QTest::qWait(300);

        const QString calls = readCalls();
        // Vacío como valor: es lo que limpia la variante vieja. Sin esto el
        // teclado seguiría en "winkeys" para siempre.
        QVERIFY2(calls.contains(QLatin1String("--key VariantList \n"))
                     || calls.contains(QLatin1String("--key VariantList ")),
                 qPrintable(calls));
        QVERIFY2(!calls.contains(QLatin1String("--key VariantList --delete")), qPrintable(calls));
        // Vacío como ausencia: la clave se va, y ahí xkb usa su valor de fábrica.
        QVERIFY2(calls.contains(QLatin1String("--key Model --delete")), qPrintable(calls));
        QVERIFY2(calls.contains(QLatin1String("--key Options --delete")), qPrintable(calls));
    }

    // Y el reflejo: una clave que ya está ausente no se borra de nuevo (sería
    // una recompilación del mapa por nada, en cada arranque).
    void anAbsentKeyIsNotDeletedAgain()
    {
        seedKxkbrc({{QStringLiteral("LayoutList"), QStringLiteral("latam")},
                    {QStringLiteral("VariantList"), QString()}});

        NotifySpy spy;
        KeyboardControl kb;
        kb.setLayout(QStringLiteral("latam"));
        kb.setVariant(QString());
        clearCalls();
        spy.received.clear();
        kb.setEnabled(true);
        QTest::qWait(300);

        const QString calls = readCalls();
        QVERIFY2(!calls.contains(QLatin1String("--delete")), qPrintable(calls));
        QVERIFY2(calls.isEmpty(), qPrintable(calls));
    }

    // Punto 3 del arranque: aplicar sobre un archivo que ya dice lo que
    // queremos no escribe ni notifica. Es lo que hace que main() pueda llamar
    // apply() en cada arranque sin costo ni parpadeo del mapa de teclas.
    void applyingOverAnAlreadyCorrectFileIsANoOp()
    {
        seedKxkbrc({{QStringLiteral("LayoutList"), QStringLiteral("latam")},
                    {QStringLiteral("VariantList"), QString()}});

        NotifySpy spy;
        KeyboardControl kb;
        kb.setLayout(QStringLiteral("latam"));
        kb.setVariant(QString());
        kb.setEnabled(true);
        QTest::qWait(300);
        clearCalls();
        spy.received.clear();

        kb.apply(); // el del arranque
        QTest::qWait(300);
        QVERIFY2(readCalls().isEmpty(), qPrintable(readCalls()));
        QVERIFY(spy.received.isEmpty());

        // Pero el botón «Aplicar ahora» sí notifica igual: el caso que existe
        // para arreglar es KWin y el archivo desincronizados, y ahí no hay nada
        // que escribir y todo que recargar.
        kb.applyNow();
        QTest::qWait(300);
        QVERIFY2(readCalls().isEmpty(), qPrintable(readCalls()));
        QCOMPARE(spy.received.size(), 1);
        QCOMPARE(spy.received.constFirst().signature, QStringLiteral("a{saay}"));
        QCOMPARE(spy.received.constFirst().groups.value(QStringLiteral("Layout")).size(), 4);
    }

    // LA SOLAPA, y el único bug que la captura no muestra.
    //
    // El combo de distribución no arranca vacío: si kdock.conf no tiene nada,
    // cae a kxkbrc y después a /etc/default/keyboard, para no abrirse en la
    // primera entrada de una lista alfabética (tildar la casilla ahí le pondría
    // albanés al escritorio). Pero ese valor es sólo lo que se *muestra*: la
    // config sigue vacía, y apply() sin distribución no escribe nada. O sea que
    // el usuario tildaba la casilla teniendo "latam" a la vista y no pasaba
    // absolutamente nada — que se lee como "la feature no anda".
    //
    // Encontrado manejando el diálogo desde una sonda, no mirándolo: la captura
    // muestra "latam" en las dos versiones.
    void tickingTheBoxAppliesWhatTheComboIsShowing()
    {
        // Un monitor inventado y ninguna pantalla real habilitada: así el
        // manager no arma ninguna DockWindow (con el Shared casi vacío eso es
        // un segfault que parece un bug del código que se está probando).
        qputenv("KDOCK_TEST_SCREENS", "VIRT-1,VIRT-9");
        {
            QSettings s(DockConfig::settingsFilePath(), QSettings::IniFormat);
            s.setValue(QStringLiteral("enabledScreens"), QStringLiteral("NOEXISTE-0"));
            s.setValue(QStringLiteral("knownScreens"), QStringLiteral("NOEXISTE-0"));
            s.sync();
        }
        const QString id = DockConfig::makeDockId(QStringLiteral("VIRT-1"), 0);
        DockConfig::addKnownDock(id);
        DockConfig::setDockEnabled(id, true);
        {
            DockConfig cfg(id);
            cfg.setDockDesktops({2}); // cualquiera que no sea el actual
        }

        // Nada guardado: es la precondición del bug.
        QVERIFY(KeyboardControl::layout().isEmpty());

        Theme theme;
        DesktopEntryIndex apps;
        KeyboardControl kb;
        QtCompat qtCompat(&theme); // la solapa Modo QT sólo existe si hay uno
        DockManager::Shared shared;
        shared.theme = &theme;
        shared.apps = &apps;
        shared.keyboard = &kb;
        shared.qtCompat = &qtCompat;
        DockManager manager(shared);

        SettingsDialog dlg(manager.configFor(id), &apps, nullptr, &manager, &theme);

        QGroupBox *box = nullptr;
        for (QGroupBox *g : dlg.findChildren<QGroupBox *>()) {
            if (g->title().contains(QLatin1String("KWin/Wayland")))
                box = g;
        }
        QVERIFY2(box, "no se encontró el grupo de teclado en la solapa Modo QT");

        // Desambiguados por el grupo que los contiene, nunca por índice del
        // diálogo entero: hay decenas de QComboBox y de QCheckBox.
        const auto combos = box->findChildren<QComboBox *>();
        QCOMPARE(combos.size(), 3); // distribución, variante, modelo
        const QString shown = combos.at(0)->currentData().toString();
        QVERIFY2(!shown.isEmpty(), "el combo abrió sin ninguna distribución seleccionada");

        const auto boxes = box->findChildren<QCheckBox *>();
        QCOMPARE(boxes.size(), 1);
        clearCalls();
        boxes.first()->setChecked(true);
        QTest::qWait(300);

        // Lo que se veía es lo que quedó guardado y lo que se escribió.
        QCOMPARE(KeyboardControl::layout(), shown);
        const QString calls = readCalls();
        QVERIFY2(calls.contains(QLatin1String("--key LayoutList ") + shown), qPrintable(calls));
    }

    // Sin distribución elegida no hay lote: la feature no tiene sentido sin
    // ella, y escribir un LayoutList vacío dejaría a KWin sin teclado.
    void withoutALayoutNothingIsApplied()
    {
        NotifySpy spy;
        KeyboardControl kb;
        kb.setEnabled(true);
        kb.setModel(QStringLiteral("pc105"));
        kb.applyNow();
        QTest::qWait(300);
        QVERIFY2(readCalls().isEmpty(), qPrintable(readCalls()));
        QVERIFY(spy.received.isEmpty());
    }
};

KDOCK_TEST_MAIN(TestKeyboard)
#include "tst_keyboard.moc"
