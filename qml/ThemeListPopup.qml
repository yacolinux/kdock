// ThemeListPopup.qml - picker for a global KDE appearance setting, used twice:
// mode "icons" lists the installed icon themes, mode "colors" the color
// schemes (see AppearanceControl). One file instead of two because the only
// difference is the model, the preview and the apply call - and both lists are
// long enough (183 icon themes / 456 schemes on a full install) to need the
// same search field.
//
// Keyboard handling is the AppMenuPopup recipe: the dock's layer surface is
// keyboard-inert, so the search field only receives keys with modal + Window
// popups plus setKeyboardInteractive() while open.

import QtQuick
import QtQuick.Controls.Basic

Popup {
    id: popup

    required property var theme
    required property var config
    // "icons" | "colors"
    required property string mode

    readonly property bool iconsMode: mode === "icons"

    popupType: Popup.Window
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    padding: 8
    width: 340
    height: 420

    property string query: ""
    // Bumped when the applied theme changes, so the check mark follows.
    property int refreshTick: 0
    // Set around our own setFavorite() call: favoritesChanged is emitted
    // synchronously, and re-sorting there would yank the row out from under the
    // cursor that just ticked it.
    property bool freezeOrder: false

    readonly property var allEntries: {
        refreshTick // dependency
        if (!appearance) return []
        return iconsMode ? appearance.iconThemes() : appearance.colorSchemes()
    }
    readonly property var listModel: {
        if (query.length === 0) return allEntries
        const needle = query.toLowerCase()
        return allEntries.filter(function (e) {
            return e.name.toLowerCase().indexOf(needle) >= 0
                   || e.id.toLowerCase().indexOf(needle) >= 0
        })
    }

    Connections {
        target: appearance
        function onChanged() { popup.refreshTick++ }
        // Another picker (the other dock's widget, or the Settings dialog)
        // pinned something: reorder here too. Ticking a favourite *in this
        // popup* deliberately does not reorder while it is open - see the row's
        // CheckBox - so this only fires for the other pickers.
        function onFavoritesChanged() {
            if (!popup.freezeOrder)
                popup.refreshTick++
        }
    }

    onAboutToShow: dockWindow.setKeyboardInteractive(true)
    onAboutToHide: dockWindow.setKeyboardInteractive(false)
    onClosed: dockWindow.setKeyboardInteractive(false)

    onOpened: {
        // Cheap unless a scheme was installed since the last scan.
        if (appearance && !iconsMode)
            appearance.refreshIfStale()
        query = ""
        searchField.text = ""
        refreshTick++
        searchField.forceActiveFocus()
        // Same double-buffering caveat as AppMenuPopup: the keyboard grab only
        // lands on the next surface commit.
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

            TextField {
                id: searchField
                width: parent.width
                placeholderText: popup.iconsMode ? qsTr("Buscar iconset…")
                                                 : qsTr("Buscar esquema de color…")
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

            Text {
                width: parent.width
                text: popup.iconsMode
                      ? qsTr("%1 iconsets").arg(popup.listModel.length)
                      : qsTr("%1 esquemas").arg(popup.listModel.length)
                color: Qt.rgba(theme.foreground.r, theme.foreground.g, theme.foreground.b, 0.6)
                font.pixelSize: 11
            }

            ListView {
                id: list
                width: parent.width
                height: parent.height - searchField.height - 11 - 2 * parent.spacing
                clip: true
                model: popup.listModel
                // Hundreds of rows, each with previews: only build what shows.
                cacheBuffer: 200
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                delegate: Rectangle {
                    id: row
                    required property var modelData
                    required property int index
                    // Inner Repeaters shadow `modelData` with their own, so the
                    // row's entry is reached through this id.
                    readonly property var entry: modelData
                    // First row after the favourites block: draws the divider.
                    readonly property bool firstNonFavorite:
                        index > 0 && entry.fav !== true
                        && popup.listModel[index - 1].fav === true
                    width: list.width
                    height: 40
                    radius: 6
                    color: rowMouse.containsMouse
                           ? Qt.rgba(theme.highlight.r, theme.highlight.g, theme.highlight.b, 0.25)
                           : "transparent"

                    Row {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        spacing: 8
                        // Above the row's MouseArea, which is declared last and
                        // would otherwise swallow the favourites checkbox's
                        // clicks. The labels and images do not accept mouse
                        // events, so a click anywhere else still falls through
                        // to the MouseArea and applies the theme.
                        z: 1

                        // Preview: three sample icons rendered in that theme
                        // (image://icon resolves against any installed set), or
                        // three swatches of the scheme's own colors.
                        Row {
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 3
                            visible: popup.iconsMode
                            Repeater {
                                model: popup.iconsMode
                                       ? ["folder", "utilities-terminal", "configure"] : []
                                Image {
                                    required property string modelData
                                    width: 22; height: 22
                                    source: "image://icon/" + modelData + "@" + theme.revision
                                            + "@" + row.entry.id
                                    sourceSize: Qt.size(22 * Screen.devicePixelRatio,
                                                        22 * Screen.devicePixelRatio)
                                    asynchronous: true
                                }
                            }
                        }
                        Row {
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 3
                            visible: !popup.iconsMode
                            Repeater {
                                model: popup.iconsMode ? [] : ["bg", "fg", "sel"]
                                Rectangle {
                                    required property string modelData
                                    width: 22; height: 22; radius: 4
                                    color: row.entry[modelData]
                                    border.width: 1
                                    border.color: Qt.rgba(theme.foreground.r, theme.foreground.g,
                                                          theme.foreground.b, 0.25)
                                }
                            }
                        }

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            // Preview strip, tick and favourites box all come
                            // out of the name's width, or it elides too early.
                            width: parent.width - 3 * 22 - 2 * 3 - 24 - favBox.width
                                   - 4 * parent.spacing
                            text: modelData.name
                            color: theme.foreground
                            elide: Text.ElideRight
                            font.bold: modelData.current === true
                        }

                        Image {
                            anchors.verticalCenter: parent.verticalCenter
                            visible: modelData.current === true
                            width: 16; height: 16
                            source: "image://icon/dialog-ok@" + theme.revision
                            sourceSize: Qt.size(16 * Screen.devicePixelRatio,
                                                16 * Screen.devicePixelRatio)
                        }

                        CheckBox {
                            id: favBox
                            anchors.verticalCenter: parent.verticalCenter
                            width: 24
                            checked: row.entry.fav === true
                            ToolTip.text: qsTr("Favorito: lo pone al principio de la lista")
                            ToolTip.visible: hovered
                            ToolTip.delay: 600
                            indicator: Rectangle {
                                anchors.centerIn: parent
                                width: 16; height: 16; radius: 3
                                color: favBox.checked ? theme.highlight : "transparent"
                                border.width: 1
                                border.color: Qt.rgba(theme.foreground.r, theme.foreground.g,
                                                      theme.foreground.b,
                                                      favBox.checked ? 0.0 : 0.45)
                                Text {
                                    anchors.centerIn: parent
                                    visible: favBox.checked
                                    text: "✓"
                                    color: theme.background
                                    font.pixelSize: 12
                                    font.bold: true
                                }
                            }
                            onToggled: {
                                popup.freezeOrder = true
                                appearance.setFavorite(popup.mode, row.entry.id, checked)
                                popup.freezeOrder = false
                            }
                        }
                    }

                    // Divider between the pinned block and the rest.
                    Rectangle {
                        visible: row.firstNonFavorite
                        anchors.top: parent.top
                        anchors.left: parent.left
                        anchors.right: parent.right
                        height: 1
                        color: Qt.rgba(theme.foreground.r, theme.foreground.g,
                                       theme.foreground.b, 0.2)
                    }

                    MouseArea {
                        id: rowMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: {
                            if (popup.iconsMode)
                                appearance.applyIconTheme(modelData.id)
                            else
                                appearance.applyColorScheme(modelData.id)
                            popup.close()
                        }
                    }
                }
            }
        }
    }
}
