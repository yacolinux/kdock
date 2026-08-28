// Controls the screensaver on each connected monitor. The actual surface is
// owned by kdock, so both the control panel and the desktop canvas use this
// same per-monitor D-Bus entry point.

import QtQuick

Item {
    id: card

    property bool compact: false
    property color fg: theme.foreground
    readonly property var screenList: Qt.application.screens
    readonly property bool available: typeof dock !== "undefined"
                                      && dock && dock.available

    Column {
        anchors.fill: parent
        anchors.margins: 2
        spacing: 6

        Text {
            visible: !card.compact
            text: qsTr("Salvapantallas")
            color: card.fg
            opacity: 0.6
            font.pixelSize: Math.max(7, Math.round((11) * cmConfig.fontScale))
            font.bold: true
        }

        Flow {
            width: parent.width
            spacing: 6

            Repeater {
                model: card.screenList
                delegate: Column {
                    required property var modelData
                    spacing: 3

                    Text {
                        text: modelData.name
                        color: card.fg
                        opacity: 0.7
                        font.pixelSize: Math.max(7, Math.round((10) * cmConfig.fontScale))
                        font.bold: true
                    }

                    Row {
                        spacing: 4

                        CmButton {
                            fg: card.fg
                            compact: card.compact
                            icon: "view-preview"
                            label: qsTr("Slideshow")
                            tip: qsTr("Activar Screensaver")
                            enabled: card.available
                            onClicked: dock.activateScreensaver(modelData.name, 0, "")
                        }

                        CmButton {
                            fg: card.fg
                            compact: card.compact
                            icon: "preferences-desktop-screensaver"
                            label: qsTr("After Dark CSS")
                            tip: qsTr("Activar Screensaver")
                            enabled: card.available
                            onClicked: dock.activateScreensaver(modelData.name, 1, "")
                        }
                    }
                }
            }
        }
    }
}
