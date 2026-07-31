// WidgetLabelMenu.qml - right-click submenu that picks how the name of a dock
// section that is *not* an app (widgets and blocks) is drawn around it. Same
// positions as IconLabelMenu, minus "name only": a widget without its icon
// would be unrecognizable. Deliberately a separate setting from the app labels
// (config.widgetLabelMode vs config.iconLabelMode) so a dock can name one and
// not the other. Uses the global context property `config`.

import QtQuick
import QtQuick.Controls.Basic

Menu {
    id: widgetLabelMenu
    title: qsTr("Texto Widget")
    popupType: Popup.Window

    IconMenuItem {
        text: qsTr("Sin nombre")
        iconName: "view-list-icons"
        checkable: true
        checked: config.widgetLabelMode === 0
        onTriggered: config.widgetLabelMode = 0
    }
    IconMenuItem {
        text: qsTr("Nombre sobre el ícono")
        iconName: "view-list-details"
        checkable: true
        checked: config.widgetLabelMode === 4
        onTriggered: config.widgetLabelMode = 4
    }
    IconMenuItem {
        text: qsTr("Nombre bajo el ícono")
        iconName: "view-list-details"
        checkable: true
        checked: config.widgetLabelMode === 1
        onTriggered: config.widgetLabelMode = 1
    }
    IconMenuItem {
        text: qsTr("Nombre a la izquierda")
        iconName: "view-list-tree"
        checkable: true
        checked: config.widgetLabelMode === 5
        onTriggered: config.widgetLabelMode = 5
    }
    IconMenuItem {
        text: qsTr("Nombre a la derecha")
        iconName: "view-list-tree"
        checkable: true
        checked: config.widgetLabelMode === 2
        onTriggered: config.widgetLabelMode = 2
    }
}
