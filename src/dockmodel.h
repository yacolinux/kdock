// The dock's item model: pinned launchers merged with running windows,
// grouped per application (matched via desktop entries).

#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QSet>

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
    // static separators (those belong to the apps block), with the widget's
    // "only pinned" flag on it ignores every window that is not one of its
    // launchers, and with its "ungroup windows" flag on it gives every window an
    // icon of its own even while the dock groups (see groupsWindows()). Everything else — grouping, activation, the context menu — is
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
    // The one window this row's hover preview shows: its active window, or the
    // first one if none is. Returns {uuid, width, height, windowIndex, title,
    // minimized, maximized, maximizable}; **empty** for a separator and for a
    // launcher with nothing running, which is what makes "no window, no preview"
    // fall out instead of being special-cased in QML.
    // The size is the window's own geometry: it gives the preview its aspect
    // ratio before the capture arrives, so the surface never resizes under the
    // pointer. The rest is what the preview's window buttons act on and draw
    // themselves from; QML re-reads the map on dataChanged, which is what makes
    // the buttons follow the window's real state.
    Q_INVOKABLE QVariantMap previewWindow(int row) const;
    Q_INVOKABLE void activateWindow(int row, int windowIndex);
    // The three window operations the hover preview's buttons offer, on the one
    // window `windowIndex` names (previewWindow() reports it). Deliberately
    // per-window and not per-icon, unlike closeAll()/sendToDesktop(): the
    // preview shows *a* window, so its buttons may not act on the others.
    Q_INVOKABLE void closeWindow(int row, int windowIndex);
    Q_INVOKABLE void setWindowMinimized(int row, int windowIndex, bool on);
    Q_INVOKABLE void setWindowMaximized(int row, int windowIndex, bool on);
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
        // The application this row belongs to, in the lowercase form the keys
        // use. Same value as `key` for a launcher row; for a window row of an
        // ungrouped model, `key` is the window instead ("win:<pointer>").
        QString appKey() const;
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
    // Whether this model puts every window of an app under one icon. The dock's
    // groupWindows, narrowed by the widget's own "ungroup windows" flag: a
    // widget may split what the dock groups, never the other way round (with
    // the dock-wide flag off there is nothing left to ungroup).
    bool groupsWindows() const;
    // Whether `key` (a lowercased app key) is one of this model's launchers.
    // Ungrouped, the extra windows of those apps are icons of this widget by
    // definition, so they skip the "only pinned" and the two exclusion filters.
    bool listsApp(const QString &key) const;
    // Whether this row reads as pinned without carrying the flag: ungrouped, the
    // extra window icons of a launcher belong to the same list entry, so their
    // right-click has to offer "Unpin" instead of a "Pin" that does nothing.
    bool pinnedByApp(const Item &item) const;
    // The row that carries the pinned flag for `appKey` (the launcher row), or
    // -1. Ungrouped, several rows share one app key and only that one has it.
    int rowOfPinnedApp(const QString &appKey) const;
    // Recompute m_monitorPinned. Cheap no-op unless this widget filters
    // monitor-wide: the sweep walks every dock of the screen, so it runs once
    // per change instead of once per window (acceptsStrayWindow is asked for
    // each of them).
    void refreshMonitorPinned();
    void placeWindow(AbstractWindow *window);
    void removeWindow(AbstractWindow *window);
    void windowChanged(AbstractWindow *window);
    QString keyForAppId(const QString &appId, DesktopEntry *outEntry) const;
    int rowOfKey(const QString &key) const;
    int rowOfWindow(AbstractWindow *window) const;
    QString keyForWindow(AbstractWindow *w) const;
    // Bounds-checked lookup shared by every per-window invokable: a stale row or
    // index (the preview's buttons hold on to both across a rebuild) has to be a
    // no-op, never a crash.
    AbstractWindow *windowAt(int row, int windowIndex) const;

    DockConfig *m_config;
    QString m_widgetToken; // empty = the dock's apps block
    DesktopEntryIndex *m_apps;
    WindowMonitor *m_monitor;
    VirtualDesktops *m_desktops = nullptr;
    QList<Item> m_items;
    bool m_updatingPinned = false;
    // Cached result of DockConfig::appsPinnedOnMonitor() for this widget. Empty
    // (and never filled) unless "skip apps pinned on this monitor" is on.
    QSet<QString> m_monitorPinned;
};
