// The panel itself: a QQuickView flagged as a layer-shell surface.
//
// The layer-shell plumbing is PreviewWindow's (anchors from the configured edge,
// margins, the destroy/recreate dance when the surface moves to another output)
// with two differences that define this window:
//
//  - the exclusive zone is always 0. This is a panel that opens and closes, not
//    a strut; reserving space would shove the user's maximized windows around
//    every time it appears;
//  - keyboard interactivity is switched on while visible and off while hidden.
//    That is what gives Esc and, more importantly, activeChanged — i.e. "the
//    user clicked somewhere else", which is the only close-on-focus-loss signal
//    a layer surface gets.
//
// The show/hide half (grace period after show(), the modal-dialog depth counter)
// is TileWindow's, for the same reasons documented there.

#pragma once

#include <QQuickView>
#include <QString>

#include "cmbackends.h"

class CmConfig;
class CmLayout;
class CmModel;
class CmSettingsDialog;
class Theme;

class CmWindow : public QQuickView
{
    Q_OBJECT
    // Tab currently on screen ("" = Principal). Lives here rather than in QML so
    // the D-Bus "show this section" call has somewhere to land.
    Q_PROPERTY(QString currentTab READ currentTab WRITE setCurrentTab NOTIFY currentTabChanged)
    // The sections, for the tab bar and the settings panel.
    Q_PROPERTY(QVariantList sections READ sections NOTIFY sectionsChanged)
    // Trailing part of every image://icon URL in the panel: the theme revision
    // plus the icon set that reads on this background. Every icon goes through
    // it — see iconSuffix().
    Q_PROPERTY(QString iconSuffix READ iconSuffix NOTIFY iconSuffixChanged)
public:
    CmWindow(CmConfig *config, Theme *theme, CmLayout *layout, CmModel *model,
             const CmBackends &backends);

    QVariantList sections() const;
    // The dock draws its widget icons out of one of two icon sets depending on
    // whether its background is light or dark, because half of breeze's
    // line-art icons vanish on the other one (a brightness icon on a dark panel
    // is invisible — seen in the first capture of the Video section). The panel
    // has the same problem and reuses the same two settings, so both follow the
    // user's choice without a second pair of options.
    QString iconSuffix() const;

    // screenName is the connector of the dock that asked. Unlike a toplevel, a
    // layer surface really is bound to that output — the name is authoritative
    // here, not a hint.
    void showOn(const QString &screenName);
    Q_INVOKABLE void hidePanel();

    QString currentTab() const { return m_currentTab; }
    void setCurrentTab(const QString &tab);

    Q_INVOKABLE void openSettings();
    Q_INVOKABLE void quitApp();
    // Relaunch with the same CLI arguments, then quit this instance.
    Q_INVOKABLE void restartSelf();

    // Launch a .desktop by id (the System section), or a plain command line.
    Q_INVOKABLE bool launchDesktop(const QString &id);
    Q_INVOKABLE bool runCommand(const QString &program, const QStringList &args = {});
    // Run a shell script by path (the Wallpaper section's "all monitors"), with
    // KDOCK_SCREEN set when a connector is given — that is the variable
    // next-wall.sh reads to limit itself to one monitor.
    Q_INVOKABLE bool runScript(const QString &path, const QString &screenName = QString());

    // Language changed (kdock wrote it to the shared conf): re-evaluate every
    // qsTr() of the panel and rebuild the settings dialog, which is Qt Widgets
    // and baked its strings in when it was built.
    void retranslate();

    // Modal helpers for the card context menu. Each one keeps the panel from
    // closing while it is up (see m_modalDepth).
    Q_INVOKABLE QString pickColor(const QString &current);
    Q_INVOKABLE QString promptText(const QString &title, const QString &label,
                                   const QString &current);
    Q_INVOKABLE bool confirm(const QString &title, const QString &text);

signals:
    void currentTabChanged();
    void sectionsChanged();
    void iconSuffixChanged();

private:
    void applyLayerProperties();
    void applyScreen();
    void scheduleApplyScreen();
    void applySize();
    void onActiveChanged();
    // True while one of our own dialogs is on screen: those steal the focus, and
    // closing the panel underneath the dialog it just opened is not what anyone
    // wants.
    bool blockingClose() const;

    CmConfig *m_config;
    Theme *m_theme;
    CmLayout *m_layout;
    CmModel *m_model;
    CmBackends m_backends;
    CmSettingsDialog *m_settingsDialog = nullptr;

    QString m_currentTab;
    QString m_screenName;
    int m_modalDepth = 0;
    // Deactivations right after show() are the compositor still handing focus
    // over, not the user clicking away.
    qint64 m_shownAt = 0;
    bool m_screenChangePending = false;
    // wl_output the current layer surface is bound to (as a raw pointer value).
    // Tracked here because QWindow::screen() is unreliable right after a
    // layer-surface recreation.
    quintptr m_boundOutput = 0;
};
