// A flat button with an optional icon, themed off `theme` like everything else
// in the panel. Deliberately not QtQuick.Controls' Button: the Basic style paints
// a light grey chrome that fights every dark panel background.

import QtQuick
import QtQuick.Controls

Item {
    id: button

    property string icon: ""
    property string label: ""
    property bool enabled: true
    property bool checked: false
    property bool compact: false
    property color accent: theme.highlight
    // The card (or the panel) that contains this button decides the text and
    // fill colour; on its own the button would use the KDE foreground and
    // vanish on a light card.
    property color fg: theme.foreground
    // Tooltip text; empty = no tooltip.
    property string tip: ""

    signal clicked()

    implicitWidth: Math.max(cmConfig ? cmConfig.buttonWidth : 0,
                            compact ? 32 : 90,
                            content.implicitWidth + (compact ? 12 : 20))
    // The content grows with the scaled font, so the height follows it instead
    // of clipping the label on a large fontSize.
    implicitHeight: Math.max(cmConfig ? cmConfig.buttonHeight : 0,
                             Math.max(compact ? 28 : 34,
                                      content.implicitHeight + (compact ? 8 : 12)))

    Rectangle {
        anchors.fill: parent
        radius: 6
        color: !button.enabled
               ? Qt.rgba(button.fg.r, button.fg.g, button.fg.b, 0.04)
               : button.checked
                 ? Qt.rgba(button.accent.r, button.accent.g, button.accent.b, 0.45)
                 : mouse.containsMouse
                   ? Qt.rgba(button.fg.r, button.fg.g, button.fg.b, 0.16)
                   : Qt.rgba(button.fg.r, button.fg.g, button.fg.b, 0.08)
        border.width: button.checked ? 1 : 0
        border.color: button.accent
    }

    Row {
        id: content
        anchors.centerIn: parent
        spacing: button.label.length > 0 && button.icon.length > 0 ? 7 : 0

        Image {
            visible: button.icon.length > 0
            anchors.verticalCenter: parent.verticalCenter
            width: button.compact ? 16 : 18
            height: width
            source: button.icon.length > 0
                    ? "image://icon/" + button.icon + win.iconSuffix : ""
            sourceSize: Qt.size(width * Screen.devicePixelRatio, height * Screen.devicePixelRatio)
            opacity: button.enabled ? 1.0 : 0.4
        }
        Text {
            visible: button.label.length > 0
            anchors.verticalCenter: parent.verticalCenter
            text: button.label
            color: button.fg
            opacity: button.enabled ? 1.0 : 0.4
            font.pixelSize: Math.max(7, Math.round((button.compact ? 11 : 12) * cmConfig.fontScale))
            font.bold: cmConfig.labelBold
        }
    }

    // The *attached* tooltip: it shares one instance per window instead of
    // creating a popup per button (the trap the tile menu paid for).
    ToolTip.text: button.tip
    ToolTip.visible: button.tip.length > 0 && mouse.containsMouse
    ToolTip.delay: 600

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        enabled: button.enabled
        onClicked: button.clicked()
    }
}
