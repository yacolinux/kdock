// IconLabelMenu.qml - reusable right-click submenu that picks how the name of
// an application is drawn next to its dock icon (see DockConfig::IconLabelMode).
// Uses the global context property `config`. Reused by the app-icon context menu
// and the widget-section menu in Dock.qml, like BackgroundColorMenu.

import QtQuick
import QtQuick.Controls

Menu {
    id: labelMenu
    title: qsTr("Texto App")
    // Read by SubMenuDelegate to draw the row that opens this submenu.
    property string menuIcon: "view-list-details"
    popupType: Popup.Window

    IconMenuItem {
        text: qsTr("Solo ícono")
        iconName: "view-list-icons"
        checkable: true
        checked: config.iconLabelMode === 0
        onTriggered: config.iconLabelMode = 0
    }
    IconMenuItem {
        text: qsTr("Nombre sobre el ícono")
        iconName: "view-list-details"
        checkable: true
        checked: config.iconLabelMode === 4
        onTriggered: config.iconLabelMode = 4
    }
    IconMenuItem {
        text: qsTr("Nombre bajo el ícono")
        iconName: "view-list-details"
        checkable: true
        checked: config.iconLabelMode === 1
        onTriggered: config.iconLabelMode = 1
    }
    IconMenuItem {
        text: qsTr("Nombre a la izquierda")
        iconName: "view-list-tree"
        checkable: true
        checked: config.iconLabelMode === 5
        onTriggered: config.iconLabelMode = 5
    }
    IconMenuItem {
        text: qsTr("Nombre a la derecha")
        iconName: "view-list-tree"
        checkable: true
        checked: config.iconLabelMode === 2
        onTriggered: config.iconLabelMode = 2
    }
    IconMenuItem {
        text: qsTr("Solo el nombre")
        iconName: "view-list-text"
        checkable: true
        checked: config.iconLabelMode === 3
        onTriggered: config.iconLabelMode = 3
    }
}
