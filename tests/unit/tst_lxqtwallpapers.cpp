// The parts of the LXQt wallpaper engine that can be decided without a
// compositor: which file a monitor should show on a given desktop, and how the
// slideshow steps.
//
// What is NOT here, and cannot be: the layer surfaces. Under offscreen (and
// under Xvfb) there is no layer-shell at all, so anchoring, the background
// layer and the click-through input region are only provable in the real
// Wayland session — see CLAUDE.md. Silence here means the *choice* of image is
// right, not that anything was drawn.
//
// The engine is left switched off throughout (its enabled() is
// DesktopWallpapers::enabled(), false by default), which is also what keeps a
// stray run from asking PCManFM to drop the desktop.

#include "desktopwallpapers.h"
#include "dockconfig.h"
#include "lxqtwallpapers.h"
#include "wallpaperfolder.h"

#include "sandbox.h"

#include <QDir>
#include <QImage>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

class TestLxqtWallpapers : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void folderListsImagesSorted();
    void folderNextWraps();
    void folderNextStartsAtTheFirstImage();
    void configIsSharedWithTheKdeEngine();
    void slideshowFoldersIncludeEveryDesktopAndKeepRemembered();
    void engineIsInertWhileDisabled();
    void currentImagesIsEmptyWhileNothingIsDrawn();
    void unconfiguredMonitorFallsBackToPcmanfm();
    void randomOtherAvoidsTheRecentOnes();
    void randomOtherDegradesOnASmallFolder();
    void everyAdvanceMovesAndRemembers();

private:
    QTemporaryDir m_dir;
    QStringList m_files;
};

void TestLxqtWallpapers::initTestCase()
{
    QVERIFY(m_dir.isValid());
    // Generated, not fixtures: what these assert on is the *order* of the
    // names, so a blob would hide the one thing under test. Deliberately
    // created out of order, and with a non-image among them.
    for (const QString &name : {QStringLiteral("c.png"), QStringLiteral("a.png"),
                                QStringLiteral("B.jpg")}) {
        const QString path = m_dir.filePath(name);
        QImage img(4, 4, QImage::Format_RGB32);
        img.fill(Qt::blue);
        QVERIFY(img.save(path));
        m_files << path;
    }
    QFile notes(m_dir.filePath(QStringLiteral("notes.txt")));
    QVERIFY(notes.open(QIODevice::WriteOnly));
    notes.write("not an image");
}

void TestLxqtWallpapers::folderListsImagesSorted()
{
    const QStringList images = WallpaperFolder::images({m_dir.path()});
    QCOMPARE(images.size(), 3); // notes.txt is not one
    // Case-insensitive, so "B.jpg" sits between "a" and "c" rather than before
    // both — otherwise a folder of mixed-case names would step in an order
    // nobody can predict from looking at it.
    QCOMPARE(QFileInfo(images.at(0)).fileName(), QStringLiteral("a.png"));
    QCOMPARE(QFileInfo(images.at(1)).fileName(), QStringLiteral("B.jpg"));
    QCOMPARE(QFileInfo(images.at(2)).fileName(), QStringLiteral("c.png"));
}

void TestLxqtWallpapers::folderNextWraps()
{
    const QStringList images = WallpaperFolder::images({m_dir.path()});
    QCOMPARE(WallpaperFolder::next({m_dir.path()}, images.at(0)), images.at(1));
    QCOMPARE(WallpaperFolder::next({m_dir.path()}, images.at(1)), images.at(2));
    // The wrap is the whole point of a slideshow: the last image steps back to
    // the first instead of stopping.
    QCOMPARE(WallpaperFolder::next({m_dir.path()}, images.at(2)), images.at(0));
}

void TestLxqtWallpapers::folderNextStartsAtTheFirstImage()
{
    const QStringList images = WallpaperFolder::images({m_dir.path()});
    // Nothing shown yet, and "the file we were showing is gone" — the engine
    // relies on these two being the same case (see LxqtWallpapers::imageFor).
    QCOMPARE(WallpaperFolder::next({m_dir.path()}, QString()), images.at(0));
    QCOMPARE(WallpaperFolder::next({m_dir.path()}, QStringLiteral("/gone/away.png")),
             images.at(0));
    // An empty or missing folder yields nothing rather than something arbitrary.
    QVERIFY(WallpaperFolder::next({}, QString()).isEmpty());
    QVERIFY(WallpaperFolder::next({QStringLiteral("/does/not/exist")}, QString()).isEmpty());
}

void TestLxqtWallpapers::configIsSharedWithTheKdeEngine()
{
    // One feature, one set of keys: a session moving between Plasma and LXQt
    // keeps its wallpapers. This is also why the LXQt engine has no settings of
    // its own to test.
    DesktopWallpapers::setImageFor(1, QStringLiteral("VIRT-1"), QStringLiteral("/tmp/one.png"));
    QCOMPARE(DesktopWallpapers::imageFor(1, QStringLiteral("VIRT-1")),
             QStringLiteral("/tmp/one.png"));

    // Desktop 1 is configurable under LXQt, which it is not under Plasma (there
    // it belongs to KDE and is snapshotted instead). The key has to survive on
    // its own, i.e. nothing may treat desktop 1 as special at the config level.
    DesktopWallpapers::setImageFor(2, QStringLiteral("VIRT-1"), QStringLiteral("/tmp/two.png"));
    QCOMPARE(DesktopWallpapers::imageFor(1, QStringLiteral("VIRT-1")),
             QStringLiteral("/tmp/one.png"));

    DesktopWallpapers::setSlideshowEnabled(1, true);
    DesktopWallpapers::setSlideshowFolder(1, QStringLiteral("VIRT-1"), m_dir.path());
    QVERIFY(DesktopWallpapers::slideshowEnabled(1));
    QCOMPARE(DesktopWallpapers::slideshowFolder(1, QStringLiteral("VIRT-1")), m_dir.path());
    DesktopWallpapers::setSlideshowEnabled(1, false);
}

void TestLxqtWallpapers::slideshowFoldersIncludeEveryDesktopAndKeepRemembered()
{
    const QString first = m_dir.path() + QStringLiteral("/first");
    const QString second = m_dir.path() + QStringLiteral("/second");
    const QString third = m_dir.path() + QStringLiteral("/third");
    const QString fourth = m_dir.path() + QStringLiteral("/fourth");

    for (const QString &screen : {QStringLiteral("FOLDER-A"), QStringLiteral("FOLDER-B"),
                                   QStringLiteral("FOLDER-C"), QStringLiteral("FOLDER-D")})
        DockConfig::addKnownScreen(screen);

    DesktopWallpapers::setSlideshowEnabled(2, true);
    DesktopWallpapers::setSlideshowFolder(2, QStringLiteral("FOLDER-A"), first);
    DesktopWallpapers::setSlideshowFolder(2, QStringLiteral("FOLDER-B"), second);
    DesktopWallpapers::setSlideshowFolder(2, QStringLiteral("FOLDER-C"), third);
    DesktopWallpapers::setSlideshowFolder(2, QStringLiteral("FOLDER-D"), second);
    DesktopWallpapers::setSlideshowEnabled(3, true);
    DesktopWallpapers::setSlideshowFolder(3, QStringLiteral("FOLDER-D"), fourth);

    QCOMPARE(DesktopWallpapers::slideshowFolders(2, QStringLiteral("FOLDER-C")),
             QStringList({third, first, second, fourth}));
    // Removing a configured entry does not remove the folder from the menu's
    // append-only registry: it remains available for reuse.
    DesktopWallpapers::setSlideshowFolder(2, QStringLiteral("FOLDER-A"), QString());
    QCOMPARE(DesktopWallpapers::slideshowFolders(2, QStringLiteral("FOLDER-C")),
             QStringList({third, first, second, fourth}));
    QCOMPARE(DesktopWallpapers::slideshowFolders(3, QStringLiteral("FOLDER-C")),
             QStringList({first, second, third, fourth}));
    DesktopWallpapers::setSlideshowEnabled(2, false);
    DesktopWallpapers::setSlideshowEnabled(3, false);
}

void TestLxqtWallpapers::engineIsInertWhileDisabled()
{
    // The master switch is off in the sandbox, and while it is, start() must
    // not create a surface or (much more importantly) tell PCManFM to give up
    // the desktop. A test that got this wrong would take the desktop away from
    // whoever ran it.
    QVERIFY(!LxqtWallpapers::enabled());
    LxqtWallpapers engine(nullptr);
    engine.start();
    // apply() is a no-op too: nothing was started, so there is nothing to
    // apply to. It must not crash on the null VirtualDesktops either.
    engine.apply(1);
    engine.advance();
    engine.quit();
}

void TestLxqtWallpapers::currentImagesIsEmptyWhileNothingIsDrawn()
{
    // This is ColorAuto's wallpaper source under LXQt, and "nothing to sample"
    // has to be an empty hash rather than a guess: PCManFM's own wallpaper is
    // deliberately NOT a fallback (one image for the whole session would make
    // per-monitor colours a fiction), so with the engine off ColorAuto must get
    // nothing and say so in its tab.
    //
    // The other half — that an active engine reports what is on each monitor —
    // cannot be checked here: there is no layer-shell without a compositor, so
    // no surface is ever created. See the header of this file.
    QVERIFY(!LxqtWallpapers::enabled());
    LxqtWallpapers engine(nullptr);
    QVERIFY(engine.currentImages().isEmpty());
    engine.start();
    QVERIFY(!engine.active());
    QVERIFY(engine.currentImages().isEmpty());
}

void TestLxqtWallpapers::unconfiguredMonitorFallsBackToPcmanfm()
{
    // The upgrade path, and the reason this fallback exists at all: a config
    // that comes from Plasma has the master switch on and **no desktop-1 keys**
    // — under Plasma desktop 1 belonged to KDE and was snapshotted instead of
    // configured. Under LXQt, where PCManFM's desktop is switched off, taking
    // that at face value would mean a black screen with no icons on the very
    // desktop the user logs into.
    //
    // XDG_CONFIG_HOME is the sandbox's (see sandbox.h), so writing PCManFM's
    // settings here does not touch the real desktop.
    const QString cfgDir = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
                           + QStringLiteral("/pcmanfm-qt/lxqt");
    QVERIFY(QDir().mkpath(cfgDir));
    {
        QSettings pcm(cfgDir + QStringLiteral("/settings.conf"), QSettings::IniFormat);
        pcm.setValue(QStringLiteral("Desktop/Wallpaper"), QStringLiteral("/tmp/pcmanfm-bg.png"));
        pcm.sync();
    }

    // Nothing configured for this monitor on desktop 1 -> PCManFM's own picture.
    QVERIFY(DesktopWallpapers::imageFor(1, QStringLiteral("VIRT-EMPTY")).isEmpty());
    LxqtWallpapers engine(nullptr);
    QCOMPARE(engine.imageForTesting(1, QStringLiteral("VIRT-EMPTY")),
             QStringLiteral("/tmp/pcmanfm-bg.png"));

    // …and a monitor that *is* configured still wins over it.
    DesktopWallpapers::setImageFor(1, QStringLiteral("VIRT-SET"), QStringLiteral("/tmp/ours.png"));
    QCOMPARE(engine.imageForTesting(1, QStringLiteral("VIRT-SET")), QStringLiteral("/tmp/ours.png"));
}

// A throwaway folder of `count` generated images, returned sorted the way
// WallpaperFolder sees them.
static QStringList seedFolder(QTemporaryDir &dir, int count)
{
    for (int i = 0; i < count; ++i) {
        QImage img(4, 4, QImage::Format_RGB32);
        img.fill(QColor(i * 7 % 256, 0, 0));
        img.save(dir.filePath(QStringLiteral("img%1.png").arg(i, 2, 10, QLatin1Char('0'))));
    }
    return WallpaperFolder::images({dir.path()});
}

void TestLxqtWallpapers::randomOtherAvoidsTheRecentOnes()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QStringList files = seedFolder(dir, 8);
    QCOMPARE(files.size(), 8);

    // The contract that fixes the reported bug: never one of the last three.
    // Run it enough times that a broken implementation cannot slip through —
    // picking uniformly from all 8 would hit the avoided set ~37% of the time,
    // so 200 draws makes a false pass essentially impossible.
    const QStringList recent = {files.at(0), files.at(1), files.at(2)};
    for (int i = 0; i < 200; ++i) {
        const QString picked = WallpaperFolder::randomOther({dir.path()}, recent);
        QVERIFY(!picked.isEmpty());
        QVERIFY2(!recent.contains(picked),
                 qPrintable(QStringLiteral("picked a recent one: %1").arg(picked)));
    }

    // And it really is random, not "the first candidate": over 200 draws from
    // five candidates, more than one distinct value must come out. (A fixed
    // implementation would return the same file every time and pass every
    // assertion above.)
    QSet<QString> seen;
    for (int i = 0; i < 200; ++i)
        seen.insert(WallpaperFolder::randomOther({dir.path()}, recent));
    QVERIFY2(seen.size() > 1, "randomOther() is not varying its answer");
}

void TestLxqtWallpapers::randomOtherDegradesOnASmallFolder()
{
    // A folder smaller than the history has to keep working: the constraint is
    // dropped oldest-first, so the most recent image is the last one given up.
    QTemporaryDir two;
    QVERIFY(two.isValid());
    const QStringList files = seedFolder(two, 2);
    QCOMPARE(files.size(), 2);
    // Both files are "recent" — with a strict rule there would be no candidate.
    const QStringList recent = {files.at(0), files.at(1)};
    for (int i = 0; i < 20; ++i) {
        const QString picked = WallpaperFolder::randomOther({two.path()}, recent);
        // It must still alternate: the one given up is the OLDEST, so the most
        // recently shown (files[1]) is the one it must not return.
        QCOMPARE(picked, files.at(0));
    }

    QTemporaryDir one;
    QVERIFY(one.isValid());
    const QStringList only = seedFolder(one, 1);
    // One image: there is nothing to alternate with, and returning empty would
    // blank the screen. It returns that image.
    QCOMPARE(WallpaperFolder::randomOther({one.path()}, only), only.at(0));
}

void TestLxqtWallpapers::everyAdvanceMovesAndRemembers()
{
    // The bug as reported: "parecen estar ciclando entre un par de wallpapers
    // entre cambios de escritorio virtual". The cause was that the engine only
    // ever *picked* an image when it had none stored, so each (desktop,
    // monitor) kept its first choice forever and switching desktops showed the
    // same handful of pictures. Every advance must move.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QStringList files = seedFolder(dir, 10);
    QCOMPARE(files.size(), 10);

    DesktopWallpapers::setSlideshowEnabled(2, true);
    DesktopWallpapers::setSlideshowFolder(2, QStringLiteral("VIRT-A"), dir.path());

    LxqtWallpapers engine(nullptr);
    QStringList seen;
    for (int i = 0; i < 12; ++i) {
        const QString picked = engine.advanceForTesting(2, QStringLiteral("VIRT-A"));
        QVERIFY(!picked.isEmpty());
        // Never the previous one, nor the two before it.
        const int back = qMin(3, seen.size());
        for (int k = 1; k <= back; ++k)
            QVERIFY2(picked != seen.at(seen.size() - k), "repeated a recent wallpaper");
        seen << picked;
    }
    // Twelve steps over ten files: it has to have used most of them, which is
    // the difference between "advancing" and "ping-ponging".
    QVERIFY2(QSet<QString>(seen.begin(), seen.end()).size() >= 5,
             "the advance is not spreading over the folder");

    // Each (desktop, monitor) keeps its own history: a second desktop pointed
    // at the same folder must not inherit the first one's.
    DesktopWallpapers::setSlideshowEnabled(3, true);
    DesktopWallpapers::setSlideshowFolder(3, QStringLiteral("VIRT-A"), dir.path());
    QVERIFY(!engine.advanceForTesting(3, QStringLiteral("VIRT-A")).isEmpty());

    // A desktop that is NOT in slideshow mode has no "next": advancing it must
    // say so (empty) rather than blank the static wallpaper it has.
    DesktopWallpapers::setSlideshowEnabled(4, false);
    QVERIFY(engine.advanceForTesting(4, QStringLiteral("VIRT-A")).isEmpty());

    DesktopWallpapers::setSlideshowEnabled(2, false);
    DesktopWallpapers::setSlideshowEnabled(3, false);
}

KDOCK_TEST_MAIN(TestLxqtWallpapers)

#include "tst_lxqtwallpapers.moc"
