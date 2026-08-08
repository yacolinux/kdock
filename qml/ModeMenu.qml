// ModeMenu.qml - reusable right-click submenu that switches the dock between
// the normal color scheme and dark mode. Uses the global context property
// `config`. Reused by the app-icon context menu and the widget-section menu in
// Dock.qml, the same way BackgroundColorMenu is.
//
// Both items go through config.setDarkModeActive(), which writes wherever the
// effective value lives (this dock's flag, or the exception list when dark mode
// is switched on app-wide) — so what the menu shows is always what it changes.

import QtQuick
import QtQuick.Controls.Basic

Menu {
    title: qsTr("Modo")
    // Read by SubMenuDelegate to draw the row that opens this submenu.
    property string menuIcon: "contrast"
    popupType: Popup.Window

    IconMenuItem {
        text: qsTr("Normal")
        iconName: "weather-clear"
        checkable: true
        checked: !config.darkModeActive
        onTriggered: config.setDarkModeActive(false)
    }
    IconMenuItem {
        text: qsTr("Dark")
        iconName: "weather-clear-night"
        checkable: true
        checked: config.darkModeActive
        onTriggered: config.setDarkModeActive(true)
    }
}
