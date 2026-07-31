// ClipboardPopup.qml - clipboard history window: a search field at the top and
// a scrollable list of past text entries (10 rows visible). Clicking a row
// copies that entry back to the active clipboard. Modeled on AppMenuPopup.

import QtQuick
import QtQuick.Controls.Basic

Popup {
    id: popup

    required property var theme
    required property var config

    popupType: Popup.Window
    // modal: true → the compositor grants a keyboard grab so the search
    // TextField receives key events (same rationale as AppMenuPopup).
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    padding: 8
    width: config.clipboardPopupWidth
    height: config.clipboardPopupHeight

    property string query: ""
    // Bumped on history change to force the list model to re-evaluate.
    property int refreshTick: 0

    readonly property var listModel: {
        refreshTick // dependency
        if (!clipboardHistory) return []
        return clipboardHistory.entries(query)
    }

    Connections {
        target: clipboardHistory
        function onChanged() { popup.refreshTick++ }
    }

    // The dock's layer surface is keyboard-inert by default; enable exclusive
    // keyboard focus while the popup is open so the search field can type.
    onAboutToShow: dockWindow.setKeyboardInteractive(true)
    onAboutToHide: dockWindow.setKeyboardInteractive(false)
    onClosed: dockWindow.setKeyboardInteractive(false)

    onOpened: {
        query = ""
        searchField.text = ""
        searchField.forceActiveFocus()
        // The keyboard-interactivity change is double-buffered; retry the
        // focus after the compositor has processed the grab.
        focusRetry.restart()
    }

    Timer {
        id: focusRetry
        interval: 50
        onTriggered: searchField.forceActiveFocus()
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

            // --- Search ---
            TextField {
                id: searchField
                width: parent.width
                placeholderText: qsTr("Buscar en el portapapeles…")
                color: theme.foreground
                placeholderTextColor: Qt.rgba(theme.foreground.r, theme.foreground.g,
                                              theme.foreground.b, 0.5)
                background: Rectangle {
                    radius: 6
                    color: Qt.rgba(theme.foreground.r, theme.foreground.g, theme.foreground.b, 0.08)
                    border.width: searchField.activeFocus ? 1 : 0
                    border.color: theme.highlight
                }
                onTextChanged: popup.query = text
            }

            // --- History list ---
            ListView {
                id: historyList
                width: parent.width
                height: parent.height - searchField.height - parent.spacing
                clip: true
                model: popup.listModel
                ScrollBar.vertical: ScrollBar {}

                delegate: ItemDelegate {
                    id: row
                    required property var modelData
                    width: ListView.view.width
                    // 10 rows visible in the default popup height.
                    height: 40

                    background: Rectangle {
                        radius: 4
                        color: row.hovered
                               ? Qt.rgba(theme.highlight.r, theme.highlight.g, theme.highlight.b, 0.25)
                               : "transparent"
                    }

                    contentItem: Text {
                        text: row.modelData.preview
                        color: theme.foreground
                        elide: Text.ElideRight
                        verticalAlignment: Text.AlignVCenter
                        leftPadding: 8
                        rightPadding: 8
                    }

                    onClicked: {
                        clipboardHistory.setClipboard(row.modelData.text)
                        popup.close()
                    }
                }

                // Empty-state hint.
                Text {
                    anchors.centerIn: parent
                    visible: historyList.count === 0
                    text: popup.query.length > 0 ? qsTr("Sin coincidencias")
                                                 : qsTr("El historial está vacío")
                    color: Qt.rgba(theme.foreground.r, theme.foreground.g, theme.foreground.b, 0.5)
                }
            }
        }
    }
}
