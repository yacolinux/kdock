// The system-tray window: a QQuickView flagged as a layer-shell surface.
//
// It is the control panel's window (controlmanager/src/cmwindow.h) stripped to
// what a tray strip needs: it anchors to the configured screen edge next to the
// dock, reserves no space (exclusive zone 0), toggles keyboard interactivity so
// a layer surface can hear "the user clicked away" (activeChanged), and hides —
// never destroys — so the next open is instant and, crucially, the SNI host it
// carries keeps running to collect tray items for the whole session.

#pragma once

#include <QQuickView>
#include <QString>

class SystrayConfig;
class SystrayModel;
class SystrayHost;
class SystraySettingsDialog;
class Theme;

class SystrayWindow : public QQuickView
{
    Q_OBJECT
    // Trailing part of every image://icon URL: theme revision (cache busting)
    // plus the icon set that reads on this window's background. Same rule as the
    // dock and the control panel.
    Q_PROPERTY(QString iconSuffix READ iconSuffix NOTIFY iconSuffixChanged)
public:
    SystrayWindow(SystrayConfig *config, Theme *theme, SystrayModel *model,
                  SystrayHost *host);

    QString iconSuffix() const;

    // screenName is the connector of the dock that asked. A layer surface really
    // is bound to that output, so the name is authoritative.
    void showOn(const QString &screenName);
    Q_INVOKABLE void hideWindow();
    Q_INVOKABLE void openSettings();
    // A tray item's menu is its own popup window; while it is up the tray window
    // is not the active surface, so the focus-loss auto-hide must stand down or
    // it tears the menu down the instant it opens.
    Q_INVOKABLE void setMenuOpen(bool on);
    void reloadConfig();

signals:
    void iconSuffixChanged();

private:
    uint layerAnchors(class QMargins *margins = nullptr) const;
    void applyLayerProperties();
    void applyScreen();
    void scheduleApplyScreen();
    void applySize();
    void onActiveChanged();
    bool blockingClose() const;

    SystrayConfig *m_config;
    Theme *m_theme;
    SystrayModel *m_model;
    SystrayHost *m_host;
    SystraySettingsDialog *m_settingsDialog = nullptr;

    QString m_screenName;
    // Deactivations right after show() are the compositor still handing focus
    // over, not the user clicking away.
    qint64 m_shownAt = 0;
    bool m_screenChangePending = false;
    // True while a tray item's menu (its own popup window) is open.
    bool m_menuOpen = false;
    // wl_output the current layer surface is bound to (raw pointer value).
    quintptr m_boundOutput = 0;
};
