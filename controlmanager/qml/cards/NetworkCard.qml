// Nearby Wi-Fi, saved connections and the details of what is connected, on
// NetworkControl — the same backend the dock's network widget uses, so this and
// the dock always agree. The full view mirrors that widget on purpose, tab for
// tab: joining a network (password included), activating a saved connection,
// and the address/netmask/gateway/DNS/MAC of every active device.
//
// The compact 2x2 card cannot hold any of that: it stays a summary plus the
// button that jumps to this section.
//
// Access-point enumeration costs one D-Bus round trip per AP, so it is switched
// on only while this card is on screen (the backend has a flag for exactly that).

import QtQuick
import QtQuick.Controls.Basic
import ".."

Item {
    id: card

    property bool compact: false
    // Text colour, pushed in by CmSectionView: the card's contrast answer on
    // Principal, the panel's on a full tab. Every text of this file uses it.
    property color fg: theme.foreground

    property int rev: 0
    Connections {
        target: network
        function onChanged() { card.rev++ }
    }

    // 0 Wi-Fi, 1 guardadas, 2 detalles. Same three tabs as the dock's widget.
    property int tab: 0
    // SSID whose password row is open (empty: none).
    property string pendingSsid: ""
    property string errorText: ""

    readonly property var connections: { card.rev; return network ? network.connections() : [] }
    readonly property var accessPoints: { card.rev; return network ? network.accessPoints() : [] }
    // Several D-Bus round trips per device, so only while its tab is up.
    readonly property var details: {
        card.rev
        if (!network || card.compact || card.tab !== 2) return []
        return network.deviceDetails()
    }
    // Saved connections that are not already in the Wi-Fi list, so a network in
    // range does not appear twice (same filter as the dock's popup).
    readonly property var savedElsewhere: {
        card.rev
        if (!network) return []
        const nearby = {}
        for (const ap of card.accessPoints)
            if (ap.connPath) nearby[ap.connPath] = true
        return card.connections.filter(c => !nearby[c.path])
    }

    Connections {
        target: network
        function onErrorOccurred(message) { card.errorText = message }
    }

    // One "Label: value" line of the Detalles tab.
    component DetailRow: Row {
        id: detail
        required property string label
        required property string value
        visible: detail.value.length > 0
        spacing: 6
        Text {
            width: 96
            text: detail.label
            color: card.fg
            opacity: 0.6
            font.pixelSize: Math.max(7, Math.round((11) * cmConfig.fontScale))
        }
        TextEdit {
            width: detail.parent ? detail.parent.width - 102 : 200
            text: detail.value
            color: card.fg
            font.pixelSize: Math.max(7, Math.round((11) * cmConfig.fontScale))
            readOnly: true
            selectByMouse: true
            wrapMode: Text.WrapAnywhere
        }
    }

    // The full view lists the nearby networks; the compact card does not, so it
    // must not pay for them either.
    Component.onCompleted: if (network && !card.compact) network.setApTrackingEnabled(true)
    Component.onDestruction: if (network && !card.compact) network.setApTrackingEnabled(false)

    Text {
        anchors.centerIn: parent
        visible: !network || !network.available
        text: qsTr("NetworkManager no responde")
        color: card.fg
        opacity: 0.6
        font.pixelSize: Math.max(7, Math.round((12) * cmConfig.fontScale))
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
                color: card.fg
                elide: Text.ElideRight
                font.pixelSize: Math.max(7, Math.round((12) * cmConfig.fontScale))
                font.bold: cmConfig.labelBold
            }
        }

        Row {
            spacing: 6
            CmButton {
                fg: card.fg
                compact: true
                icon: "network-wireless"
                label: qsTr("Wi-Fi")
                visible: network ? network.wifiAvailable : false
                checked: network ? network.wifiEnabled : false
                onClicked: network.setWifiEnabled(!network.wifiEnabled)
            }
            CmButton {
                fg: card.fg
                compact: true
                icon: "network-wired"
                label: qsTr("Redes")
                onClicked: win.currentTab = "network"
            }
        }
    }

    // --- full ---------------------------------------------------------------
    Item {
        anchors.fill: parent
        visible: !card.compact && network && network.available

        // Mini-tabs + the error NetworkManager last reported: everything that
        // stays put while the view below changes.
        Column {
            id: head
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            spacing: 6

            Row {
                spacing: 6
                CmButton {
                    fg: card.fg
                    icon: "network-wireless"
                    label: qsTr("Wi-Fi")
                    checked: card.tab === 0
                    onClicked: card.tab = 0
                }
                CmButton {
                    fg: card.fg
                    icon: "network-wired"
                    label: qsTr("Guardadas")
                    checked: card.tab === 1
                    onClicked: card.tab = 1
                }
                CmButton {
                    fg: card.fg
                    icon: "documentinfo"
                    label: qsTr("Detalles")
                    checked: card.tab === 2
                    onClicked: card.tab = 2
                }
            }

            // A silent failure (wrong password, missing authorization) is what
            // makes this feel broken; click to dismiss.
            Rectangle {
                width: parent.width
                height: visible ? errorLabel.implicitHeight + 8 : 0
                visible: card.errorText.length > 0
                radius: 4
                color: Qt.rgba(0.8, 0.2, 0.2, 0.25)
                Text {
                    id: errorLabel
                    anchors.fill: parent
                    anchors.margins: 4
                    text: card.errorText
                    color: card.fg
                    font.pixelSize: Math.max(7, Math.round((11) * cmConfig.fontScale))
                    wrapMode: Text.Wrap
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: card.errorText = ""
                }
            }
        }

        // The full connection editor (static IP, DNS, routes) lives in the
        // dock's settings dialog and in no other process, so this is a call over
        // org.kdock.Dock and it greys out when no dock answers.
        CmButton {
            id: configBtn
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            fg: card.fg
            icon: "configure"
            label: qsTr("Configurar redes…")
            enabled: dock ? dock.available : false
            tip: enabled ? "" : qsTr("kdock no responde")
            onClicked: dock.openNetworkSettings()
        }

        Flickable {
            anchors.top: head.bottom
            anchors.topMargin: 8
            anchors.bottom: configBtn.top
            anchors.bottomMargin: 8
            anchors.left: parent.left
            anchors.right: parent.right
            contentWidth: width
            contentHeight: full.height
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}

            Column {
                id: full
                width: parent.width
                spacing: 10

                // --- Wi-Fi tab ------------------------------------------------
                Column {
                    width: parent.width
                    spacing: 3
                    visible: card.tab === 0

                    Row {
                        width: parent.width
                        spacing: 6
                        CmButton {
                            fg: card.fg
                            compact: true
                            icon: "network-wireless"
                            label: network && network.wifiEnabled ? qsTr("Wi-Fi encendido")
                                                                  : qsTr("Wi-Fi apagado")
                            visible: network ? network.wifiAvailable : false
                            checked: network ? network.wifiEnabled : false
                            onClicked: network.setWifiEnabled(!network.wifiEnabled)
                        }
                        CmButton {
                            fg: card.fg
                            compact: true
                            icon: "view-refresh"
                            label: network && network.scanning ? qsTr("Buscando…")
                                                               : qsTr("Buscar redes")
                            visible: network ? network.wifiAvailable : false
                            enabled: network ? (network.wifiEnabled && !network.scanning) : false
                            onClicked: network.requestScan()
                        }
                    }

                    Repeater {
                        model: card.accessPoints
                        delegate: Column {
                            id: apRow
                            required property var modelData
                            width: full.width
                            spacing: 2

                            Item {
                                width: parent.width
                                height: 26

                                Rectangle {
                                    anchors.fill: parent
                                    radius: 4
                                    color: apMouse.containsMouse
                                           ? Qt.rgba(card.fg.r, card.fg.g, card.fg.b, 0.10)
                                           : "transparent"
                                }
                                Text {
                                    anchors.left: parent.left
                                    anchors.leftMargin: 4
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: parent.width - 130
                                    text: apRow.modelData.ssid
                                    color: card.fg
                                    opacity: apRow.modelData.active ? 1.0 : 0.75
                                    elide: Text.ElideRight
                                    font.bold: apRow.modelData.active && cmConfig.labelBold
                                    font.pixelSize: Math.max(7, Math.round((11) * cmConfig.fontScale))
                                }
                                Text {
                                    anchors.right: parent.right
                                    anchors.rightMargin: 4
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: (apRow.modelData.active ? qsTr("Conectado") + "  ·  "
                                           : apRow.modelData.saved ? qsTr("Guardada") + "  ·  " : "")
                                          + apRow.modelData.strength + " %"
                                          + (apRow.modelData.secure ? " 🔒" : "")
                                    color: apRow.modelData.active ? theme.highlight : card.fg
                                    opacity: apRow.modelData.active ? 1.0 : 0.5
                                    font.pixelSize: Math.max(7, Math.round((10) * cmConfig.fontScale))
                                }
                                // Joining lives here now, exactly as in the dock's
                                // widget: a saved network activates, a secured one
                                // opens the password row below, and an open one
                                // asks first — connecting with no confirmation
                                // would leave a saved connection behind.
                                MouseArea {
                                    id: apMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                                    onClicked: (mouse) => {
                                        if (mouse.button === Qt.RightButton) {
                                            if (apRow.modelData.saved
                                                && win.confirm(qsTr("Olvidar la red"),
                                                               qsTr("¿Borrar la conexión guardada de %1?")
                                                                   .arg(apRow.modelData.ssid)))
                                                network.forgetConnection(apRow.modelData.connPath)
                                            return
                                        }
                                        card.errorText = ""
                                        if (apRow.modelData.saved) {
                                            card.pendingSsid = ""
                                            network.connectToAccessPoint(apRow.modelData.ssid, "",
                                                                         apRow.modelData.path)
                                            return
                                        }
                                        if (!apRow.modelData.secure) {
                                            if (win.confirm(qsTr("Red abierta"),
                                                            qsTr("%1 no tiene contraseña. ¿Conectarse igual?")
                                                                .arg(apRow.modelData.ssid)))
                                                network.connectToAccessPoint(apRow.modelData.ssid, "",
                                                                             apRow.modelData.path)
                                            return
                                        }
                                        card.pendingSsid = card.pendingSsid === apRow.modelData.ssid
                                                           ? "" : apRow.modelData.ssid
                                    }
                                }
                            }

                            // Password row, under the network it belongs to.
                            Row {
                                width: parent.width
                                height: visible ? 34 : 0
                                visible: card.pendingSsid === apRow.modelData.ssid
                                spacing: 6

                                TextField {
                                    id: pskField
                                    width: parent.width - joinButton.width - 6
                                    height: 30
                                    echoMode: TextInput.Password
                                    placeholderText: qsTr("Contraseña de %1").arg(apRow.modelData.ssid)
                                    color: card.fg
                                    placeholderTextColor: Qt.rgba(card.fg.r, card.fg.g, card.fg.b, 0.5)
                                    font.pixelSize: Math.max(7, Math.round((11) * cmConfig.fontScale))
                                    background: Rectangle {
                                        radius: 6
                                        color: Qt.rgba(card.fg.r, card.fg.g, card.fg.b, 0.08)
                                        border.width: pskField.activeFocus ? 1 : 0
                                        border.color: theme.highlight
                                    }
                                    onVisibleChanged: if (visible) focusRetry.restart()
                                    onAccepted: joinButton.clicked()
                                }
                                CmButton {
                                    id: joinButton
                                    fg: card.fg
                                    compact: true
                                    label: qsTr("Conectar")
                                    enabled: pskField.text.length >= 8
                                             || pskField.text.length === 5 // WEP passphrase
                                    onClicked: {
                                        network.connectToAccessPoint(apRow.modelData.ssid,
                                                                     pskField.text,
                                                                     apRow.modelData.path)
                                        card.pendingSsid = ""
                                        pskField.text = ""
                                    }
                                }

                                // The panel's keyboard grab is double-buffered:
                                // the field only takes focus once the compositor
                                // has applied it.
                                Timer {
                                    id: focusRetry
                                    interval: 60
                                    onTriggered: pskField.forceActiveFocus()
                                }
                            }
                        }
                    }

                    Text {
                        width: full.width
                        visible: card.accessPoints.length === 0
                        text: network && network.wifiAvailable
                              ? (network.wifiEnabled ? qsTr("No se ven redes cercanas")
                                                     : qsTr("El Wi-Fi está apagado"))
                              : qsTr("Este equipo no tiene Wi-Fi")
                        color: card.fg
                        opacity: 0.5
                        wrapMode: Text.Wrap
                        font.pixelSize: Math.max(7, Math.round((11) * cmConfig.fontScale))
                    }
                }

                // --- Guardadas tab --------------------------------------------
                Column {
                    width: parent.width
                    spacing: 3
                    visible: card.tab === 1

                    Repeater {
                        model: card.tab === 1 ? card.savedElsewhere : []
                        delegate: Item {
                            id: connRow
                            required property var modelData
                            width: full.width
                            height: 28

                            Rectangle {
                                anchors.fill: parent
                                radius: 4
                                color: connMouse.containsMouse
                                       ? Qt.rgba(card.fg.r, card.fg.g, card.fg.b, 0.10)
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
                                anchors.right: connState.left
                                anchors.rightMargin: 6
                                anchors.verticalCenter: parent.verticalCenter
                                text: connRow.modelData.id
                                color: card.fg
                                opacity: connRow.modelData.active ? 1.0 : 0.7
                                elide: Text.ElideRight
                                font.pixelSize: Math.max(7, Math.round((11) * cmConfig.fontScale))
                                font.bold: connRow.modelData.active && cmConfig.labelBold
                            }
                            Text {
                                id: connState
                                anchors.right: parent.right
                                anchors.rightMargin: 4
                                anchors.verticalCenter: parent.verticalCenter
                                text: connRow.modelData.active ? qsTr("Conectado") : ""
                                color: theme.highlight
                                font.pixelSize: Math.max(7, Math.round((10) * cmConfig.fontScale))
                            }
                            MouseArea {
                                id: connMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: {
                                    card.errorText = ""
                                    if (connRow.modelData.active)
                                        network.deactivate(connRow.modelData.activePath)
                                    else
                                        network.activate(connRow.modelData.path)
                                }
                            }
                        }
                    }

                    Text {
                        width: full.width
                        visible: card.savedElsewhere.length === 0
                        text: qsTr("No hay más conexiones guardadas que las redes en alcance")
                        color: card.fg
                        opacity: 0.5
                        wrapMode: Text.Wrap
                        font.pixelSize: Math.max(7, Math.round((11) * cmConfig.fontScale))
                    }
                }

                // --- Detalles tab ---------------------------------------------
                Column {
                    id: detailsView
                    width: parent.width
                    spacing: 8
                    visible: card.tab === 2

                    Repeater {
                        model: card.details
                        delegate: Column {
                            id: devBox
                            required property var modelData
                            width: detailsView.width
                            spacing: 2

                            Row {
                                spacing: 6
                                Image {
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 16; height: 16
                                    source: "image://icon/"
                                            + (devBox.modelData.wifi ? "network-wireless"
                                                                     : "network-wired")
                                            + win.iconSuffix
                                    sourceSize: Qt.size(16 * Screen.devicePixelRatio,
                                                        16 * Screen.devicePixelRatio)
                                }
                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: devBox.modelData.connection + "  ·  " + devBox.modelData.iface
                                    color: card.fg
                                    opacity: 0.6
                                    font.bold: true
                                    font.pixelSize: Math.max(7, Math.round((11) * cmConfig.fontScale))
                                }
                            }

                            DetailRow { label: qsTr("Tipo"); value: devBox.modelData.typeLabel
                                        + " (" + devBox.modelData.stateLabel + ")" }
                            DetailRow { label: qsTr("Dirección IP"); value: devBox.modelData.ip4 }
                            DetailRow { label: qsTr("Máscara"); value: devBox.modelData.mask }
                            DetailRow { label: qsTr("Otras IPv4"); value: devBox.modelData.extraIp4 }
                            DetailRow { label: qsTr("Puerta de enlace"); value: devBox.modelData.gateway }
                            DetailRow { label: qsTr("DNS"); value: devBox.modelData.dns }
                            DetailRow { label: qsTr("IPv6"); value: devBox.modelData.ip6 }
                            DetailRow { label: qsTr("MAC"); value: devBox.modelData.mac }

                            Rectangle {
                                width: detailsView.width; height: 1
                                color: Qt.rgba(card.fg.r, card.fg.g, card.fg.b, 0.12)
                            }
                        }
                    }

                    Text {
                        width: detailsView.width
                        visible: card.details.length === 0
                        text: qsTr("Ninguna conexión activa")
                        color: card.fg
                        opacity: 0.5
                        font.pixelSize: Math.max(7, Math.round((11) * cmConfig.fontScale))
                    }
                }
            }
        }
    }
}
