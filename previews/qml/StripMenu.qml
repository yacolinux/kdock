// Right-click menu of a strip: the handful of settings worth reaching without
// opening the panel, plus the panel itself.
//
// Every Menu carries `popupType: Popup.Window` — without it a popup opened from a
// layer-shell surface is clipped to that surface on Wayland (trap documented in
// CLAUDE.md).

import QtQuick
import QtQuick.Controls.Basic

Menu {
    id: menu
    popupType: Popup.Window

    Menu {
        title: qsTr("Mover a")
        popupType: Popup.Window

        MenuItem {
            text: qsTr("Arriba")
            checkable: true
            checked: config.edge === 1
            onTriggered: config.edge = 1
        }
        MenuItem {
            text: qsTr("Abajo")
            checkable: true
            checked: config.edge === 0
            onTriggered: config.edge = 0
        }
        MenuItem {
            text: qsTr("Izquierda")
            checkable: true
            checked: config.edge === 2
            onTriggered: config.edge = 2
        }
        MenuItem {
            text: qsTr("Derecha")
            checkable: true
            checked: config.edge === 3
            onTriggered: config.edge = 3
        }
    }

    Menu {
        id: colorMenu
        title: qsTr("Color de fondo")
        popupType: Popup.Window

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
                        width: 22
                        height: 22
                        radius: 4
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
            onObjectAdded: (i, o) => colorMenu.insertItem(i, o)
            onObjectRemoved: (i, o) => colorMenu.removeItem(o)
        }

        MenuSeparator {}

        MenuItem {
            text: qsTr("Predeterminado (tema)")
            checkable: true
            checked: !config.panelColorSet
            onTriggered: config.resetPanelColor()
        }
    }

    MenuSeparator {}

    MenuItem {
        text: qsTr("Mostrar títulos")
        checkable: true
        checked: config.showTitles
        onTriggered: config.showTitles = !config.showTitles
    }
    MenuItem {
        text: qsTr("Reservar espacio en pantalla")
        checkable: true
        checked: config.reserveSpace
        onTriggered: config.reserveSpace = !config.reserveSpace
    }
    MenuItem {
        text: qsTr("Ocultar automáticamente")
        checkable: true
        checked: config.autohide
        onTriggered: config.autohide = !config.autohide
    }

    MenuSeparator {}

    MenuItem {
        text: qsTr("Configurar…")
        onTriggered: previewWindow.openSettings()
    }
    MenuItem {
        text: qsTr("Reiniciar")
        onTriggered: previewWindow.restart()
    }
    MenuItem {
        text: qsTr("Salir")
        onTriggered: previewWindow.quit()
    }
}
