// Saved connections and nearby Wi-Fi, on NetworkControl — the same backend the
// dock's network widget uses, so this and the dock always agree.
//
// Access-point enumeration costs one D-Bus round trip per AP, so it is switched
// on only while this card is on screen (the backend has a flag for exactly that).

import QtQuick
import QtQuick.Controls.Basic
import ".."

Item {
    id: card

    property bool compact: false

    property int rev: 0
    Connections {
        target: network
        function onChanged() { card.rev++ }
    }

    readonly property var connections: { card.rev; return network ? network.connections() : [] }
    readonly property var accessPoints: { card.rev; return network ? network.accessPoints() : [] }

    // The full view lists the nearby networks; the compact card does not, so it
    // must not pay for them either.
    Component.onCompleted: if (network && !card.compact) network.setApTrackingEnabled(true)
    Component.onDestruction: if (network && !card.compact) network.setApTrackingEnabled(false)

    Text {
        anchors.centerIn: parent
        visible: !network || !network.available
        text: qsTr("NetworkManager no responde")
        color: theme.foreground
        opacity: 0.6
        font.pixelSize: 12
    }

    // --- compact ------------------------------------------------------------
    Column {
        anchors.fill: parent
        anchors.margins: 4
        spacing: 6
        visible: card.compact && network && network.available

        Row {
            width: parent.width
            spacing: 8
            Image {
                anchors.verticalCenter: parent.verticalCenter
                width: 22
                height: 22
                source: network ? "image://icon/" + network.iconName + win.iconSuffix : ""
                sourceSize: Qt.size(22 * Screen.devicePixelRatio, 22 * Screen.devicePixelRatio)
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width - 30
                text: network && network.primaryName.length > 0 ? network.primaryName
                                                                : qsTr("Sin conexión")
                color: theme.foreground
                elide: Text.ElideRight
                font.pixelSize: 12
                font.bold: cmConfig.labelBold
            }
        }

        Row {
            spacing: 6
            CmButton {
                compact: true
                icon: "network-wireless"
                label: qsTr("Wi-Fi")
                visible: network ? network.wifiAvailable : false
                checked: network ? network.wifiEnabled : false
                onClicked: network.setWifiEnabled(!network.wifiEnabled)
            }
            CmButton {
                compact: true
                icon: "network-wired"
                label: qsTr("Redes")
                onClicked: win.currentTab = "network"
            }
        }
    }

    // --- full ---------------------------------------------------------------
    Flickable {
        anchors.fill: parent
        visible: !card.compact && network && network.available
        contentWidth: width
        contentHeight: full.height
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}

        Column {
            id: full
            width: parent.width
            spacing: 10

            Row {
                spacing: 6
                CmButton {
                    icon: "network-wireless"
                    label: qsTr("Wi-Fi")
                    visible: network ? network.wifiAvailable : false
                    checked: network ? network.wifiEnabled : false
                    onClicked: network.setWifiEnabled(!network.wifiEnabled)
                }
                CmButton {
                    icon: "view-refresh"
                    label: qsTr("Buscar redes")
                    visible: network ? network.wifiAvailable : false
                    enabled: network ? (network.wifiEnabled && !network.scanning) : false
                    onClicked: network.requestScan()
                }
            }

            Column {
                width: parent.width
                spacing: 3
                visible: card.connections.length > 0
                Text {
                    text: qsTr("Conexiones guardadas")
                    color: theme.foreground
                    opacity: 0.6
                    font.pixelSize: 11
                    font.bold: true
                }
                Repeater {
                    model: card.connections
                    delegate: Item {
                        id: connRow
                        required property var modelData
                        width: full.width
                        height: 28

                        Rectangle {
                            anchors.fill: parent
                            radius: 4
                            color: connMouse.containsMouse
                                   ? Qt.rgba(theme.foreground.r, theme.foreground.g,
                                             theme.foreground.b, 0.10)
                                   : "transparent"
                        }
                        Image {
                            id: connIcon
                            anchors.left: parent.left
                            anchors.leftMargin: 4
                            anchors.verticalCenter: parent.verticalCenter
                            width: 16
                            height: 16
                            source: "image://icon/"
                                    + (connRow.modelData.wifi ? "network-wireless" : "network-wired")
                                    + win.iconSuffix
                            sourceSize: Qt.size(16 * Screen.devicePixelRatio,
                                                16 * Screen.devicePixelRatio)
                            opacity: connRow.modelData.active ? 1.0 : 0.55
                        }
                        Text {
                            anchors.left: connIcon.right
                            anchors.leftMargin: 8
                            anchors.right: parent.right
                            anchors.rightMargin: 4
                            anchors.verticalCenter: parent.verticalCenter
                            text: connRow.modelData.id
                            color: theme.foreground
                            opacity: connRow.modelData.active ? 1.0 : 0.7
                            elide: Text.ElideRight
                            font.pixelSize: 11
                            font.bold: connRow.modelData.active && cmConfig.labelBold
                        }
                        MouseArea {
                            id: connMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: {
                                if (connRow.modelData.active)
                                    network.deactivate(connRow.modelData.activePath)
                                else
                                    network.activate(connRow.modelData.path)
                            }
                        }
                    }
                }
            }

            Column {
                width: parent.width
                spacing: 3
                visible: card.accessPoints.length > 0
                Text {
                    text: qsTr("Redes cercanas")
                    color: theme.foreground
                    opacity: 0.6
                    font.pixelSize: 11
                    font.bold: true
                }
                Repeater {
                    model: card.accessPoints
                    delegate: Item {
                        id: apRow
                        required property var modelData
                        width: full.width
                        height: 26

                        Text {
                            anchors.left: parent.left
                            anchors.leftMargin: 4
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width - 90
                            text: apRow.modelData.ssid
                            color: theme.foreground
                            opacity: apRow.modelData.active ? 1.0 : 0.75
                            elide: Text.ElideRight
                            font.pixelSize: 11
                        }
                        Text {
                            anchors.right: parent.right
                            anchors.rightMargin: 4
                            anchors.verticalCenter: parent.verticalCenter
                            text: apRow.modelData.strength + " %"
                                  + (apRow.modelData.secure ? " 🔒" : "")
                            color: theme.foreground
                            opacity: 0.5
                            font.pixelSize: 10
                        }
                        // Deliberately no click-to-join here: an open network
                        // would connect with no confirmation and leave a saved
                        // connection behind. Joining a new one belongs in the
                        // dock's popup, which has the password row.
                    }
                }
            }
        }
    }
}
