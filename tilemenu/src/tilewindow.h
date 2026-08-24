// The menu itself: a plain maximized toplevel, not a layer-shell surface.
//
// That is the whole trick of this binary. KWin already subtracts the docks'
// layer-shell struts when it computes the maximize area, so "maximized" *is*
// "the whole desktop except the visible docks" — for any edge, any number of
// docks, and the Plasma panels too, with no geometry code on our side. A dock in
// autohide reserves nothing, so the menu covers the screen and the dock (which
// lives on the `top` layer, above ordinary windows) draws over it when revealed.
//
// It also means keyboard focus is simply what a normal window gets, with none of
// the double-buffered keyboard_interactivity dance AppMenuPopup needs.

#pragma once

#include <QQuickView>
#include <QString>

class AppMenu;
class DesktopEntryIndex;
class PowerControl;
class Theme;
class TileConfig;
class TileLayout;
class TileModel;
class TileSettingsDialog;
class TileUsage;

class TileWindow : public QQuickView
{
    Q_OBJECT
public:
    TileWindow(TileConfig *config, Theme *theme, DesktopEntryIndex *apps, AppMenu *menu,
               TileLayout *layout, TileModel *model, PowerControl *power, TileUsage *usage);

    // screenName is the connector of the dock that asked. Wayland does not let a
    // client place a toplevel, so this is a hint: it is applied through
    // setScreen() while the window is down, and in practice KWin puts a new
    // window on the active output — which is the one the user just clicked on.
    void showOn(const QString &screenName);
    Q_INVOKABLE void hideMenu();

    Q_INVOKABLE void launch(const QString &id);
    Q_INVOKABLE void openSettings();
    Q_INVOKABLE void quitApp();

    // Language changed (kdock wrote it to the shared conf): re-evaluate every
    // qsTr() of the menu and rebuild the settings panel, which is Qt Widgets
    // and baked its strings in when it was built.
    void retranslate();

    // Modal helpers for the tile context menu. Each one keeps the menu from
    // closing while it is up (see m_modalDepth).
    Q_INVOKABLE QString pickIcon(const QString &current);
    Q_INVOKABLE QString pickColor(const QString &current);
    Q_INVOKABLE QString pickImage(const QString &current);
    Q_INVOKABLE QString promptText(const QString &title, const QString &label,
                                   const QString &current);
    Q_INVOKABLE bool confirm(const QString &title, const QString &text);

private:
    void onActiveChanged();
    // True while one of our own dialogs is on screen: those steal the focus, and
    // closing the menu underneath the dialog it just opened is not what anyone
    // wants.
    bool blockingClose() const;

    TileConfig *m_config;
    Theme *m_theme;
    DesktopEntryIndex *m_apps;
    AppMenu *m_menu;
    TileLayout *m_layout;
    TileModel *m_model;
    PowerControl *m_power;
    TileUsage *m_usage;
    TileSettingsDialog *m_settingsDialog = nullptr;
    int m_modalDepth = 0;
    // Deactivations right after show() are the compositor still handing focus
    // over, not the user clicking away.
    qint64 m_shownAt = 0;
};
