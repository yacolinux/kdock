// AppMenuPopup.qml - XDG application menu (KMenu/Kickoff-like):
// search field, category sidebar (Favorites first), app list, and
// editable favorites (star toggle / context menu).

import QtQuick
import QtQuick.Controls.Basic

Popup {
    id: popup

    required property var theme
    required property var config

    popupType: Popup.Window
    // modal: true → the compositor grants a keyboard grab to the popup so the
    // search TextField receives key events. With Popup.Window there is no dim
    // overlay (that only happens with Popup.Item), so the look is unchanged.
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    padding: 8
    // Size is configured from Settings → Widgets (per-dock, persisted).
    width: config.menuPopupWidth
    height: config.menuPopupHeight

    // Sidebar rows: { key, label, icon, depth }. Categories first, then the
    // submenus declared in the XDG .menu files (KDE's menu editor, browsers'
    // web apps) — see AppMenu::sections().
    readonly property var sectionModel: {
        refreshTick // dependency
        return appMenu ? appMenu.sections() : []
    }
    property string currentCategory: sectionModel.length > 0 ? sectionModel[0].key : ""
    property string query: ""
    // Bumped on favorites change to force the list model to re-evaluate.
    property int refreshTick: 0

    readonly property var listModel: {
        refreshTick // dependency
        if (!appMenu) return []
        if (query.length > 0) return appMenu.search(query)
        return appMenu.appsInCategory(currentCategory)
    }

    Connections {
        target: appMenu
        function onFavoritesChanged() { popup.refreshTick++ }
        function onChanged() { popup.refreshTick++ }
    }

    // The dock's layer surface is keyboard-inert by default; enable exclusive
    // keyboard focus while the menu is open so the search field can type.
    onAboutToShow: dockWindow.setKeyboardInteractive(true)
    onAboutToHide: dockWindow.setKeyboardInteractive(false)
    onClosed: dockWindow.setKeyboardInteractive(false)

    onOpened: {
        query = ""
        searchField.text = ""
        searchField.forceActiveFocus()
        // The layer-shell keyboard-interactivity change is double-buffered: it
        // only takes effect on the next wl_surface_commit, which is async. Retry
        // the focus after the compositor has had time to process the grab.
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
                placeholderText: qsTr("Search applications…")
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

            // --- Body: categories | apps ---
            Row {
                width: parent.width
                height: parent.height - searchField.height
                        - (powerRow.visible ? powerRow.height + parent.spacing : 0)
                        - parent.spacing
                spacing: 8

                // Category sidebar (hidden while searching)
                Rectangle {
                    id: sidebar
                    visible: popup.query.length === 0
                    // Sized for the widest row: "All Applications" plus its icon,
                    // plus the indent a nested .menu submenu adds. Every row
                    // carries an icon now (see AppMenu::sections), so the old 172
                    // elided that one label.
                    width: visible ? 196 : 0
                    height: parent.height
                    radius: 6
                    color: Qt.rgba(theme.foreground.r, theme.foreground.g, theme.foreground.b, 0.05)

                    ListView {
                        anchors.fill: parent
                        anchors.margins: 4
                        clip: true
                        model: popup.sectionModel
                        ScrollBar.vertical: ScrollBar {}
                        delegate: ItemDelegate {
                            id: sectionDelegate
                            required property int index
                            required property var modelData
                            width: ListView.view.width
                            height: 30
                            background: Rectangle {
                                radius: 4
                                color: popup.currentCategory === sectionDelegate.modelData.key
                                       ? Qt.rgba(theme.highlight.r, theme.highlight.g, theme.highlight.b, 0.35)
                                       : (sectionDelegate.hovered
                                          ? Qt.rgba(theme.foreground.r, theme.foreground.g,
                                                    theme.foreground.b, 0.08) : "transparent")
                            }
                            contentItem: Row {
                                // Nested submenus are indented under their parent.
                                leftPadding: 6 + sectionDelegate.modelData.depth * 14
                                spacing: 6
                                Image {
                                    anchors.verticalCenter: parent.verticalCenter
                                    visible: sectionDelegate.modelData.icon.length > 0
                                    width: visible ? 18 : 0; height: 18
                                    source: visible ? "image://icon/" + sectionDelegate.modelData.icon
                                                      + "@" + theme.revision : ""
                                    sourceSize: Qt.size(18 * Screen.devicePixelRatio,
                                                        18 * Screen.devicePixelRatio)
                                }
                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: sectionDelegate.width - parent.leftPadding
                                           - (sectionDelegate.modelData.icon.length > 0 ? 24 : 0) - 6
                                    text: sectionDelegate.modelData.label
                                    color: theme.foreground
                                    elide: Text.ElideRight
                                    verticalAlignment: Text.AlignVCenter
                                }
                            }
                            onClicked: popup.currentCategory = sectionDelegate.modelData.key
                        }
                    }
                }

                // App/favorites grid. One column keeps the classic list look;
                // config.menuColumns (1-5) turns it into a grid for apps and
                // favorites alike (same model).
                GridView {
                    id: appList
                    readonly property int available: parent.width - sidebar.width
                                                     - (sidebar.visible ? parent.spacing : 0)
                    readonly property int columns: Math.max(1, config.menuColumns)
                    readonly property int gridIconSize: config.menuAppIconSize
                    readonly property int gridSpacing: config.menuGridSpacing

                    // Cells are sized from the icon, not from the available width:
                    // otherwise a handful of apps at 4 columns spread across the
                    // whole popup. The GridView is then only as wide as the cells
                    // it is allowed to put in a row, so `columns` still caps the
                    // row and the leftover space stays empty on the right.
                    FontMetrics { id: gridFont }
                    // Floor on the cell width: a cell sized purely from the icon
                    // is too narrow for the name under it, and every entry ends
                    // up elided after four characters. Ten characters per line
                    // (two lines) is enough for most app names.
                    readonly property int textFloor: Math.ceil(gridFont.averageCharacterWidth * 10)
                                                     + 2 * gridSpacing
                    cellWidth: columns === 1
                               ? available
                               : Math.min(Math.floor(available / columns),
                                          Math.max(gridIconSize + 2 * gridSpacing + 16, textFloor))
                    cellHeight: columns === 1
                                ? Math.max(44, gridIconSize + 12)
                                : gridIconSize + 2 * gridSpacing + Math.ceil(gridFont.height * 2) + 6

                    width: columns === 1 ? available : Math.min(available, columns * cellWidth)
                    height: parent.height
                    clip: true
                    model: popup.listModel
                    ScrollBar.vertical: ScrollBar {}

                    delegate: ItemDelegate {
                        id: appDelegate
                        required property var modelData
                        width: appList.cellWidth
                        height: appList.cellHeight
                        readonly property bool grid: appList.columns > 1

                        background: Rectangle {
                            anchors.fill: parent
                            anchors.margins: appDelegate.grid ? 3 : 0
                            radius: 4
                            color: appDelegate.hovered
                                   ? Qt.rgba(theme.highlight.r, theme.highlight.g, theme.highlight.b, 0.25)
                                   : "transparent"
                        }

                        contentItem: Item {
                            // --- single-column horizontal row (classic look) ---
                            Row {
                                anchors.fill: parent
                                visible: !appDelegate.grid
                                spacing: 8
                                Image {
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: appList.gridIconSize; height: appList.gridIconSize
                                    source: "image://icon/" + appDelegate.modelData.icon + "@" + theme.revision
                                    sourceSize: Qt.size(appList.gridIconSize * Screen.devicePixelRatio,
                                                        appList.gridIconSize * Screen.devicePixelRatio)
                                }
                                Column {
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: appDelegate.width - appList.gridIconSize - starButton.width - 24
                                    Text {
                                        text: appDelegate.modelData.name
                                        color: theme.foreground
                                        elide: Text.ElideRight
                                        width: parent.width
                                    }
                                    Text {
                                        visible: appDelegate.modelData.comment.length > 0
                                        text: appDelegate.modelData.comment
                                        color: Qt.rgba(theme.foreground.r, theme.foreground.g,
                                                       theme.foreground.b, 0.6)
                                        font.pixelSize: 11
                                        elide: Text.ElideRight
                                        width: parent.width
                                    }
                                }
                                // Favorite toggle
                                ToolButton {
                                    id: starButton
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 28; height: 28
                                    background: Item {}
                                    contentItem: Image {
                                        width: 18; height: 18
                                        anchors.centerIn: parent
                                        source: "image://icon/" + (appDelegate.modelData.favorite
                                                ? "starred-symbolic" : "non-starred-symbolic")
                                                + "@" + theme.revision
                                        sourceSize: Qt.size(18 * Screen.devicePixelRatio, 18 * Screen.devicePixelRatio)
                                        opacity: appDelegate.modelData.favorite ? 1.0 : (appDelegate.hovered ? 0.7 : 0.0)
                                    }
                                    onClicked: appMenu.toggleFavorite(appDelegate.modelData.id)
                                }
                            }

                            // --- multi-column compact cell (icon over name) ---
                            Column {
                                anchors.fill: parent
                                anchors.margins: appList.gridSpacing
                                visible: appDelegate.grid
                                spacing: 2
                                Image {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    width: appList.gridIconSize; height: appList.gridIconSize
                                    source: "image://icon/" + appDelegate.modelData.icon + "@" + theme.revision
                                    sourceSize: Qt.size(appList.gridIconSize * Screen.devicePixelRatio,
                                                        appList.gridIconSize * Screen.devicePixelRatio)
                                }
                                Text {
                                    width: parent.width
                                    text: appDelegate.modelData.name
                                    color: theme.foreground
                                    elide: Text.ElideRight
                                    horizontalAlignment: Text.AlignHCenter
                                    maximumLineCount: 2
                                    wrapMode: Text.Wrap
                                }
                            }

                            // Favorite toggle overlay for grid cells (top-right).
                            ToolButton {
                                visible: appDelegate.grid
                                anchors.top: parent.top
                                anchors.right: parent.right
                                width: 22; height: 22
                                background: Item {}
                                contentItem: Image {
                                    width: 14; height: 14
                                    anchors.centerIn: parent
                                    source: "image://icon/" + (appDelegate.modelData.favorite
                                            ? "starred-symbolic" : "non-starred-symbolic")
                                            + "@" + theme.revision
                                    sourceSize: Qt.size(14 * Screen.devicePixelRatio, 14 * Screen.devicePixelRatio)
                                    opacity: appDelegate.modelData.favorite ? 1.0 : (appDelegate.hovered ? 0.7 : 0.0)
                                }
                                onClicked: appMenu.toggleFavorite(appDelegate.modelData.id)
                            }
                        }

                        onClicked: {
                            appMenu.launch(appDelegate.modelData.id)
                            popup.close()
                        }

                        TapHandler {
                            acceptedButtons: Qt.RightButton
                            onTapped: favMenu.popup()
                        }
                        Menu {
                            id: favMenu
                            popupType: Popup.Window
                            IconMenuItem {
                                text: appDelegate.modelData.favorite ? qsTr("Remove from favorites")
                                                                     : qsTr("Add to favorites")
                                iconName: appDelegate.modelData.favorite ? "non-starred-symbolic"
                                                                          : "starred-symbolic"
                                onTriggered: appMenu.toggleFavorite(appDelegate.modelData.id)
                            }
                        }
                    }
                }
            }

            // --- Power / session actions (Kickoff-like footer) ---
            Row {
                id: powerRow
                visible: power && power.available && config.showMenuPower
                width: parent.width
                height: visible ? 40 : 0
                spacing: 6
                layoutDirection: Qt.RightToLeft

                Repeater {
                    model: [
                        { ic: "system-shutdown",    act: "shutdown", tip: qsTr("Shut Down…") },
                        { ic: "system-reboot",      act: "reboot",   tip: qsTr("Restart…") },
                        { ic: "system-log-out",     act: "logout",   tip: qsTr("Log Out…") },
                        { ic: "system-suspend",     act: "suspend",  tip: qsTr("Suspend") },
                        { ic: "system-lock-screen", act: "lock",     tip: qsTr("Lock") }
                    ]
                    Item {
                        required property var modelData
                        width: 34; height: 34
                        Rectangle {
                            anchors.fill: parent
                            radius: 6
                            color: pma.containsMouse
                                   ? Qt.rgba(theme.highlight.r, theme.highlight.g, theme.highlight.b, 0.25)
                                   : "transparent"
                        }
                        Image {
                            anchors.centerIn: parent
                            width: 22; height: 22
                            source: "image://icon/" + modelData.ic + "@" + theme.revision
                            sourceSize: Qt.size(22 * Screen.devicePixelRatio, 22 * Screen.devicePixelRatio)
                        }
                        ToolTip {
                            popupType: Popup.Window
                            visible: pma.containsMouse
                            delay: 400
                            text: modelData.tip
                        }
                        MouseArea {
                            id: pma
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: {
                                power[modelData.act]()
                                popup.close()
                            }
                        }
                    }
                }
            }
        }
    }
}
