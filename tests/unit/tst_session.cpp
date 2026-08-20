// Session detection, and the two consumers whose availability it decides.
//
// What this freezes is the bug it was written for: kdock used to read
// XDG_CURRENT_DESKTOP looking for "KDE", and this machine's LXQt session
// exports "LXQt:kwin_wayland" — a string that contains the *window manager*.
// Every feature gated on that test was dead in a session where KWin was up and
// answering. So the cases below are mostly about what a real session string
// looks like, not about the enum.
//
// The bus-backed halves (hasKWin / hasPlasmaShell) are deliberately not
// asserted on: their answer is whatever session the test happens to run in, and
// a test that pinned it would pass on this machine and fail in CI.

#include "powercontrol.h"
#include "session.h"

#include "sandbox.h"

#include <QProcess>
#include <QStandardPaths>
#include <QTest>

class TestSession : public QObject
{
    Q_OBJECT

private slots:
    void detectsFromEnvironment_data();
    void detectsFromEnvironment();
    void testSeamForcesKind();
    void powerControlIsUnavailableInAnUnknownSession();
};

namespace {

// Session::kind() caches its answer for the life of the process (nobody
// switches session type under a running dock), so each case has to be a fresh
// process. This re-runs *this very binary* with one test function and a doctored
// environment, which is the cheapest way to get one.
//
// Returns the "<field>: <value>" line the child prints, for `field`.
QString reportInProcess(const QMap<QString, QString> &env, const QString &field)
{
    QProcess p;
    QProcessEnvironment pe = QProcessEnvironment::systemEnvironment();
    // Clear the three inputs first: the *real* session's variables are in the
    // inherited environment and would decide the answer for us.
    pe.remove(QStringLiteral("XDG_CURRENT_DESKTOP"));
    pe.remove(QStringLiteral("XDG_SESSION_DESKTOP"));
    pe.remove(QStringLiteral("QT_QPA_PLATFORMTHEME"));
    pe.remove(QStringLiteral("KDOCK_TEST_SESSION"));
    for (auto it = env.constBegin(); it != env.constEnd(); ++it)
        pe.insert(it.key(), it.value());
    pe.insert(QStringLiteral("KDOCK_SESSION_REPORT"), QStringLiteral("1"));
    p.setProcessEnvironment(pe);
    p.start(QCoreApplication::applicationFilePath(), {QStringLiteral("-silent")});
    if (!p.waitForFinished(10000))
        return QStringLiteral("<timeout>");
    const QString out = QString::fromUtf8(p.readAllStandardOutput());
    const QString prefix = field + QLatin1String(": ");
    for (const QString &line : out.split(QLatin1Char('\n'))) {
        if (line.startsWith(prefix))
            return line.mid(prefix.size()).trimmed();
    }
    return QStringLiteral("<no answer>");
}

QString kindInProcess(const QMap<QString, QString> &env)
{
    return reportInProcess(env, QStringLiteral("kind"));
}

QString kindName()
{
    switch (Session::kind()) {
    case Session::Kde:   return QStringLiteral("Kde");
    case Session::Lxqt:  return QStringLiteral("Lxqt");
    case Session::Other: return QStringLiteral("Other");
    }
    return QStringLiteral("?");
}

} // namespace

void TestSession::detectsFromEnvironment_data()
{
    QTest::addColumn<QString>("currentDesktop");
    QTest::addColumn<QString>("sessionDesktop");
    QTest::addColumn<QString>("platformTheme");
    QTest::addColumn<QString>("expected");

    // The one that mattered: the LXQt session of this project runs KWin, and
    // its XDG_CURRENT_DESKTOP names both. Read as "does it mention KDE?" this
    // is a KDE session; it is not.
    QTest::newRow("lxqt with kwin") << "LXQt:kwin_wayland" << "lxqt-wayland" << "lxqt" << "Lxqt";
    QTest::newRow("lxqt plain") << "LXQt" << "lxqt" << "lxqt" << "Lxqt";
    // The platform theme alone is enough: a session started by hand may export
    // nothing else.
    QTest::newRow("lxqt theme only") << "" << "" << "lxqt" << "Lxqt";
    QTest::newRow("plasma wayland") << "KDE" << "plasmawayland" << "" << "Kde";
    QTest::newRow("plasma x11") << "KDE" << "plasma" << "" << "Kde";
    QTest::newRow("gnome") << "GNOME" << "gnome" << "" << "Other";
    QTest::newRow("nothing") << "" << "" << "" << "Other";
}

void TestSession::detectsFromEnvironment()
{
    QFETCH(QString, currentDesktop);
    QFETCH(QString, sessionDesktop);
    QFETCH(QString, platformTheme);
    QFETCH(QString, expected);

    QMap<QString, QString> env;
    if (!currentDesktop.isEmpty())
        env.insert(QStringLiteral("XDG_CURRENT_DESKTOP"), currentDesktop);
    if (!sessionDesktop.isEmpty())
        env.insert(QStringLiteral("XDG_SESSION_DESKTOP"), sessionDesktop);
    if (!platformTheme.isEmpty())
        env.insert(QStringLiteral("QT_QPA_PLATFORMTHEME"), platformTheme);

    QCOMPARE(kindInProcess(env), expected);
}

void TestSession::testSeamForcesKind()
{
    // The seam has to win over a real session's variables, or a test could not
    // ask for a session it is not running in. An unknown value is Other, which
    // is how a test asks for "a desktop kdock knows nothing about".
    QMap<QString, QString> lxqtEnv{{QStringLiteral("XDG_CURRENT_DESKTOP"), QStringLiteral("KDE")},
                                   {QStringLiteral("KDOCK_TEST_SESSION"), QStringLiteral("lxqt")}};
    QCOMPARE(kindInProcess(lxqtEnv), QStringLiteral("Lxqt"));

    QMap<QString, QString> otherEnv{{QStringLiteral("XDG_CURRENT_DESKTOP"), QStringLiteral("LXQt")},
                                    {QStringLiteral("KDOCK_TEST_SESSION"), QStringLiteral("nonsense")}};
    QCOMPARE(kindInProcess(otherEnv), QStringLiteral("Other"));
}

void TestSession::powerControlIsUnavailableInAnUnknownSession()
{
    // Session::kind() is cached per process, so this too has to be asked of a
    // child. A desktop kdock knows nothing about gets no session buttons: the
    // four places that draw them gate on exactly this.
    QMap<QString, QString> env{{QStringLiteral("KDOCK_TEST_SESSION"), QStringLiteral("other")}};
    QCOMPARE(reportInProcess(env, QStringLiteral("power")), QStringLiteral("0"));

    // And under LXQt they are available wherever LXQt's own .desktop files (or
    // lxqt-leave) are installed. Skipped rather than failed on a machine
    // without LXQt: this asserts kdock's wiring, not the packages of the host.
    env[QStringLiteral("KDOCK_TEST_SESSION")] = QStringLiteral("lxqt");
    if (QStandardPaths::locate(QStandardPaths::ApplicationsLocation,
                               QStringLiteral("lxqt-logout.desktop")).isEmpty()
        && QStandardPaths::findExecutable(QStringLiteral("lxqt-leave")).isEmpty()) {
        QSKIP("LXQt is not installed here, so there is nothing to resolve");
    }
    QCOMPARE(reportInProcess(env, QStringLiteral("power")), QStringLiteral("1"));
}

// The child-process mode of this binary: print the detected kind and leave,
// before Qt Test can run anything.
int main(int argc, char *argv[])
{
    if (!qEnvironmentVariableIsEmpty("KDOCK_SESSION_REPORT")) {
        QCoreApplication app(argc, argv);
        printf("kind: %s\n", qPrintable(kindName()));
        PowerControl power;
        printf("power: %d\n", power.available() ? 1 : 0);
        return 0;
    }
    kdocktest::installSandbox();
    QApplication app(argc, argv);
    app.setAttribute(Qt::AA_Use96Dpi, true);
    TestSession tc;
    QTEST_SET_MAIN_SOURCE_PATH
    return QTest::qExec(&tc, argc, argv);
}

#include "tst_session.moc"
