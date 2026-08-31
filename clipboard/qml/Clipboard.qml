// The complete clipboard history window. It is a normal resizable toplevel in
// the kdock-clipboard process; kdock itself only launches and toggles it.

import QtQuick
import QtQuick.Controls
import QtQuick.Window

Item {
    id: root

    readonly property int pad: 12
    readonly property color fg: palette.windowText
    readonly property color dim: Qt.rgba(fg.r, fg.g, fg.b, 0.62)

    SystemPalette {
        id: palette
        colorGroup: SystemPalette.Active
    }

    Rectangle {
        anchors.fill: parent
        color: palette.window
    }

    Column {
        anchors.fill: parent
        anchors.margins: root.pad
        spacing: 8

        Row {
            id: optionsRow
            width: parent.width
            spacing: 8

            Text {
                width: parent.width - closeButton.width - parent.spacing
                text: qsTr("Historial del portapapeles")
                color: root.fg
                font.bold: true
                font.pixelSize: 18
                elide: Text.ElideRight
                verticalAlignment: Text.AlignVCenter
                height: 32
            }

            Button {
                id: closeButton
                text: qsTr("Cerrar")
                onClicked: win.closeWindow()
            }
        }

        Row {
            width: parent.width
            spacing: 8

            CheckBox {
                id: fixedCheck
                text: qsTr("Fija")
                checked: win.alwaysOnTop
                onToggled: win.alwaysOnTop = checked
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Mantener la ventana siempre encima")
            }

            CheckBox {
                id: closeCheck
                text: qsTr("Cerrar después de copiar")
                checked: win.closeAfterCopy
                onToggled: win.closeAfterCopy = checked
            }
        }

        Flow {
            id: toolsFlow
            width: parent.width
            height: childrenRect.height
            spacing: 8

            Button {
                text: qsTr("Borrar")
                onClicked: clipboardHistory.clearHistory()
            }

            Button {
                text: qsTr("Abrir")
                onClicked: clipboardHistory.openInEditor()
            }

            Button {
                text: qsTr("Guardar")
                onClicked: clipboardHistory.saveHistoryDialog()
            }

            CheckBox {
                text: qsTr("Guardar imágenes")
                checked: clipboardHistory.captureImages
                onToggled: clipboardHistory.captureImages = checked
            }
        }

        TextField {
            id: searchField
            width: parent.width
            placeholderText: qsTr("Buscar en el portapapeles…")
            selectByMouse: true
            onTextChanged: root.query = text
        }

        ListView {
            id: historyList
            width: parent.width
            height: parent.height - 32 - optionsRow.height - toolsFlow.height
                    - searchField.height - parent.spacing * 4
            clip: true
            model: root.listModel
            ScrollBar.vertical: ScrollBar {}

            delegate: ItemDelegate {
                id: row
                required property var modelData
                width: ListView.view.width
                height: row.modelData.isImage ? 76 : 42

                background: Rectangle {
                    radius: 4
                    color: row.hovered ? palette.highlight : "transparent"
                    opacity: row.hovered ? 0.28 : 1.0
                }

                contentItem: Row {
                    spacing: 8
                    leftPadding: 8
                    rightPadding: 8

                    Image {
                        visible: row.modelData.isImage
                        anchors.verticalCenter: parent.verticalCenter
                        width: visible ? 104 : 0
                        height: 68
                        fillMode: Image.PreserveAspectFit
                        source: row.modelData.isImage ? row.modelData.imageUrl : ""
                        sourceSize: Qt.size(104 * Screen.devicePixelRatio,
                                            68 * Screen.devicePixelRatio)
                        asynchronous: true
                    }

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        width: row.width - 16 - (row.modelData.isImage ? 112 : 0)
                        text: row.modelData.preview
                        color: root.fg
                        elide: Text.ElideRight
                        verticalAlignment: Text.AlignVCenter
                    }
                }

                onClicked: {
                    if (row.modelData.isImage)
                        clipboardHistory.setClipboardImage(row.modelData.file)
                    else
                        clipboardHistory.setClipboard(row.modelData.text)
                    if (win.closeAfterCopy)
                        win.closeWindow()
                    else
                        win.hideWindow()
                }
            }

            Text {
                anchors.centerIn: parent
                visible: historyList.count === 0
                text: root.query.length > 0 ? qsTr("Sin coincidencias")
                                             : qsTr("El historial está vacío")
                color: root.dim
            }
        }
    }

    property string query: ""
    property int refreshTick: 0
    readonly property var listModel: {
        refreshTick
        return clipboardHistory ? clipboardHistory.entries(query) : []
    }

    Connections {
        target: clipboardHistory
        function onChanged() { root.refreshTick++ }
    }
}
