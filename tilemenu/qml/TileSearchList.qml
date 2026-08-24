// TileSearchList.qml - the vertical "en filas" results list for a search, and
// the recent-apps strip that shows when the empty search box takes focus.
//
// It binds to a plain JS array of { tileId, name, icon, comment } handed down
// from TileModel (searchResults() / recentApps()), so it is completely
// independent of the tile grid: no placement, no groups, no drag. Keyboard is
// the point here — Down/Up walk the rows, Enter launches, Up past the top hands
// focus back to the search field.

import QtQuick
import QtQuick.Controls

ListView {
    id: list

    property var entries: []
    property string title: ""

    // A row was chosen (click or Enter).
    signal launch(string id)
    // Up-arrow on the first row: give the search field the keyboard back.
    signal atTop()

    // The row the context menu is acting on. One shared Menu for the whole list
    // (declaring it in the delegate would build one per row), parameterised by id
    // — the same trick TileMenu.qml uses for its tile menu.
    property string menuId: ""
    function openRowMenu(id) {
        list.menuId = id
        rowMenu.popup()
    }

    Menu {
        id: rowMenu
        popupType: Popup.Window
        width: Math.max(implicitWidth + 64, 220)
        IconMenuItem {
            text: qsTr("Abrir nueva instancia")
            iconName: "window-new"
            onTriggered: list.launch(list.menuId)
        }
        IconMenuItem {
            readonly property bool fav: appMenu.isFavorite(list.menuId)
            text: fav ? qsTr("Quitar de Favoritos") : qsTr("Agregar a Favoritos")
            iconName: fav ? "non-starred-symbolic" : "starred-symbolic"
            onTriggered: appMenu.toggleFavorite(list.menuId)
        }
    }

    model: entries
    clip: true
    // We drive the cursor ourselves so we can bounce off the top edge into the
    // search field instead of Qt's built-in wrap/stop.
    keyNavigationEnabled: false
    boundsBehavior: Flickable.StopAtBounds
    currentIndex: entries.length > 0 ? 0 : -1
    ScrollBar.vertical: ScrollBar {}

    // Keep the cursor in range when the result set shrinks under it.
    onEntriesChanged: currentIndex = entries.length > 0
                      ? Math.min(currentIndex < 0 ? 0 : currentIndex, entries.length - 1)
                      : -1

    function launchCurrent() {
        if (currentIndex >= 0 && currentIndex < entries.length)
            list.launch(entries[currentIndex].tileId)
    }

    header: Text {
        visible: list.title.length > 0
        height: visible ? implicitHeight + 8 : 0
        leftPadding: 10
        topPadding: 4
        text: list.title
        color: Qt.rgba(theme.foreground.r, theme.foreground.g, theme.foreground.b, 0.55)
        font.pixelSize: 11
        font.bold: true
    }

    highlightMoveDuration: 80
    highlight: Rectangle {
        radius: 6
        color: Qt.rgba(theme.highlight.r, theme.highlight.g, theme.highlight.b, 0.28)
    }

    delegate: Item {
        id: row
        required property int index
        required property var modelData
        width: ListView.view.width
        height: 44

        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            onPositionChanged: list.currentIndex = row.index
            onClicked: (mouse) => {
                list.currentIndex = row.index
                if (mouse.button === Qt.RightButton)
                    list.openRowMenu(row.modelData.tileId)
                else
                    list.launch(row.modelData.tileId)
            }
        }

        Row {
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 12
            spacing: 12

            Image {
                anchors.verticalCenter: parent.verticalCenter
                width: 30
                height: 30
                visible: tileConfig.showIcons
                source: "image://icon/" + row.modelData.icon + "@" + theme.revision
                sourceSize: Qt.size(30 * Screen.devicePixelRatio, 30 * Screen.devicePixelRatio)
            }

            Column {
                anchors.verticalCenter: parent.verticalCenter
                spacing: 1
                Text {
                    text: row.modelData.name
                    color: theme.foreground
                    font.pixelSize: 14
                    font.bold: tileConfig.labelBold
                    elide: Text.ElideRight
                    width: row.width - 66
                }
                Text {
                    visible: text.length > 0
                    text: row.modelData.comment || ""
                    color: Qt.rgba(theme.foreground.r, theme.foreground.g,
                                   theme.foreground.b, 0.55)
                    font.pixelSize: 11
                    elide: Text.ElideRight
                    width: row.width - 66
                }
            }
        }
    }

    Keys.onDownPressed: {
        if (currentIndex < entries.length - 1)
            incrementCurrentIndex()
    }
    Keys.onUpPressed: {
        if (currentIndex <= 0)
            list.atTop()
        else
            decrementCurrentIndex()
    }
    Keys.onReturnPressed: launchCurrent()
    Keys.onEnterPressed: launchCurrent()
    Keys.onEscapePressed: win.hideMenu()
}
