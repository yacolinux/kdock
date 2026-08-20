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

public:
    explicit DockLink(QObject *parent = nullptr);

    bool available() const { return m_available; }
    bool darkMode() const { return m_darkMode; }

    Q_INVOKABLE void setDarkMode(bool on);
    Q_INVOKABLE void toggleDarkMode() { setDarkMode(!m_darkMode); }
    // Empty dockId = the primary dock.
    Q_INVOKABLE void openSettings(const QString &dockId = QString());
    // The dock's settings dialog, opened on its Redes tab (the connection
    // editor: static IP, DNS, routes). Nothing of that lives in this process.
    Q_INVOKABLE void openNetworkSettings(const QString &dockId = QString());
    // Restarts the kdock *process*, i.e. every dock it draws. The UI says so.
    Q_INVOKABLE void restartDock();
    Q_INVOKABLE void nextWallpaper(const QString &screenName);
    Q_INVOKABLE void nextWallpaperAll();
    // ColorAuto (see src/autocolorscheme.h). Both run in the dock's process on
    // purpose: the engine keeps which of its two generated schemes is current,
    // and that has to exist exactly once for the whole session.
    Q_INVOKABLE void generateColorScheme();
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

private:
    bool m_available = false;
    bool m_darkMode = false;
    QVariantList m_docks;
};
