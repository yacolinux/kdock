// kdock's whole involvement with the control panel: find the binary, toggle it,
// and open its settings panel.
//
// kdock-controlmanager is a separate process with its own config and its own
// settings dialog (same split as kdock-previews and kdock-tilemenu). It stays
// resident after the first launch, so every click after that is a D-Bus toggle
// and opens instantly.

#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

class ControlManagerLauncher : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;

    // Show/hide the panel. Starts the process when it is not running yet, which
    // is what the widget's first click does. The screen name matters more here
    // than for the tile menu: the panel is a layer-shell surface, so it really
    // does land on that output.
    Q_INVOKABLE void toggle(const QString &screenName = QString());
    // Open the panel straight on one section's tab (the widget's context menu).
    Q_INVOKABLE void showSection(const QString &sectionId,
                                 const QString &screenName = QString());
    Q_INVOKABLE void openSettings();

    static QString binaryPath();
    static bool installed();
    static bool running();
    // Stop a running instance. It comes back on the next click of the widget,
    // or right away when preload is on (kdock::restartAll).
    static void quitRunning();
    // Whether kdock should bring the process up at startup instead of waiting
    // for the first click. Lives in the panel's own config file.
    static bool preload();
    static void setPreload(bool on);
    static void startIfPreloading();

private:
    static bool start(const QStringList &args = {});
};
