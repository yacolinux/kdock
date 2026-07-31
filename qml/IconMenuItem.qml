// IconMenuItem.qml - MenuItem that renders its icon via Image (image://icon/
// provider) instead of QQuickIconImage. QQuickIconImage does not work inside
// popupType: Popup.Window menus on Wayland, but Image does (proven by the
// AppMenuPopup app list). This component bridges that gap.

import QtQuick
import QtQuick.Controls.Basic

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
        Image {
            visible: control._source !== ""
            source: control._source
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
