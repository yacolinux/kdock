// The dock's item model: pinned launchers merged with running windows,
// grouped per application (matched via desktop entries).

#pragma once

#include <QAbstractListModel>
#include <QList>

#include "desktopentry.h"

class DockConfig;
class WindowMonitor;
class AbstractWindow;
class VirtualDesktops;

class DockModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        IconNameRole,
        PinnedRole,
        WindowCountRole,
        ActiveRole,
        MinimizedRole,
        IsSeparatorRole,
        TitleRole,
        // Appended, not slotted in: the values above are what QML already binds.
        SeparatorTransparentRole,
    };

    // `widgetToken` empty: the dock's own apps block — launchers come from
    // DockConfig::pinned() and the two static separators are drawn.
    // `widgetToken` = "appsel<n>": the model of one selectable-apps widget. Its
    // launchers are that widget's own list (DockConfig::widgetApps), it draws no
    // static separators (those belong to the apps block), and with the widget's
    // "only pinned" flag on it ignores every window that is not one of its
    // launchers. Everything else — grouping, activation, the context menu — is
    // the same code, which is the point: the widget *is* the apps block.
    DockModel(DockConfig *config, DesktopEntryIndex *apps, WindowMonitor *monitor,
              VirtualDesktops *desktops = nullptr, const QString &widgetToken = {},
              QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void activate(int row);     // left click: launch / focus / cycle / minimize
    Q_INVOKABLE void launch(int row);       // always start a new instance
    Q_INVOKABLE void closeAll(int row);
    // Move every window of this icon to a 1-based virtual desktop, without
    // switching the view there. No-op on wlroots (no desktop requests in
    // wlr-foreign-toplevel) and when KWin reports no such desktop.
    Q_INVOKABLE void sendToDesktop(int row, int position);
    Q_INVOKABLE void togglePinned(int row);
    Q_INVOKABLE QVariantList windowList(int row) const;
    Q_INVOKABLE void activateWindow(int row, int windowIndex);
    Q_INVOKABLE void moveItem(int from, int to); // drag-and-drop reordering
    Q_INVOKABLE void syncWindows();               // deferred re-sync after startup
    // Every name the apps block draws (separators have none), for Dock.qml's
    // widest-label measurement. It asks the model instead of the Repeater
    // because the Repeater's id lives inside another component scope and is not
    // reachable from the QML root (bug 2026-07-30).
    Q_INVOKABLE QStringList labelStrings() const;

private:
    struct Item
    {
        QString key; // grouping key (lowercase desktop id or app_id)
        DesktopEntry entry;
        QString fallbackAppId;
        bool pinned = false;
        QList<AbstractWindow *> windows;
        bool isSeparator = false;
        int separatorIndex = 0; // 1 or 2
        bool separatorTransparent = false; // takes the room, draws no line

        QString displayName() const;
        QString iconName() const;
    };

    void rebuild();
    // The launcher list this model persists to: the dock's `pinned` or the
    // widget's own list. Every read and write of the pinned set goes through
    // these two, so a widget never writes the dock's launchers and vice versa.
    QStringList pinnedIds() const;
    void savePinnedIds(const QStringList &ids);
    // Whether a window that matches no launcher row gets one of its own. False
    // only for a widget with "only pinned" on, which is then a fixed set of
    // icons that still shows the window state of its own apps.
    bool acceptsStrayWindows() const;
    // Whether this model may give a row of its own to a window of `appId`. It
    // is acceptsStrayWindows() plus the "skip apps pinned in another widget"
    // filter, which is what lets one appsel widget be the catch-all for
    // everything the others do not already draw. Only asked for windows with no
    // row yet: the widget's own launchers are never filtered out.
    bool acceptsStrayWindow(const QString &appId) const;
    void placeWindow(AbstractWindow *window);
    void removeWindow(AbstractWindow *window);
    void windowChanged(AbstractWindow *window);
    QString keyForAppId(const QString &appId, DesktopEntry *outEntry) const;
    int rowOfKey(const QString &key) const;
    int rowOfWindow(AbstractWindow *window) const;
    QString keyForWindow(AbstractWindow *w) const;

    DockConfig *m_config;
    QString m_widgetToken; // empty = the dock's apps block
    DesktopEntryIndex *m_apps;
    WindowMonitor *m_monitor;
    VirtualDesktops *m_desktops = nullptr;
    QList<Item> m_items;
    bool m_updatingPinned = false;
};
