// BackgroundColorMenu.qml - reusable right-click submenu that switches the dock
// background color between the user-configurable quick colors (shared by every
// dock) and the theme default.
// Uses the global context properties (config, theme). Reused by the app-icon
// context menu and the widget-section menu in Dock.qml.

import QtQuick
import QtQuick.Controls

Menu {
    id: bgMenu
    title: qsTr("Color de fondo")
    // Read by SubMenuDelegate to draw the row that opens this submenu.
    property string menuIcon: "color-management"
    popupType: Popup.Window

    // One swatch per entry of config.panelPresetColors.
    Instantiator {
        model: config.panelPresetColors
        delegate: MenuItem {
            id: presetItem
            required property int index
            required property string modelData
            readonly property color swatch: modelData
            checkable: true
            checked: config.panelColorSet && Qt.colorEqual(config.panelColor, swatch)
            onTriggered: config.panelColor = swatch
            contentItem: Row {
                spacing: 8
                Rectangle {
                    width: 22; height: 22; radius: 4
                    color: presetItem.swatch
                    border.width: 1
                    border.color: Qt.rgba(theme.foreground.r, theme.foreground.g,
                                          theme.foreground.b, 0.35)
                    anchors.verticalCenter: parent.verticalCenter
                }
                Text {
                    text: qsTr("Color %1").arg(presetItem.index + 1)
                    color: presetItem.palette.windowText
                    font: presetItem.font
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
        }
        onObjectAdded: (i, o) => bgMenu.insertItem(i, o)
        onObjectRemoved: (i, o) => bgMenu.removeItem(o)
    }

    MenuSeparator {}

    IconMenuItem {
        text: qsTr("Predeterminado (tema)")
        iconName: "edit-undo"
        checkable: true
        checked: !config.panelColorSet
        onTriggered: config.resetPanelColor()
    }
}
