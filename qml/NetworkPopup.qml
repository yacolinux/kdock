// NetworkPopup.qml - a small network window with three mini-tabs, all of them
// on NetworkControl (NetworkManager):
//
//   Wi-Fi      the access points in range, with the scan button; picking a new
//              secured one asks for the password right in the list.
//   Guardadas  the saved connections that are *not* in range (Ethernet,
//              networks elsewhere), to activate or deactivate.
//   Detalles   what the connection actually is: address + netmask, gateway,
//              DNS, MAC, IPv6, per device with an active connection.
//
// The header (primary connection + Wi-Fi toggle) and the "Configurar redes…"
// button — which opens the dock's Redes tab, the full connection editor — sit
// outside the tabs because they belong to all three.
//
// Modal on purpose: the password field needs key events, and the dock's layer
// surface is keyboard-inert until it asks for the grab (same as AppMenuPopup
// and ClipboardPopup).

import QtQuick
import QtQuick.Controls

Popup {
    id: popup

    required property var theme
    required property var config

    popupType: Popup.Window
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    padding: 8
    width: 400
    height: 560

    property int refreshTick: 0
    // SSID whose password row is open (empty: none).
    property string pendingSsid: ""
    property string errorText: ""
    // 0 Wi-Fi, 1 guardadas, 2 detalles.
    property int tab: 0

    readonly property var apModel: {
        refreshTick // dependency
        return network ? network.accessPoints() : []
    }
    // Saved connections that are *not* already listed as a nearby network:
    // Ethernet, and Wi-Fi networks out of range. Showing both lists unfiltered
    // meant every network in range appeared twice.
    readonly property var connModel: {
        refreshTick // dependency
        if (!network) return []
        const nearby = {}
        for (const ap of popup.apModel)
            if (ap.connPath) nearby[ap.connPath] = true
        return network.connections().filter(c => !nearby[c.path])
    }
    // Several D-Bus round trips per device, so it is only asked for while the
    // Detalles tab is the one on screen.
    readonly property var detailModel: {
        refreshTick // dependency
        if (!network || popup.tab !== 2) return []
        return network.deviceDetails()
    }

    Connections {
        target: network
        function onChanged() { popup.refreshTick++ }
        function onErrorOccurred(message) { popup.errorText = message }
    }

    // Enumerating access points is one D-Bus round trip each, so the backend
    // only does it while this popup is open. The keyboard grab is what makes
    // the password field usable.
    onAboutToShow: {
        popup.errorText = ""
        popup.pendingSsid = ""
        if (network) {
            network.setApTrackingEnabled(true)
            network.requestScan()
        }
        dockWindow.setKeyboardInteractive(true)
    }
    onAboutToHide: dockWindow.setKeyboardInteractive(false)
    onClosed: {
        dockWindow.setKeyboardInteractive(false)
        if (network)
            network.setApTrackingEnabled(false)
    }

    background: Rectangle {
        radius: 12
        color: Qt.rgba(theme.background.r, theme.background.g, theme.background.b,
                       Math.max(0.96, config.opacity))
        border.width: 1
        border.color: Qt.rgba(theme.foreground.r, theme.foreground.g, theme.foreground.b, 0.25)
    }

    // Signal strength as four bars, the way every Wi-Fi list draws it.
    component SignalBars: Row {
        id: bars
        required property int strength
        spacing: 2
        Repeater {
            model: 4
            Rectangle {
                required property int index
                width: 3
                height: 4 + index * 3
                anchors.bottom: parent.bottom
                radius: 1
                color: bars.strength >= (index + 1) * 20
                       ? theme.highlight
                       : Qt.rgba(theme.foreground.r, theme.foreground.g, theme.foreground.b, 0.25)
            }
        }
    }

    component SectionLabel: Text {
        color: Qt.rgba(theme.foreground.r, theme.foreground.g, theme.foreground.b, 0.55)
        font.pixelSize: 11
        font.bold: true
    }

    // One mini-tab: a rounded pill, filled with the highlight when current.
    // Same gesture (and same look) as the control panel's own tab bar.
    component TabPill: Item {
        id: pill
        required property string label
        required property int index
        width: Math.max(72, pillText.implicitWidth + 22)
        height: 24

        Rectangle {
            anchors.fill: parent
            radius: height / 2
            color: popup.tab === pill.index
                   ? theme.highlight
                   : pillMouse.containsMouse
                     ? Qt.rgba(theme.foreground.r, theme.foreground.g, theme.foreground.b, 0.12)
                     : "transparent"
            border.width: popup.tab === pill.index ? 0 : 1
            border.color: Qt.rgba(theme.foreground.r, theme.foreground.g, theme.foreground.b, 0.2)
        }
        Text {
            id: pillText
            anchors.centerIn: parent
            text: pill.label
            font.pixelSize: 11
            font.bold: popup.tab === pill.index
            // On the filled pill the theme foreground can be the highlight's own
            // color; luminance decides, the same test the dock makes over its
            // panel color.
            color: popup.tab === pill.index
                   ? ((0.299 * theme.highlight.r + 0.587 * theme.highlight.g
                       + 0.114 * theme.highlight.b) > 0.5 ? "#141414" : "#F2F2F2")
                   : theme.foreground
        }
        MouseArea {
            id: pillMouse
            anchors.fill: parent
            hoverEnabled: true
            onClicked: popup.tab = pill.index
        }
    }

    // One "Label: value" line of the Detalles tab. Selectable on purpose: an IP
    // or a MAC is something people copy.
    component DetailRow: Row {
        id: detail
        required property string label
        required property string value
        visible: detail.value.length > 0
        spacing: 6
        Text {
            width: 78
            text: detail.label
            color: Qt.rgba(theme.foreground.r, theme.foreground.g, theme.foreground.b, 0.55)
            font.pixelSize: 11
        }
        TextEdit {
            width: detail.parent ? detail.parent.width - 84 : 200
            text: detail.value
            color: theme.foreground
            font.pixelSize: 11
            readOnly: true
            selectByMouse: true
            wrapMode: Text.WrapAnywhere
        }
    }

    contentItem: Item {
        // Header, tab pills and error banner: everything that stays put while
        // the tab below changes.
        Column {
            id: headerCol
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
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
                    visible: network && network.wifiAvailable
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

            Row {
                width: parent.width
                spacing: 6
                TabPill { index: 0; label: qsTr("Wi-Fi") }
                TabPill { index: 1; label: qsTr("Guardadas") }
                TabPill { index: 2; label: qsTr("Detalles") }
            }

            // Last failure reported by NetworkManager (wrong password, missing
            // authorization). Silent failures are what make this feel broken.
            Rectangle {
                width: parent.width
                height: visible ? errorLabel.implicitHeight + 8 : 0
                visible: popup.errorText.length > 0
                radius: 4
                color: Qt.rgba(0.8, 0.2, 0.2, 0.25)
                Text {
                    id: errorLabel
                    anchors.fill: parent
                    anchors.margins: 4
                    text: popup.errorText
                    color: theme.foreground
                    font.pixelSize: 11
                    wrapMode: Text.Wrap
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: popup.errorText = ""
                }
            }
        }

        // The full connection editor: it lives in the dock's settings dialog and
        // nowhere else, so this is a call into the window, not a view of ours.
        Button {
            id: configButton
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            height: 30
            text: qsTr("Configurar redes…")
            onClicked: {
                popup.close()
                dockWindow.openNetworkSettings()
            }
        }

        Flickable {
            anchors.top: headerCol.bottom
            anchors.topMargin: 8
            anchors.bottom: configButton.top
            anchors.bottomMargin: 8
            anchors.left: parent.left
            anchors.right: parent.right
            clip: true
            contentHeight: listColumn.height
            ScrollBar.vertical: ScrollBar {}

            Column {
                id: listColumn
                width: parent.width
                spacing: 2

                // --- Wi-Fi tab -------------------------------------------
                Column {
                    width: listColumn.width
                    spacing: 2
                    visible: popup.tab === 0

                    Row {
                        width: listColumn.width
                        visible: network && network.wifiAvailable && network.wifiEnabled
                        SectionLabel {
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width - scanButton.width
                            text: qsTr("REDES CERCANAS")
                        }
                        ToolButton {
                            id: scanButton
                            anchors.verticalCenter: parent.verticalCenter
                            enabled: !(network && network.scanning)
                            text: network && network.scanning ? qsTr("Buscando…") : qsTr("Escanear")
                            font.pixelSize: 11
                            onClicked: network.requestScan()
                            contentItem: Text {
                                text: scanButton.text
                                font: scanButton.font
                                color: scanButton.enabled ? theme.highlight
                                                          : Qt.rgba(theme.foreground.r,
                                                                    theme.foreground.g,
                                                                    theme.foreground.b, 0.4)
                                horizontalAlignment: Text.AlignRight
                                verticalAlignment: Text.AlignVCenter
                            }
                            background: Item {}
                        }
                    }

                    Repeater {
                        model: (network && network.wifiAvailable && network.wifiEnabled)
                               ? popup.apModel : []
                        delegate: Column {
                            id: apRow
                            required property var modelData
                            width: listColumn.width

                            ItemDelegate {
                                id: apItem
                                width: parent.width
                                height: 40
                                onClicked: {
                                    popup.errorText = ""
                                    // Saved or open: connect straight away.
                                    if (apRow.modelData.saved || !apRow.modelData.secure) {
                                        popup.pendingSsid = ""
                                        network.connectToAccessPoint(apRow.modelData.ssid, "",
                                                                     apRow.modelData.path)
                                        return
                                    }
                                    popup.pendingSsid = popup.pendingSsid === apRow.modelData.ssid
                                                        ? "" : apRow.modelData.ssid
                                }

                                background: Rectangle {
                                    radius: 6
                                    color: apItem.hovered
                                           ? Qt.rgba(theme.highlight.r, theme.highlight.g,
                                                     theme.highlight.b, 0.25)
                                           : "transparent"
                                }

                                contentItem: Row {
                                    spacing: 8
                                    SignalBars {
                                        anchors.verticalCenter: parent.verticalCenter
                                        height: 16
                                        strength: apRow.modelData.strength
                                    }
                                    Image {
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: 14; height: 14
                                        visible: apRow.modelData.secure
                                        source: "image://icon/object-locked@" + theme.revision
                                        sourceSize: Qt.size(14 * Screen.devicePixelRatio,
                                                            14 * Screen.devicePixelRatio)
                                        opacity: 0.7
                                    }
                                    Text {
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: apItem.width - 46 - apState.width - 40
                                        text: apRow.modelData.ssid
                                        color: theme.foreground
                                        font.bold: apRow.modelData.active
                                        elide: Text.ElideRight
                                    }
                                    Text {
                                        id: apState
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: apRow.modelData.active ? qsTr("Conectado")
                                              : apRow.modelData.saved ? qsTr("Guardada")
                                                                      : apRow.modelData.band + " GHz"
                                        color: apRow.modelData.active ? theme.highlight
                                               : Qt.rgba(theme.foreground.r, theme.foreground.g,
                                                         theme.foreground.b, 0.5)
                                        font.pixelSize: 11
                                    }
                                }

                                // Right-click: forget a saved network.
                                MouseArea {
                                    anchors.fill: parent
                                    acceptedButtons: Qt.RightButton
                                    enabled: apRow.modelData.saved
                                    onClicked: apMenu.popup()
                                }
                                Menu {
                                    id: apMenu
                                    popupType: Popup.Window
                                    width: Math.max(implicitWidth + 64, 200)
                                    MenuItem {
                                        text: qsTr("Olvidar esta red")
                                        onTriggered: network.forgetConnection(apRow.modelData.connPath)
                                    }
                                }
                            }

                            // Password row, shown under the network it belongs to.
                            Row {
                                width: parent.width
                                height: visible ? 36 : 0
                                visible: popup.pendingSsid === apRow.modelData.ssid
                                spacing: 6

                                TextField {
                                    id: pskField
                                    width: parent.width - joinButton.width - 6
                                    height: 32
                                    echoMode: TextInput.Password
                                    placeholderText: qsTr("Contraseña de %1").arg(apRow.modelData.ssid)
                                    color: theme.foreground
                                    placeholderTextColor: Qt.rgba(theme.foreground.r,
                                                                  theme.foreground.g,
                                                                  theme.foreground.b, 0.5)
                                    background: Rectangle {
                                        radius: 6
                                        color: Qt.rgba(theme.foreground.r, theme.foreground.g,
                                                       theme.foreground.b, 0.08)
                                        border.width: pskField.activeFocus ? 1 : 0
                                        border.color: theme.highlight
                                    }
                                    onVisibleChanged: if (visible) focusRetry.restart()
                                    onAccepted: joinButton.clicked()
                                }
                                Button {
                                    id: joinButton
                                    height: 32
                                    text: qsTr("Conectar")
                                    enabled: pskField.text.length >= 8
                                             || pskField.text.length === 5 // WEP passphrase
                                    onClicked: {
                                        network.connectToAccessPoint(apRow.modelData.ssid,
                                                                     pskField.text,
                                                                     apRow.modelData.path)
                                        popup.pendingSsid = ""
                                        pskField.text = ""
                                    }
                                }

                                // The keyboard grab is double-buffered: the field
                                // only takes focus once the compositor applied it.
                                Timer {
                                    id: focusRetry
                                    interval: 60
                                    onTriggered: pskField.forceActiveFocus()
                                }
                            }
                        }
                    }

                    Text {
                        width: listColumn.width
                        visible: popup.apModel.length === 0
                        text: network && network.wifiAvailable
                              ? (network.wifiEnabled ? qsTr("No se ven redes cercanas")
                                                     : qsTr("El Wi-Fi está apagado"))
                              : qsTr("Este equipo no tiene Wi-Fi")
                        color: Qt.rgba(theme.foreground.r, theme.foreground.g,
                                       theme.foreground.b, 0.5)
                        font.pixelSize: 11
                        wrapMode: Text.Wrap
                    }
                }

                // --- Guardadas tab ---------------------------------------
                Column {
                    width: listColumn.width
                    spacing: 2
                    visible: popup.tab === 1

                    SectionLabel {
                        text: qsTr("CONEXIONES GUARDADAS")
                        visible: popup.connModel.length > 0
                    }

                    Repeater {
                        model: popup.tab === 1 ? popup.connModel : []
                        delegate: ItemDelegate {
                            id: connRow
                            required property var modelData
                            width: listColumn.width
                            height: 40
                            onClicked: {
                                popup.errorText = ""
                                if (connRow.modelData.active)
                                    network.deactivate(connRow.modelData.activePath)
                                else
                                    network.activate(connRow.modelData.path)
                            }

                            background: Rectangle {
                                radius: 6
                                color: connRow.hovered
                                       ? Qt.rgba(theme.highlight.r, theme.highlight.g,
                                                 theme.highlight.b, 0.25)
                                       : "transparent"
                            }

                            contentItem: Row {
                                spacing: 8
                                Image {
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 22; height: 22
                                    source: "image://icon/"
                                            + (connRow.modelData.wifi ? "network-wireless"
                                                                      : "network-wired")
                                            + "@" + theme.revision
                                    sourceSize: Qt.size(22 * Screen.devicePixelRatio,
                                                        22 * Screen.devicePixelRatio)
                                    opacity: connRow.modelData.active ? 1.0 : 0.6
                                }
                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: connRow.width - 22 - connState.width - 34
                                    text: connRow.modelData.id
                                    color: theme.foreground
                                    font.bold: connRow.modelData.active
                                    elide: Text.ElideRight
                                }
                                Text {
                                    id: connState
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: connRow.modelData.active ? qsTr("Conectado") : ""
                                    color: theme.highlight
                                    font.pixelSize: 11
                                }
                            }
                        }
                    }

                    Text {
                        width: listColumn.width
                        visible: popup.connModel.length === 0
                        text: qsTr("No hay más conexiones guardadas que las redes en alcance")
                        color: Qt.rgba(theme.foreground.r, theme.foreground.g, theme.foreground.b, 0.5)
                        font.pixelSize: 11
                        wrapMode: Text.Wrap
                    }
                }

                // --- Detalles tab ----------------------------------------
                Column {
                    id: detailsView
                    width: listColumn.width
                    spacing: 8
                    visible: popup.tab === 2

                    Repeater {
                        model: popup.detailModel
                        delegate: Column {
                            id: devBox
                            required property var modelData
                            width: detailsView.width
                            spacing: 2

                            Row {
                                spacing: 6
                                Image {
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 18; height: 18
                                    source: "image://icon/"
                                            + (devBox.modelData.wifi ? "network-wireless"
                                                                     : "network-wired")
                                            + "@" + theme.revision
                                    sourceSize: Qt.size(18 * Screen.devicePixelRatio,
                                                        18 * Screen.devicePixelRatio)
                                }
                                SectionLabel {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: devBox.modelData.connection + "  ·  "
                                          + devBox.modelData.iface
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
                                color: Qt.rgba(theme.foreground.r, theme.foreground.g,
                                               theme.foreground.b, 0.12)
                            }
                        }
                    }

                    Text {
                        width: detailsView.width
                        visible: popup.detailModel.length === 0
                        text: qsTr("Ninguna conexión activa")
                        color: Qt.rgba(theme.foreground.r, theme.foreground.g, theme.foreground.b, 0.5)
                        font.pixelSize: 11
                    }
                }
            }
        }
    }
}
