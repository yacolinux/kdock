// TileSidebar.qml - the section list: Favorites, All Applications, the
// freedesktop categories, and the submenus declared in the XDG .menu files
// (indented under their parent). Exactly the model AppMenuPopup's sidebar uses,
// because it is exactly the same AppMenu::sections().

import QtQuick
import QtQuick.Controls.Basic

Rectangle {
    id: sidebar

    property string currentKey: ""
    signal sectionPicked(string key)

    // Bumped when the app set changes, to re-evaluate the sections() call below
    // (a plain function call has no bindings of its own).
    property int refreshTick: 0
    readonly property var sectionModel: {
        refreshTick; // dependency
        return appMenu ? appMenu.sections() : []
    }

    color: Qt.rgba(theme.foreground.r, theme.foreground.g, theme.foreground.b, 0.06)
    radius: 8

    Connections {
        target: appMenu
        function onChanged() { sidebar.refreshTick++ }
    }

    ListView {
        id: list
        anchors.fill: parent
        anchors.margins: 6
        clip: true
        model: sidebar.sectionModel
        ScrollBar.vertical: ScrollBar {}

        delegate: ItemDelegate {
            id: sectionRow
            required property var modelData
            width: ListView.view.width
            height: 32

            background: Rectangle {
                radius: 5
                color: sidebar.currentKey === sectionRow.modelData.key
                       ? Qt.rgba(theme.highlight.r, theme.highlight.g, theme.highlight.b, 0.45)
                       : (sectionRow.hovered
                          ? Qt.rgba(theme.foreground.r, theme.foreground.g,
                                    theme.foreground.b, 0.10)
                          : "transparent")
            }

            contentItem: Row {
                leftPadding: 8 + sectionRow.modelData.depth * 14
                spacing: 6
                Image {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: sectionRow.modelData.icon.length > 0
                    width: visible ? 18 : 0
                    height: 18
                    source: visible ? "image://icon/" + sectionRow.modelData.icon
                                      + "@" + theme.revision : ""
                    sourceSize: Qt.size(18 * Screen.devicePixelRatio,
                                        18 * Screen.devicePixelRatio)
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    width: sectionRow.width - parent.leftPadding
                           - (sectionRow.modelData.icon.length > 0 ? 24 : 0) - 8
                    text: sectionRow.modelData.label
                    color: theme.foreground
                    font.bold: tileConfig.labelBold
                    elide: Text.ElideRight
                    verticalAlignment: Text.AlignVCenter
                }
            }

            onClicked: sidebar.sectionPicked(sectionRow.modelData.key)
        }
    }
}
