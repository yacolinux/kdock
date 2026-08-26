// NeonMenu.qml - reusable right-click submenu for the neon glow. Uses the global
// context properties (config, theme). Reused by the app-icon context menu and
// the widget-section menu in Dock.qml, the same way ModeMenu/BackgroundColorMenu
// are.
//
// Three things, none of them duplicated in the Settings dialog on purpose:
//  - five quick colors (config.neonPresetColors) -> setNeonColorGlobal(), which
//    is a global static, so a colour change hits every dock;
//  - the global on/off master (config.neonEnabledGlobal), also global;
//  - a per-dock opt-out (config.neonDockDisabled) that wins over both.

import QtQuick
import QtQuick.Controls

Menu {
    id: neonMenu
    title: qsTr("Neon")
    // Read by SubMenuDelegate to draw the row that opens this submenu.
    property string menuIcon: "draw-highlight"
    popupType: Popup.Window
    // Same width floor fix as the other custom submenus: implicitWidth is the
    // background floor, not the widest row.
    width: Math.max(implicitWidth + 64, 240)

    // One swatch per fixed neon colour. Selecting sets the global neon colour.
    Instantiator {
        model: config.neonPresetColors
        delegate: MenuItem {
            id: presetItem
            required property int index
            required property string modelData
            readonly property color swatch: modelData
            checkable: true
            checked: Qt.colorEqual(config.neonColor, swatch)
            onTriggered: config.setNeonColorGlobal(swatch)
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
        onObjectAdded: (i, o) => neonMenu.insertItem(i, o)
        onObjectRemoved: (i, o) => neonMenu.removeItem(o)
    }

    MenuSeparator {}

    // Global master (every dock).
    IconMenuItem {
        text: qsTr("Neon activado (todos los docks)")
        iconName: "draw-highlight"
        checkable: true
        checked: config.neonEnabledGlobal
        onTriggered: config.setNeonEnabledGlobal(!config.neonEnabledGlobal)
    }
    // Per-dock opt-out: wins over the master and the dark-mode link.
    IconMenuItem {
        text: qsTr("Desactivar solo en este dock")
        iconName: "dialog-cancel"
        checkable: true
        checked: config.neonDockDisabled
        onTriggered: config.neonDockDisabled = !config.neonDockDisabled
    }
}
