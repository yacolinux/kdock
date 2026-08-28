// Client side of org.kdock.Dock — the small service kdock exports (see
// src/dockservice.h).
//
// It exists because three things the System and Video sections need cannot be
// done from outside the dock's process:
//   - dark mode: writing darkModeOn into the shared .conf does *not* repaint a
//     running dock (there is no file watcher), so the toggle has to be a call;
//   - the settings dialog: it is only reachable from the dock's own menu;
//   - restart: it relaunches the process with its original arguments.
//
// kdock not being on the bus is normal (the panel can outlive a dock restart),
// so `available` is a property the cards gate on rather than an error.

#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>

class DockLink : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(bool darkMode READ darkMode WRITE setDarkMode NOTIFY changed)
    // ColorAuto's *automatic* mode, and whether a generation would find a
    // wallpaper at all. The second one is what stops the card from claiming
    // success over a no-op — see refresh().
    Q_PROPERTY(bool colorAutoEnabled READ colorAutoEnabled WRITE setColorAutoEnabled NOTIFY changed)
    Q_PROPERTY(bool colorAutoCanRead READ colorAutoCanRead NOTIFY changed)
    // Whether the dock on the bus answers these two at all.
    //
    // The panel and the dock restart independently, so a panel from this build
    // can be talking to a kdock that predates ColorAuto's switch — and the two
    // "no" answers are indistinguishable from a real no otherwise. With this
    // false the card falls back to how it behaved before: buttons live, no
    // claims about wallpapers it cannot ask about.
    Q_PROPERTY(bool colorAutoKnown READ colorAutoKnown NOTIFY changed)

public:
    explicit DockLink(QObject *parent = nullptr);

    bool available() const { return m_available; }
    bool darkMode() const { return m_darkMode; }
    bool colorAutoEnabled() const { return m_colorAutoEnabled; }
    bool colorAutoCanRead() const { return m_colorAutoCanRead; }
    bool colorAutoKnown() const { return m_colorAutoKnown; }

    Q_INVOKABLE void setDarkMode(bool on);
    Q_INVOKABLE void toggleDarkMode() { setDarkMode(!m_darkMode); }
    // Empty dockId = the primary dock.
    Q_INVOKABLE void openSettings(const QString &dockId = QString());
    // Opens Kdock's settings directly on the Wallpapers tab.
    Q_INVOKABLE void openWallpaperSettings(const QString &dockId = QString());
    // The dock's settings dialog, opened on its Redes tab (the connection
    // editor: static IP, DNS, routes). Nothing of that lives in this process.
    Q_INVOKABLE void openNetworkSettings(const QString &dockId = QString());
    // Restarts the kdock *process*, i.e. every dock it draws. The UI says so.
    Q_INVOKABLE void restartDock();
    Q_INVOKABLE void nextWallpaper(const QString &screenName);
    Q_INVOKABLE void nextWallpaperAll();
    // Explicit request from the desktop canvas. This is deliberately not a
    // setting toggle: kdock decides whether to ignore the configured policy.
    Q_INVOKABLE void activateScreensaver(const QString &screenName, int engine = -1,
                                         const QString &page = QString());
    // ColorAuto (see src/autocolorscheme.h). Both run in the dock's process on
    // purpose: the engine keeps which of its two generated schemes is current,
    // and that has to exist exactly once for the whole session.
    Q_INVOKABLE void generateColorScheme();
    // The automatic mode. A call and not a config write: switching it on
    // captures the defaults and applies straight away, and dark mode can refuse
    // — all of that lives in the dock's process.
    Q_INVOKABLE void setColorAutoEnabled(bool on);
    Q_INVOKABLE void toggleColorAuto() { setColorAutoEnabled(!m_colorAutoEnabled); }
    // Re-read just the "is there a wallpaper to sample" answer. It changes
    // without any signal to hang off (a slideshow step, a monitor unplugged,
    // the wallpaper engine switched off in the dock's settings), so the card
    // asks again when it is about to matter instead of trusting a cache.
    Q_INVOKABLE void refreshColorAuto();
    // Blocking, unlike everything else here: the card shows the id it saved,
    // and there is nothing to show until the dock answers. Short timeout so a
    // dock that went away cannot freeze the panel.
    Q_INVOKABLE QString saveColorScheme();
    // [{ id, screen, primary }]
    Q_INVOKABLE QVariantList docks() const { return m_docks; }

public slots:
    void refresh();

signals:
    void changed();

private slots:
    void onDarkModeChanged(bool on);
    void onColorAutoChanged(bool enabled);

private:
    bool m_available = false;
    bool m_darkMode = false;
    bool m_colorAutoEnabled = false;
    bool m_colorAutoCanRead = false;
    bool m_colorAutoKnown = false;
    QVariantList m_docks;
};
