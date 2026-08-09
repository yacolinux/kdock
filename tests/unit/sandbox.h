// Test sandbox: a throwaway XDG home, installed before anything can look at it.
//
// Two reasons this is a header with its own main() instead of plain QTEST_MAIN:
//
//   1. XDG_DATA_HOME has to be set **before the first QStandardPaths call**, and
//      DockConfig::settingsFilePath() runs a one-shot migration the first time it
//      is asked. Setting it inside initTestCase() is already too late for some
//      paths, so it happens before QApplication is even built.
//   2. Without the isolation a test writes the *real* config of whoever runs it,
//      and the backends drive real hardware (see tests/lib/fakebin.sh).
//
// KDOCK_TEST_MAIN(Klass) is the drop-in replacement for QTEST_MAIN.

#pragma once

#include <QApplication>
#include <QDir>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

namespace kdocktest {

// Lives for the whole process: QTemporaryDir removes the tree when it dies.
inline QTemporaryDir &sandboxDir()
{
    static QTemporaryDir dir(QDir::tempPath() + QStringLiteral("/kdock-unit-XXXXXX"));
    return dir;
}

inline void installSandbox()
{
    const QString root = sandboxDir().path();
    qputenv("XDG_DATA_HOME", QFile::encodeName(root + QStringLiteral("/data")));
    qputenv("XDG_CONFIG_HOME", QFile::encodeName(root + QStringLiteral("/config")));
    qputenv("XDG_CACHE_HOME", QFile::encodeName(root + QStringLiteral("/cache")));
    QDir().mkpath(root + QStringLiteral("/data/kdock"));
    QDir().mkpath(root + QStringLiteral("/config"));
    QDir().mkpath(root + QStringLiteral("/cache"));
}

} // namespace kdocktest

#define KDOCK_TEST_MAIN(Klass)                                                            \
    int main(int argc, char *argv[])                                                      \
    {                                                                                     \
        kdocktest::installSandbox();                                                      \
        QApplication app(argc, argv);                                                     \
        app.setAttribute(Qt::AA_Use96Dpi, true);                                          \
        Klass tc;                                                                         \
        QTEST_SET_MAIN_SOURCE_PATH                                                        \
        return QTest::qExec(&tc, argc, argv);                                             \
    }
