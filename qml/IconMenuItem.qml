// IconMenuItem.qml - MenuItem that renders its icon via Image (image://icon/
// provider) instead of QQuickIconImage. QQuickIconImage does not work inside
// popupType: Popup.Window menus on Wayland, but Image does (proven by the
// AppMenuPopup app list). This component bridges that gap.

import QtQuick
import QtQuick.Controls

MenuItem {
    id: control

    // Freedesktop icon name (resolved through the image://icon provider).
    property string iconName: ""
    // Ready-made URL, for icons that are not theme names: systray menu entries
    // ship a raw image blob served by image://systray. Takes precedence.
    property string iconSource: ""

    readonly property string _source: iconSource !== "" ? iconSource
        : iconName !== "" ? "image://icon/" + iconName + "@" + theme.revision
                          : ""

    contentItem: Row {
        spacing: control.spacing
        // MenuItem draws its check indicator at leftPadding, which is where our
        // icon starts: without reserving its room the tick lands on top of the
        // icon and both read as a smudge. The stock IconLabel does this through
        // its own indicatorPadding; a spacer is the equivalent for a Row.
        Item {
            visible: control.checkable
            width: visible && control.indicator ? control.indicator.width : 0
            height: 1
        }
        Image {
            visible: control._source !== ""
            source: control._source
            // Systray menu entries may carry a raw icon-data URL with a serial
            // suffix. Do not retain every old raw blob in Qt Quick's image
            // cache; named theme icons keep the normal cache behavior.
            cache: control.iconSource === ""
            sourceSize: Qt.size(22 * Screen.devicePixelRatio, 22 * Screen.devicePixelRatio)
            width: 22
            height: 22
            anchors.verticalCenter: parent.verticalCenter
        }
        Text {
            text: control.text
            color: control.palette.windowText
            font: control.font
            anchors.verticalCenter: parent.verticalCenter
            elide: Text.ElideRight
        }
    }
}
