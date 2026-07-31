// kdock's handle on the accessory previews binary (previews/, PLAN.md).
//
// kdock never draws the preview strips: it only starts, stops and configures
// `kdock-previews`, which is a separate process with its own settings file and
// its own multi-monitor handling. The contract is deliberately tiny:
//   - the master switch lives in the previews' own config file, so that binary
//     stays the single source of truth even when kdock is not running;
//   - everything else goes over its D-Bus service, org.kdock.Previews.

#pragma once

#include <QObject>
#include <QString>

class PreviewsLauncher : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;

    // Absolute path of the kdock-previews binary; empty when it is not
    // installed (an older install, or a build without the previews target).
    static QString binaryPath();
    static bool available() { return !binaryPath().isEmpty(); }
    // Whether an instance currently owns org.kdock.Previews.
    static bool running();

    // Master switch, read from / written to the previews' settings file.
    static bool enabled();

    // Persist the switch and apply it now: start the process (or tell a running
    // one to re-read the setting) / ask it to quit.
    void setEnabled(bool on);
    // Open the previews' own configuration panel.
    void openSettings();
    // Start the process when the switch is on and nothing is running yet.
    // Called once from main() after the docks are up.
    void startIfEnabled();

private:
    static bool start(const QStringList &args = {});
    // Path of the previews' shared settings file. Mirrors
    // PreviewConfig::settingsFilePath() (previews/src/previewconfig.cpp) — the
    // one duplicated line in the whole integration.
    static QString settingsFilePath();
};
