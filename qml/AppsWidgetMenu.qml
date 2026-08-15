// AppsWidgetMenu.qml - right-click submenu of one "selectable apps" widget:
// its four checkboxes and a manual reload. Same settings as the panel in the
// dialog's Widgets tab, reachable without opening the dialog.
//
// It hangs off the app-icon context menu in Dock.qml, which is the only
// right-click a selectable-apps widget has: the widget is a block, so its
// section MouseArea (and with it the section menu) is switched off.
//
// `token` says which instance this is; the row that opens the submenu is hidden
// through `rowVisible` (read by SubMenuDelegate) for the dock's own apps block,
// which has none of these settings.

import QtQuick
import QtQuick.Controls

Menu {
    id: menu

    property string token: ""
    // Read by SubMenuDelegate: the row this submenu opens from.
    property string menuIcon: "view-list-icons"
    property bool rowVisible: true

    title: qsTr("Apps Seleccionables")
    popupType: Popup.Window
    // A Menu does not size itself to its widest item, and these labels are the
    // longest of any menu in the dock — plus every row here carries both a check
    // column and an icon, which implicitWidth underestimates. 320 clipped the two
    // filters in Spanish ("…en el monito"); 480 is measured against the widest.
    width: Math.max(implicitWidth + 64, 480)

    // The getters are Q_INVOKABLE calls with no NOTIFY behind them, so a plain
    // `checked:` binding would freeze at whatever the flags were when the menu
    // was built — and the same four settings are edited from the dialog. Same
    // trick as config.gapRevision in Dock.qml: touch a counter first, and bump
    // it from the signals that do carry the token.
    property int rev: 0
    Connections {
        target: config
        function onWidgetOnlyPinnedChanged(t) { if (t === menu.token) menu.rev++ }
        function onWidgetExcludeOthersChanged(t) { if (t === menu.token) menu.rev++ }
        function onWidgetExcludeMonitorChanged(t) { if (t === menu.token) menu.rev++ }
        function onWidgetUngroupWindowsChanged(t) { if (t === menu.token) menu.rev++ }
    }

    readonly property bool onlyPinned: (menu.rev, config.widgetOnlyPinned(menu.token))
    readonly property bool excludeMonitor: (menu.rev, config.widgetExcludeMonitor(menu.token))

    IconMenuItem {
        text: qsTr("Show pinned only")
        iconName: "window-pin"
        checkable: true
        checked: menu.onlyPinned
        onTriggered: config.setWidgetOnlyPinned(menu.token, !menu.onlyPinned)
    }
    IconMenuItem {
        text: qsTr("Skip apps pinned on this monitor")
        iconName: "video-display"
        checkable: true
        checked: menu.excludeMonitor
        // The two filters only act on what "show pinned only" lets through.
        enabled: !menu.onlyPinned
        onTriggered: config.setWidgetExcludeMonitor(menu.token, !menu.excludeMonitor)
    }
    IconMenuItem {
        readonly property bool on: (menu.rev, config.widgetExcludeOthers(menu.token))
        text: qsTr("Skip apps pinned in other Selectable apps")
        iconName: "window-duplicate"
        checkable: true
        checked: on
        // Off while the monitor-wide one is on: that is its superset, and
        // setWidgetExcludeMonitor() is what keeps them from running at once.
        enabled: !menu.onlyPinned && !menu.excludeMonitor
        onTriggered: config.setWidgetExcludeOthers(menu.token, !on)
    }
    IconMenuItem {
        readonly property bool on: (menu.rev, config.widgetUngroupWindows(menu.token))
        text: qsTr("Ungroup windows")
        iconName: "window-duplicate"
        checkable: true
        checked: on
        // A widget can only ungroup further: with the dock-wide grouping off
        // there is nothing left to ungroup.
        enabled: config.groupWindows
        onTriggered: config.setWidgetUngroupWindows(menu.token, !on)
    }
    MenuSeparator {}
    IconMenuItem {
        text: qsTr("Recargar este widget")
        iconName: "view-refresh"
        onTriggered: config.reloadAppsWidget(menu.token)
    }
}
