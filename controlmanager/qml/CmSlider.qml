// One labelled slider row: icon, name, the slider itself and the value.
//
// Every section that controls a continuous quantity (volume, brightness) draws
// one of these, so the panel has a single place where that row is styled — and
// a single place where the "don't fight the user's drag with the backend's
// echo" rule lives (see `live` below).

import QtQuick
import QtQuick.Controls.Basic

Item {
    id: row

    property string icon: ""
    property string label: ""
    property real value: 0          // 0..1, what the backend reports
    property real minimum: 0
    property real maximum: 1
    property bool enabled: true
    // Text on the right. Empty means "the percentage of value".
    property string valueText: ""
    property bool compact: false
    // Same contract as CmButton.fg: the card that contains the row decides.
    property color fg: theme.foreground

    signal moved(real value)

    implicitHeight: Math.round((compact ? 30 : 38) * cmConfig.fontScale)

    // The backend echoes back what we just wrote, usually a few milliseconds
    // later. Feeding that into the slider while the user is still dragging
    // makes the handle stutter, so the binding is dropped for the duration.
    readonly property bool live: !slider.pressed

    readonly property bool pressed: slider.pressed

    // A Flickable ancestor steals the pointer grab out from under the handle.
    // Every card is inside one, and `contentHeight != height` is enough for
    // Flickable to consider itself flickable — content *shorter* than the
    // viewport counts, so even a panel with nothing to scroll steals. It takes
    // the grab as soon as the drag drifts past Qt's threshold on the vertical
    // axis, and a hand always drifts: the handle stops following, the gesture
    // ends up outside the window, and only a plain click (which never crosses
    // the threshold) works. Reported 2026-08-12 on the panel's monitor rows.
    //
    // Note that `flickableDirection: VerticalFlick` does NOT help: vertical is
    // precisely the axis that steals here. Suspending `interactive` for the
    // duration of the drag is what does, and it costs the scroll gesture
    // nothing — the pointer is busy holding a handle.
    property Item flickAncestor: null
    property bool flickWasInteractive: true

    function findFlickable() {
        var p = row.parent
        while (p) {
            // Duck-typed instead of `instanceof Flickable`: this file has no
            // reason to care which Flickable subclass it is inside of.
            if (p.maximumFlickVelocity !== undefined && p.interactive !== undefined)
                return p
            p = p.parent
        }
        return null
    }

    onPressedChanged: {
        if (row.pressed) {
            row.flickAncestor = row.findFlickable()
            if (row.flickAncestor) {
                row.flickWasInteractive = row.flickAncestor.interactive
                row.flickAncestor.interactive = false
            }
        } else if (row.flickAncestor) {
            // Restore what it was, not a blunt `true`: a Flickable that was
            // deliberately inert has to stay that way.
            row.flickAncestor.interactive = row.flickWasInteractive
            row.flickAncestor = null
        }
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
        id: valueLabel
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        text: row.valueText.length > 0
              ? row.valueText
              : Math.round(row.value * 100) + " %"
        color: row.fg
        opacity: row.enabled ? 0.7 : 0.35
        font.pixelSize: Math.max(7, Math.round((11) * cmConfig.fontScale))
        horizontalAlignment: Text.AlignRight
        width: Math.round(42 * cmConfig.fontScale)
    }

    Slider {
        id: slider
        anchors.left: labelText.visible ? labelText.right
                                        : (iconImage.visible ? iconImage.right : parent.left)
        anchors.leftMargin: 8
        anchors.right: valueLabel.left
        anchors.rightMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        from: row.minimum
        to: row.maximum
        enabled: row.enabled
        value: row.value

        // Re-assert the backend's value the moment the drag ends: the plain
        // `value:` binding above is broken by the user's own interaction.
        Binding on value {
            when: row.live
            value: row.value
            restoreMode: Binding.RestoreNone
        }

        onMoved: row.moved(slider.value)

        background: Rectangle {
            x: slider.leftPadding
            y: slider.topPadding + slider.availableHeight / 2 - height / 2
            width: slider.availableWidth
            height: 4
            radius: 2
            color: Qt.rgba(row.fg.r, row.fg.g, row.fg.b, 0.18)
            Rectangle {
                width: slider.visualPosition * parent.width
                height: parent.height
                radius: parent.radius
                color: slider.enabled ? theme.highlight
                                      : Qt.rgba(row.fg.r, row.fg.g, row.fg.b, 0.3)
            }
        }
        handle: Rectangle {
            x: slider.leftPadding + slider.visualPosition * (slider.availableWidth - width)
            y: slider.topPadding + slider.availableHeight / 2 - height / 2
            width: 14
            height: 14
            radius: 7
            color: slider.pressed ? theme.highlight : row.fg
            opacity: slider.enabled ? 1.0 : 0.4
        }
    }
}
