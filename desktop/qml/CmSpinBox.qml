// A labelled percent row backed by a SpinBox instead of a Slider. The panel
// sets brightness (and anything else continuous) in whole-percent steps: a
// SpinBox takes clicks on its buttons, typed values and — with wheelEnabled —
// the mouse wheel, and none of those needs a pointer grab the way a slider
// drag does. That is the whole point: the panel's Flickables steal a slider's
// drag as soon as it drifts off the horizontal axis (see CmSlider), while a
// wheel event is accepted by the topmost item and never reaches the
// Flickable.
//
// The row's API mirrors CmSlider (value is 0..1, `moved` reports 0..1) so the
// callers are unchanged; only the widget is different.

import QtQuick
import QtQuick.Controls

Item {
    id: row

    property string icon: ""
    property string label: ""
    property real value: 0          // 0..1, what the backend reports
    property bool enabled: true
    property bool compact: false
    // Same contract as CmButton.fg: the card that contains the row decides.
    property color fg: theme.foreground

    signal moved(real value)

    implicitHeight: Math.round((compact ? 30 : 38) * cmConfig.fontScale)

    // The spinbox works in whole percent; the row's API stays 0..1.
    readonly property int pct: Math.round(row.value * 100)

    // Backend echo suppression: after a user change the backend may clamp or
    // delay its answer, and re-pushing it into the box mid-scroll reads as the
    // box fighting the wheel. The push is suspended while the field is being
    // edited and for 500 ms after the last user change, then re-asserted.
    property bool echoSuppressed: false
    Timer {
        id: echoTimer
        interval: 500
        onTriggered: row.echoSuppressed = false
    }

    Image {
        id: iconImage
        visible: row.icon.length > 0
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        width: row.compact ? 16 : 20
        height: width
        source: row.icon.length > 0
                ? "image://icon/" + row.icon + win.iconSuffix : ""
        sourceSize: Qt.size(width * Screen.devicePixelRatio, height * Screen.devicePixelRatio)
        opacity: row.enabled ? 1.0 : 0.4
    }

    Text {
        id: labelText
        visible: row.label.length > 0 && !row.compact
        anchors.left: iconImage.visible ? iconImage.right : parent.left
        anchors.leftMargin: iconImage.visible ? 8 : 0
        anchors.verticalCenter: parent.verticalCenter
        width: Math.min(implicitWidth, row.width * 0.4)
        text: row.label
        elide: Text.ElideRight
        color: row.fg
        opacity: row.enabled ? 0.85 : 0.4
        font.pixelSize: Math.max(7, Math.round((12) * cmConfig.fontScale))
        font.bold: cmConfig.labelBold
    }

    Text {
        id: pctLabel
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        text: "%"
        color: row.fg
        opacity: row.enabled ? 0.6 : 0.3
        font.pixelSize: Math.max(7, Math.round((11) * cmConfig.fontScale))
    }

    SpinBox {
        id: spinbox
        anchors.right: pctLabel.left
        anchors.rightMargin: 4
        anchors.verticalCenter: parent.verticalCenter
        // Wide enough for "100" plus the two step buttons ("puede tener 100%").
        width: row.compact ? 76 : 92
        from: 0
        to: 100
        stepSize: 1
        editable: true
        wheelEnabled: true
        enabled: row.enabled
        font.pixelSize: Math.max(7, Math.round((12) * cmConfig.fontScale))

        value: row.pct

        // Re-assert the backend's value once the user is done: the plain
        // `value:` binding above is broken by the user's own interaction.
        Binding on value {
            when: !spinbox.activeFocus && !row.echoSuppressed
            value: row.pct
            restoreMode: Binding.RestoreNone
        }

        onValueModified: {
            row.echoSuppressed = true
            echoTimer.restart()
            row.moved(spinbox.value / 100.0)
        }

        validator: IntValidator {
            locale: spinbox.locale.name
            bottom: 0
            top: 100
        }

        contentItem: TextInput {
            z: 2
            text: spinbox.displayText
            clip: width < implicitWidth
            padding: 4
            font: spinbox.font
            color: row.enabled ? row.fg : Qt.rgba(row.fg.r, row.fg.g, row.fg.b, 0.4)
            selectionColor: Qt.rgba(row.fg.r, row.fg.g, row.fg.b, 0.25)
            selectedTextColor: row.fg
            horizontalAlignment: Qt.AlignHCenter
            verticalAlignment: Qt.AlignVCenter
            readOnly: !spinbox.editable
            validator: spinbox.validator
        }

        background: Rectangle {
            implicitWidth: 60
            radius: 4
            color: !row.enabled
                   ? Qt.rgba(row.fg.r, row.fg.g, row.fg.b, 0.04)
                   : spinbox.activeFocus
                     ? Qt.rgba(row.fg.r, row.fg.g, row.fg.b, 0.14)
                     : Qt.rgba(row.fg.r, row.fg.g, row.fg.b, 0.08)
            border.width: spinbox.activeFocus ? 1 : 0
            border.color: theme.highlight
        }

        leftPadding: padding + (spinbox.mirrored
                                ? (spinbox.up.indicator ? spinbox.up.indicator.width : 0)
                                : (spinbox.down.indicator ? spinbox.down.indicator.width : 0))
        rightPadding: padding + (spinbox.mirrored
                                 ? (spinbox.down.indicator ? spinbox.down.indicator.width : 0)
                                 : (spinbox.up.indicator ? spinbox.up.indicator.width : 0))

        up.indicator: Rectangle {
            x: spinbox.mirrored ? 0 : spinbox.width - width
            // Not `implicitHeight: spinbox.height`: the Basic style computes the
            // control's own implicitHeight FROM the indicators' implicitHeight,
            // so that would be a binding loop. Fixed implicit sizes, height
            // follows the control instead.
            height: spinbox.height
            implicitWidth: row.compact ? 22 : 26
            implicitHeight: row.compact ? 22 : 26
            color: spinbox.up.pressed
                   ? Qt.rgba(row.fg.r, row.fg.g, row.fg.b, 0.25)
                   : Qt.rgba(row.fg.r, row.fg.g, row.fg.b, 0.10)
            radius: 4
            Rectangle {
                x: (parent.width - width) / 2
                y: (parent.height - height) / 2
                width: parent.width / 3
                height: 2
                color: row.enabled ? row.fg : Qt.rgba(row.fg.r, row.fg.g, row.fg.b, 0.4)
            }
            Rectangle {
                x: (parent.width - width) / 2
                y: (parent.height - height) / 2
                width: 2
                height: parent.width / 3
                color: row.enabled ? row.fg : Qt.rgba(row.fg.r, row.fg.g, row.fg.b, 0.4)
            }
        }

        down.indicator: Rectangle {
            x: spinbox.mirrored ? parent.width - width : 0
            height: spinbox.height
            implicitWidth: row.compact ? 22 : 26
            implicitHeight: row.compact ? 22 : 26
            color: spinbox.down.pressed
                   ? Qt.rgba(row.fg.r, row.fg.g, row.fg.b, 0.25)
                   : Qt.rgba(row.fg.r, row.fg.g, row.fg.b, 0.10)
            radius: 4
            Rectangle {
                x: (parent.width - width) / 2
                y: (parent.height - height) / 2
                width: parent.width / 3
                height: 2
                color: row.enabled ? row.fg : Qt.rgba(row.fg.r, row.fg.g, row.fg.b, 0.4)
            }
        }
    }
}
