#include "apprestart.h"

#include "controlmanagerlauncher.h"
#include "previewslauncher.h"
#include "tilemenulauncher.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QProcess>
#include <QThread>

namespace {
// Long enough for three processes to notice the D-Bus call and tear down,
// short enough that a wedged one does not hold the restart hostage: past this
// the dock relaunches anyway (worst case an accessory survives, exactly like
// before this existed).
constexpr int kQuitWaitMs = 2000;
constexpr int kPollMs = 25;

bool anyAccessoryRunning()
{
    return PreviewsLauncher::running() || TileMenuLauncher::running()
           || ControlManagerLauncher::running();
}
} // namespace

void kdock::restartAll()
{
    PreviewsLauncher::quitRunning();
    TileMenuLauncher::quitRunning();
    ControlManagerLauncher::quitRunning();

    // Waiting is not politeness, it is the whole trick: the relaunched kdock
    // brings the accessories back through startIfEnabled()/startIfPreloading(),
    // and both of those skip when the bus name is still registered. Handing over
    // while the old processes are still dying is how you end up with none of
    // them running. running() is a blocking round trip to the bus daemon, so
    // the loop costs nothing while it spins.
    QElapsedTimer waited;
    waited.start();
    while (anyAccessoryRunning() && waited.elapsed() < kQuitWaitMs)
        QThread::msleep(kPollMs);

    // Preserve the CLI arguments this instance was started with (e.g.
    // --screen <name>) so the relaunched dock lands on the same output.
    QStringList args = QCoreApplication::arguments();
    if (!args.isEmpty())
        args.removeFirst();
    QProcess::startDetached(QCoreApplication::applicationFilePath(), args);
    QCoreApplication::quit();
}
