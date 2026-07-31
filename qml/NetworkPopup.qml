// NetworkPopup.qml - network status and saved-connection list (NetworkManager).
// Header: primary connection name + a Wi-Fi on/off toggle. List: saved
// connections (active first); click to activate, or disconnect an active one.
// Non-modal (no text input, so no keyboard grab needed). Modeled on DisksPopup.

import QtQuick
import QtQuick.Controls.Basic

Popup {
    id: popup

    required property var theme
    required property var config

    popupType: Popup.Window
    focus: false
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    padding: 8
    width: 320
    height: Math.max(120, Math.min(440, 84 + connList.count * 44))

    property int refreshTick: 0
    readonly property var listModel: {
        refreshTick // dependency
        return network ? network.connections() : []
    }

    Connections {
        target: network
        function onChanged() { popup.refreshTick++ }
    }

    background: Rectangle {
        radius: 12
        color: Qt.rgba(theme.background.r, theme.background.g, theme.background.b,
                       Math.max(0.96, config.opacity))
        border.width: 1
        border.color: Qt.rgba(theme.foreground.r, theme.foreground.g, theme.foreground.b, 0.25)
    }

    contentItem: Item {
        Column {
            anchors.fill: parent
            spacing: 8

            // --- Header: status + Wi-Fi toggle ---
            Row {
                width: parent.width
                spacing: 8
                Image {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 24; height: 24
                    source: "image://icon/" + (network ? network.iconName : "network-offline")
                            + "@" + theme.revision
                    sourceSize: Qt.size(24 * Screen.devicePixelRatio, 24 * Screen.devicePixelRatio)
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - 24 - wifiToggle.width - 16
                    text: network && network.primaryName ? network.primaryName
                                                          : qsTr("Sin conexión")
                    color: theme.foreground
                    elide: Text.ElideRight
                }
                Switch {
                    id: wifiToggle
                    anchors.verticalCenter: parent.verticalCenter
                    checked: network ? network.wifiEnabled : false
                    onToggled: network.setWifiEnabled(checked)
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Wi-Fi")
                }
            }

            Rectangle {
                width: parent.width; height: 1
                color: Qt.rgba(theme.foreground.r, theme.foreground.g, theme.foreground.b, 0.15)
            }

            // --- Saved connections ---
            ListView {
                id: connList
                width: parent.width
                height: parent.height - 32 - 1 - parent.spacing * 2
                clip: true
                model: popup.listModel
                spacing: 2
                ScrollBar.vertical: ScrollBar {}

                delegate: ItemDelegate {
                    id: row
                    required property var modelData
                    width: ListView.view.width
                    height: 42
                    onClicked: {
                        if (row.modelData.active)
                            network.deactivate(row.modelData.activePath)
                        else
                            network.activate(row.modelData.path)
                    }

                    background: Rectangle {
                        radius: 6
                        color: row.hovered
                               ? Qt.rgba(theme.highlight.r, theme.highlight.g, theme.highlight.b, 0.25)
                               : "transparent"
                    }

                    contentItem: Row {
                        spacing: 8
                        Image {
                            anchors.verticalCenter: parent.verticalCenter
                            width: 22; height: 22
                            source: "image://icon/"
                                    + (row.modelData.wifi ? "network-wireless" : "network-wired")
                                    + "@" + theme.revision
                            sourceSize: Qt.size(22 * Screen.devicePixelRatio, 22 * Screen.devicePixelRatio)
                            opacity: row.modelData.active ? 1.0 : 0.6
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            width: row.width - 22 - stateLabel.width - 24
                            text: row.modelData.id
                            color: theme.foreground
                            font.bold: row.modelData.active
                            elide: Text.ElideRight
                        }
                        Text {
                            id: stateLabel
                            anchors.verticalCenter: parent.verticalCenter
                            text: row.modelData.active ? qsTr("Conectado") : ""
                            color: theme.highlight
                            font.pixelSize: 11
                        }
                    }
                }

                Text {
                    anchors.centerIn: parent
                    visible: connList.count === 0
                    text: qsTr("No hay conexiones guardadas")
                    color: Qt.rgba(theme.foreground.r, theme.foreground.g, theme.foreground.b, 0.5)
                }
            }
        }
    }
}
