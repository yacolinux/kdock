// RelanzadorPopup.qml - Popup layout for a Relanzador.
// Shows a mini-dock with the relanzador's launchers and optional widgets.

import QtQuick
import QtQuick.Controls

Popup {
    id: popup

    required property var relanzador  // RelanzadorConfig
    required property int iconSize
    required property int iconSpacing
    required property bool horizontal
    required property var theme
    required property var config
    required property var apps  // DesktopEntryIndex
    property var relanzadores: null  // RelanzadoresManager

    // Get the model from the manager when relanzador changes
    readonly property var relanzadorModel: {
        if (!relanzadores || !relanzador) return null
        return relanzadores.getModel(relanzador.id)
    }

    // A real xdg_popup (delegated to xdg-shell by the layer-shell integration)
    // so it can extend beyond the dock's tiny layer-shell surface. Without
    // this the popup is clipped inside the dock surface and stays invisible.
    popupType: Popup.Window
    modal: false
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    padding: 8

    background: Rectangle {
        radius: (config && config.compact) ? 6 : 12
        color: theme ? Qt.rgba(theme.background.r, theme.background.g, theme.background.b, config ? config.opacity : 0.85) : "transparent"
        border.width: (config && config.panelMode) ? 0 : 1
        border.color: theme ? Qt.rgba(theme.foreground.r, theme.foreground.g, theme.foreground.b, 0.18) : "transparent"
    }

    contentItem: Item {
        id: content
        implicitWidth: popupGrid.implicitWidth + pad * 2
        implicitHeight: popupGrid.implicitHeight + pad * 2

        readonly property int pad: (config && config.compact) ? 4 : 8

        Grid {
            id: popupGrid
            anchors.centerIn: parent
            columns: popup.horizontal ? Math.max(1, relanzadorRepeater.count) : 1
            spacing: popup.iconSpacing

            Repeater {
                id: relanzadorRepeater
                model: popup.relanzadorModel

                Item {
                    width: popup.iconSize
                    height: popup.iconSize

                    Image {
                        anchors.centerIn: parent
                        width: Math.round(popup.iconSize * 0.85)
                        height: width
                        source: model.iconName ? "image://icon/" + model.iconName + "@" + theme.revision : ""
                        sourceSize: Qt.size(popup.iconSize * Screen.devicePixelRatio,
                                            popup.iconSize * Screen.devicePixelRatio)
                        scale: delegateMouse.containsMouse ? 1.12 : 1.0
                        Behavior on scale { NumberAnimation { duration: 120 } }
                    }

                    ToolTip {
                        popupType: Popup.Window
                        visible: delegateMouse.containsMouse
                        delay: 400
                        text: model.name || ""
                    }

                    MouseArea {
                        id: delegateMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        acceptedButtons: Qt.LeftButton | Qt.MiddleButton
                        onClicked: (mouse) => {
                            if (mouse.button === Qt.LeftButton) {
                                // Launch the app or open sub-relanzador
                                if (model.isSubRelanzador) {
                                    // TODO: Open sub-relanzador popup
                                } else if (popup.relanzadorModel) {
                                    popup.relanzadorModel.launch(index)
                                }
                                popup.close()
                            } else if (mouse.button === Qt.MiddleButton) {
                                // Middle click: new instance
                                if (popup.relanzadorModel && !model.isSubRelanzador) {
                                    popup.relanzadorModel.launch(index)
                                }
                            }
                        }
                    }
                }
            }
        }

        // Optional widgets section
        Row {
            id: widgetRow
            anchors.top: popup.horizontal ? popupGrid.bottom : undefined
            anchors.left: popup.horizontal ? undefined : popupGrid.right
            anchors.topMargin: popup.horizontal ? popup.iconSpacing : 0
            anchors.leftMargin: popup.horizontal ? 0 : popup.iconSpacing
            anchors.horizontalCenter: popup.horizontal ? parent.horizontalCenter : undefined
            anchors.verticalCenter: popup.horizontal ? undefined : parent.verticalCenter
            spacing: popup.iconSpacing

            // Volume widget placeholder
            Item {
                visible: relanzador && relanzador.showVolume
                width: popup.iconSize
                height: popup.iconSize
                // TODO: Reuse volume widget from main dock
            }

            // Brightness widget placeholder
            Item {
                visible: relanzador && relanzador.showBrightness
                width: popup.iconSize
                height: popup.iconSize
                // TODO: Reuse brightness widget from main dock
            }

            // Clock widget placeholder
            Item {
                visible: relanzador && relanzador.showClock
                width: popup.iconSize
                height: popup.iconSize
                // TODO: Reuse clock widget from main dock
            }

            // Autohide toggle placeholder
            Item {
                visible: relanzador && relanzador.showAutohide
                width: popup.iconSize
                height: popup.iconSize
                // TODO: Reuse autohide widget from main dock
            }

            // Systray placeholder
            Item {
                visible: relanzador && relanzador.showSystray
                width: popup.iconSize
                height: popup.iconSize
                // TODO: Reuse systray widget from main dock
            }
        }
    }
}