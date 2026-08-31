// DisksPopup.qml - list of removable volumes (UDisks2). Each row shows the
// label + mount point/size and a mount/unmount button; ejectable drives get an
// eject button. Clicking a mounted row opens it in the file manager. Modeled on
// the clipboard window but non-modal (no text input, so no keyboard grab needed).

import QtQuick
import QtQuick.Controls

Popup {
    id: popup

    required property var theme
    required property var config

    popupType: Popup.Window
    focus: false
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    padding: 8
    width: 340
    height: Math.max(96, Math.min(420, 56 + volumesList.count * 56))

    // Bumped on disks change to force the model to re-evaluate.
    property int refreshTick: 0
    readonly property var listModel: {
        refreshTick // dependency
        return disks ? disks.volumes() : []
    }

    Connections {
        target: disks
        function onChanged() { popup.refreshTick++ }
    }

    background: Rectangle {
        radius: 12
        color: Qt.rgba(theme.background.r, theme.background.g, theme.background.b,
                       Math.max(0.96, config.opacity))
        border.width: 1
        border.color: Qt.rgba(theme.foreground.r, theme.foreground.g, theme.foreground.b, 0.25)
    }

    function fmtSize(bytes) {
        if (!bytes || bytes <= 0) return ""
        const u = ["B", "KB", "MB", "GB", "TB"]
        let i = 0, s = bytes
        while (s >= 1024 && i < u.length - 1) { s /= 1024; i++ }
        return s.toFixed(s < 10 && i > 0 ? 1 : 0) + " " + u[i]
    }

    contentItem: Item {
        ListView {
            id: volumesList
            anchors.fill: parent
            clip: true
            model: popup.listModel
            spacing: 2
            ScrollBar.vertical: ScrollBar {}

            delegate: ItemDelegate {
                id: row
                required property var modelData
                width: ListView.view.width
                height: 54
                // Clicking a mounted row opens it; unmounted rows mount first.
                onClicked: {
                    if (row.modelData.mounted)
                        disks.openMount(row.modelData.mountPoint)
                    else
                        disks.mount(row.modelData.path)
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
                        width: 32; height: 32
                        source: "image://icon/drive-removable-media-usb@" + theme.revision
                        sourceSize: Qt.size(32 * Screen.devicePixelRatio, 32 * Screen.devicePixelRatio)
                    }
                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        width: row.width - 32 - ejectBtn.width - mountBtn.width - 40
                        Text {
                            width: parent.width
                            text: row.modelData.label
                            color: theme.foreground
                            elide: Text.ElideRight
                        }
                        Text {
                            width: parent.width
                            text: row.modelData.mounted
                                  ? row.modelData.mountPoint
                                  : (popup.fmtSize(row.modelData.size) || row.modelData.device)
                            color: Qt.rgba(theme.foreground.r, theme.foreground.g, theme.foreground.b, 0.5)
                            font.pixelSize: 11
                            elide: Text.ElideMiddle
                        }
                    }
                    ToolButton {
                        id: mountBtn
                        anchors.verticalCenter: parent.verticalCenter
                        text: row.modelData.mounted ? qsTr("Desmontar") : qsTr("Montar")
                        onClicked: row.modelData.mounted
                                   ? disks.unmount(row.modelData.path)
                                   : disks.mount(row.modelData.path)
                        contentItem: Text {
                            text: mountBtn.text
                            color: theme.foreground
                            font.pixelSize: 12
                        }
                    }
                    ToolButton {
                        id: ejectBtn
                        anchors.verticalCenter: parent.verticalCenter
                        visible: row.modelData.ejectable
                        width: visible ? implicitWidth : 0
                        onClicked: disks.eject(row.modelData.drive)
                        contentItem: Image {
                            source: "image://icon/media-eject@" + theme.revision
                            sourceSize: Qt.size(18 * Screen.devicePixelRatio, 18 * Screen.devicePixelRatio)
                            fillMode: Image.PreserveAspectFit
                        }
                    }
                }
            }

            // Empty-state hint.
            Text {
                anchors.centerIn: parent
                visible: volumesList.count === 0
                text: qsTr("No hay dispositivos extraíbles")
                color: Qt.rgba(theme.foreground.r, theme.foreground.g, theme.foreground.b, 0.5)
            }
        }
    }
}
