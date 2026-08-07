// Owns everything: the shared services and one strip per opted-in monitor.
//
// Same opt-in scheme as kdock's DockManager (src/dockmanager.cpp): a strip is
// shown where the user enabled it *and* the monitor is connected, hotplug
// re-runs sync(), and on the very first run the primary monitor is adopted so
// the binary is not silently useless. Simpler in one way — one strip per monitor,
// no slots — so a strip is identified by the plain screen name.
//
// The capture pipeline is deliberately global: one ThumbnailSource (one capture
// in flight for the whole process) and one ThumbnailCache feeding every strip's
// image provider.

#pragma once

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

class DesktopEntryIndex;
class KWinWindows;
class PreviewConfig;
class PreviewModel;
class PreviewSettingsDialog;
class PreviewWindow;
class ScreenShotSource;
class Theme;
class ThumbnailCache;
class ThumbnailSource;
class VirtualDesktops;

class PreviewManager : public QObject
{
    Q_OBJECT
public:
    explicit PreviewManager(QObject *parent = nullptr);

    QStringList connectedScreens() const;
    // Monitors the settings panel should offer: connected plus every one ever
    // seen (so an unplugged screen can still be configured).
    QStringList knownScreensForUi() const;

    // Master switch, shared with kdock's "Activar Dock Preview" checkbox. With
    // it off no strip is shown, so `--settings` can open the panel without
    // putting anything on screen.
    bool enabled() const;
    void setEnabled(bool enabled);
    // Re-read the settings file and apply (kdock flipped the master switch on an
    // already running instance).
    void reload();

    bool isScreenEnabled(const QString &screenName) const;
    // Persist + apply live.
    void setScreenEnabled(const QString &screenName, bool enabled);

    // Config of a strip, created on demand so the settings panel can edit a
    // monitor that is not currently showing one.
    PreviewConfig *configFor(const QString &screenName);

    void showSettings();

    // Language changed (kdock wrote it to the shared conf): re-evaluate every
    // qsTr() of the strips and rebuild the settings panel, which is Qt Widgets
    // and baked its strings in when it was built.
    void retranslate();

    // Used by the --dump-captures diagnostic in main.cpp.
    KWinWindows *windows() const { return m_windows; }
    ThumbnailSource *source() const;
    ThumbnailCache *cache() const { return m_cache; }

private:
    void migrateFirstRun();
    void sync();
    void createStrip(const QString &screenName);
    void destroyStrip(const QString &screenName);
    QString primaryScreenName() const;

    // Shared services.
    Theme *m_theme;
    DesktopEntryIndex *m_apps;
    KWinWindows *m_windows;
    VirtualDesktops *m_desktops;
    ThumbnailCache *m_cache;
    ScreenShotSource *m_source;
    PreviewSettingsDialog *m_dialog = nullptr;

    struct Instance {
        PreviewModel *model = nullptr;
        PreviewWindow *window = nullptr;
    };

    QHash<QString, PreviewConfig *> m_configs; // cached, by screen name
    QHash<QString, Instance> m_instances;      // only for shown strips
    QSet<QString> m_captureFailWarned;         // one warning per window, not per try
};
