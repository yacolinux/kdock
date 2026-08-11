// The weather window: an ordinary toplevel QQuickView, not a layer-shell
// surface.
//
// Same call as kdock-tilemenu and kdock-calendar, and for the same three
// reasons: it needs no privileged Wayland interface, it runs straight from
// build/ with no .desktop dance, and it can be screenshotted whole under Xvfb
// like any X window — which is what makes iterating on the layout cheap.
//
// Closing it quits the process (setQuitOnLastWindowClosed is off, so this is
// explicit): the binary is single-instance but *not* resident, so an install
// never leaves an old one mapped in memory.

#pragma once

#include <QQuickView>
#include <QString>

class Theme;
class WeatherConfig;
class WeatherControl;
class WeatherSettingsDialog;

class WeatherWindow : public QQuickView
{
    Q_OBJECT
public:
    WeatherWindow(WeatherConfig *config, WeatherControl *weather, Theme *theme);

    // screenName is the connector of the dock that asked. A Wayland client
    // cannot place a toplevel, so this is a hint applied through setScreen()
    // while the window is down.
    void showOn(const QString &screenName);

    Q_INVOKABLE void closeWindow();
    Q_INVOKABLE void openSettings();
    // The suffix every image://icon URL of this window carries: theme revision
    // (cache busting) plus the icon set that reads on this background. Same
    // rule as the dock and the control panel — never "@" + theme.revision by
    // hand, or a dark line-art icon lands on a dark panel.
    Q_INVOKABLE QString iconSuffix() const;

    // Language changed: re-evaluate the QML and rebuild the settings dialog,
    // which is Qt Widgets and baked its strings in when it was built.
    void retranslate();

protected:
    // A window manager close (the ✕, Alt+F4) ends the process.
    bool event(QEvent *e) override;

private:
    WeatherConfig *m_config;
    WeatherControl *m_weather;
    Theme *m_theme;
    WeatherSettingsDialog *m_settingsDialog = nullptr;
};
