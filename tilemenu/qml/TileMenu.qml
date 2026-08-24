// TileMenu.qml - the whole menu: section sidebar, search, the tile canvas, the
// A-Z rail and the power row.
//
// The window is an ordinary maximized toplevel (see tilewindow.h), so this item
// is handed exactly the desktop area the docks and panels left free — there is
// no geometry math here for "where is the dock".
//
// Groups are tabs: the canvas draws exactly one group, so a tile sits at its own
// (col, row) and the drag arithmetic is plain. Dropping a tile on another tab
// moves it there — with the other groups off screen, that and the tile's context
// menu are the only routes.

import QtQuick
import QtQuick.Controls

Item {
    id: root
    focus: true

    readonly property int pad: 16
    readonly property int gap: tileConfig.cellSpacing

    readonly property bool sidebarVisible: tileConfig.sidebar !== 2
    readonly property bool sidebarLeft: tileConfig.sidebar === 0
    // Jumping to a letter is meaningless once the tiles are placed by hand.
    readonly property bool railVisible: tileConfig.showLetterIndex && !tiles.customized
                                        && !tiles.searching
    readonly property int railW: railVisible ? 30 : 0

    readonly property color baseColor: (tileConfig.backgroundMode === 1
                                        && tileConfig.backgroundColorSet)
                                       ? tileConfig.backgroundColor : theme.background

    // --- grid metrics -------------------------------------------------------
    // The available width never depends on the cell size, so none of this feeds
    // back into itself.
    readonly property int canvasAvail: Math.max(120, canvasArea.width)
    readonly property int columns: tileConfig.columns > 0
        ? tileConfig.columns
        : Math.max(1, Math.floor((canvasAvail + gap) / (tileConfig.cellSize + gap)))
    readonly property int cell: tileConfig.cellStretch
        ? Math.max(tileConfig.cellMin,
                   Math.min(tileConfig.cellMax,
                            Math.floor((canvasAvail + gap) / columns) - gap))
        : tileConfig.cellSize
    readonly property int pitch: cell + gap

    // --- search results view ------------------------------------------------
    // "En filas" (searchView 0) swaps the tile canvas for a vertical list while
    // searching; "con íconos" (1) keeps the icon grid. Opening the menu always
    // lands on the section grid (Favoritos), never on a leftover search: the
    // recent-use history still orders the results, it just is not shown as its
    // own strip on open.
    readonly property bool searchListMode: tileConfig.searchView === 0
    readonly property var resultEntries: {
        tiles.query; tileConfig.searchSort // re-evaluate on either change
        return (root.searchListMode && tiles.searching) ? tiles.searchResults() : []
    }
    readonly property bool showResultsList: root.searchListMode && tiles.searching
    readonly property bool listShown: showResultsList
    readonly property var listEntries: resultEntries

    // One tab says nothing and eats the space, so the bar shows up only once
    // there is a second group. A search crosses every group, so it hides too.
    readonly property bool tabsVisible: tiles.groups.length > 1 && !tiles.searching
    readonly property bool tabsVertical: tileConfig.groupTabs >= 2

    // Pointer position while a tile is being dragged, in root coordinates,
    // reported by the tile's MouseArea.
    property point dragPointer: Qt.point(-1, -1)
    // Tab the pointer is over while dragging, or -1. The tile itself is clipped
    // by the canvas once it leaves it, so this follows the pointer, which is
    // both what the user is aiming with and always in range.
    readonly property int dragOverTab: {
        if (!canvas.dragTile || !groupTabs.visible)
            return -1
        var p = groupTabs.mapFromItem(root, root.dragPointer.x, root.dragPointer.y)
        return groupTabs.tabAt(p.x, p.y)
    }

    onColumnsChanged: tileLayout.setAutoColumns(columns)
    Component.onCompleted: tileLayout.setAutoColumns(columns)

    // --- background ---------------------------------------------------------
    Rectangle {
        anchors.fill: parent
        color: Qt.rgba(root.baseColor.r, root.baseColor.g, root.baseColor.b,
                       tileConfig.backgroundOpacity)
    }
    Image {
        anchors.fill: parent
        visible: tileConfig.backgroundImage.length > 0
        source: tileConfig.backgroundImageUrl
        fillMode: Image.PreserveAspectCrop
        opacity: tileConfig.backgroundOpacity
        asynchronous: true
    }

    // --- corner controls ----------------------------------------------------
    Row {
        id: corner
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: 10
        spacing: 10
        z: 50

        // The pin: with it on, none of the automatic close paths fire and the
        // menu behaves like any other window (it can go to the background and
        // come back through the task switcher).
        // Wrapped in an Item because the click area has to cover the whole
        // control: a MouseArea anchored to fill cannot be a child of a Row.
        Item {
            width: pinRow.width
            height: 26
            anchors.verticalCenter: parent.verticalCenter

            Row {
                id: pinRow
                spacing: 6
                anchors.verticalCenter: parent.verticalCenter
                Rectangle {
                    width: 18; height: 18; radius: 4
                    anchors.verticalCenter: parent.verticalCenter
                    color: tileConfig.keepOpen ? theme.highlight : "transparent"
                    border.width: 1
                    border.color: Qt.rgba(theme.foreground.r, theme.foreground.g,
                                          theme.foreground.b, 0.5)
                    Text {
                        anchors.centerIn: parent
                        visible: tileConfig.keepOpen
                        text: "✓"
                        color: theme.background
                        font.pixelSize: 13
                        font.bold: true
                    }
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Mantener abierto")
                    color: theme.foreground
                    font.pixelSize: 12
                }
            }

            MouseArea {
                anchors.fill: parent
                onClicked: tileConfig.keepOpen = !tileConfig.keepOpen
            }
        }

        Item {
            width: 26; height: 26
            anchors.verticalCenter: parent.verticalCenter
            Rectangle {
                anchors.fill: parent
                radius: 5
                color: closeMouse.containsMouse
                       ? Qt.rgba(theme.highlight.r, theme.highlight.g, theme.highlight.b, 0.35)
                       : "transparent"
            }
            Text {
                anchors.centerIn: parent
                text: "✕"
                color: theme.foreground
                font.pixelSize: 15
            }
            MouseArea {
                id: closeMouse
                anchors.fill: parent
                hoverEnabled: true
                onClicked: win.hideMenu()
            }
        }
    }

    // --- body ---------------------------------------------------------------
    Item {
        id: body
        anchors.fill: parent
        anchors.margins: root.pad
        anchors.topMargin: root.pad + corner.height

        TileSidebar {
            id: sidebar
            visible: root.sidebarVisible
            width: visible ? tileConfig.sidebarWidth : 0
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.left: root.sidebarLeft ? parent.left : undefined
            anchors.right: root.sidebarLeft ? undefined : parent.right
            currentKey: tiles.section
            onSectionPicked: (key) => {
                searchField.text = ""
                tiles.section = key
            }
        }

        Column {
            id: main
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.left: root.sidebarLeft
                          ? (sidebar.visible ? sidebar.right : parent.left) : parent.left
            anchors.right: root.sidebarLeft
                           ? parent.right : (sidebar.visible ? sidebar.left : parent.right)
            anchors.leftMargin: root.sidebarLeft && sidebar.visible ? root.pad : 0
            anchors.rightMargin: !root.sidebarLeft && sidebar.visible ? root.pad : 0
            spacing: 10

            TextField {
                id: searchField
                visible: tileConfig.showSearch
                width: parent.width
                height: visible ? implicitHeight : 0
                placeholderText: qsTr("Buscar aplicaciones…")
                color: theme.foreground
                placeholderTextColor: Qt.rgba(theme.foreground.r, theme.foreground.g,
                                              theme.foreground.b, 0.5)
                // Keep the text from running under the clear button.
                rightPadding: clearBtn.visible ? clearBtn.width + 20 : leftPadding
                background: Rectangle {
                    radius: 6
                    color: Qt.rgba(theme.foreground.r, theme.foreground.g,
                                   theme.foreground.b, 0.10)
                    border.width: searchField.activeFocus ? 1 : 0
                    border.color: theme.highlight
                }
                onTextChanged: tiles.query = text

                // Clear-all: wipe the query and keep the caret in the field.
                Text {
                    id: clearBtn
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.right: parent.right
                    anchors.rightMargin: 10
                    visible: searchField.text.length > 0
                    text: "✕"
                    font.pixelSize: 14
                    color: clearMouse.containsMouse
                           ? theme.highlight
                           : Qt.rgba(theme.foreground.r, theme.foreground.g,
                                     theme.foreground.b, 0.6)
                    MouseArea {
                        id: clearMouse
                        anchors.fill: parent
                        anchors.margins: -6
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            searchField.text = ""
                            searchField.forceActiveFocus()
                        }
                    }
                }

                // Tab completes the query with the first result's name — the
                // fastest way to "the thing I obviously meant".
                Keys.onTabPressed: (event) => {
                    if (root.showResultsList && root.resultEntries.length > 0) {
                        searchField.text = root.resultEntries[0].name
                        searchField.cursorPosition = searchField.text.length
                    }
                    event.accepted = true
                }
                // Down drops into whichever results view is on screen.
                Keys.onDownPressed: {
                    if (root.listShown)
                        searchList.forceActiveFocus()
                    else
                        canvas.selectFirst()
                }
                Keys.onReturnPressed: {
                    if (root.listShown)
                        searchList.launchCurrent()
                    else
                        canvas.launchSelected()
                }
                Keys.onEnterPressed: {
                    if (root.listShown)
                        searchList.launchCurrent()
                    else
                        canvas.launchSelected()
                }
            }

            Item {
                id: canvasRow
                width: parent.width
                height: parent.height - (searchField.visible ? searchField.height + parent.spacing : 0)
                        - (powerRow.visible ? powerRow.height + parent.spacing : 0)

                // A-Z rail on the side opposite the sidebar.
                Column {
                    id: rail
                    visible: root.railVisible
                    width: root.railW
                    anchors.top: parent.top
                    anchors.left: root.sidebarLeft ? undefined : parent.left
                    anchors.right: root.sidebarLeft ? parent.right : undefined
                    spacing: 0

                    property var letters: {
                        tiles.rows; // re-evaluate when the section changes
                        return tiles.availableLetters()
                    }

                    Repeater {
                        model: ["A","B","C","D","E","F","G","H","I","J","K","L","M",
                                "N","O","P","Q","R","S","T","U","V","W","X","Y","Z"]
                        delegate: Item {
                            id: letterCell
                            required property string modelData
                            width: root.railW
                            height: Math.max(14, Math.floor(canvasRow.height / 26))
                            readonly property bool has: rail.letters.indexOf(letterCell.modelData) >= 0
                            Text {
                                anchors.centerIn: parent
                                text: letterCell.modelData
                                font.pixelSize: 11
                                color: letterCell.has
                                       ? (letterMouse.containsMouse ? theme.highlight
                                                                    : theme.foreground)
                                       : Qt.rgba(theme.foreground.r, theme.foreground.g,
                                                 theme.foreground.b, 0.22)
                            }
                            MouseArea {
                                id: letterMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                enabled: letterCell.has
                                onClicked: canvas.jumpToLetter(letterCell.modelData)
                            }
                        }
                    }
                }

                // The column the tab bar and the canvas share. Having it makes
                // every anchor below a plain edge-to-edge one instead of a
                // three-way conditional over sidebar side and A-Z rail.
                Item {
                    id: canvasCol
                    visible: !root.listShown
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    anchors.left: root.sidebarLeft ? parent.left : rail.right
                    anchors.right: root.sidebarLeft ? rail.left : parent.right

                TileGroupTabs {
                    id: groupTabs
                    visible: root.tabsVisible
                    side: tileConfig.groupTabs
                    model: tiles.groups
                    currentIndex: tiles.currentGroup
                    dropTarget: root.dragOverTab

                    anchors.top: (root.tabsVertical || tileConfig.groupTabs === 0)
                                 ? parent.top : undefined
                    anchors.bottom: (root.tabsVertical || tileConfig.groupTabs === 1)
                                    ? parent.bottom : undefined
                    anchors.left: (!root.tabsVertical || tileConfig.groupTabs === 2)
                                  ? parent.left : undefined
                    anchors.right: (!root.tabsVertical || tileConfig.groupTabs === 3)
                                   ? parent.right : undefined
                    // Only the cross axis is set here; the other two anchors
                    // above stretch the bar along its own axis.
                    width: !visible ? 0 : (root.tabsVertical ? implicitWidth : undefined)
                    height: !visible ? 0 : (root.tabsVertical ? undefined : implicitHeight)

                    onPicked: (i) => tiles.currentGroup = i
                    onAddRequested: {
                        var name = win.promptText(qsTr("Nuevo grupo"), qsTr("Nombre:"), "")
                        if (name !== "")
                            tiles.currentGroup = tileLayout.addGroup(tiles.section, name)
                    }
                    onRenameRequested: (i) => {
                        var current = i < tiles.groups.length ? tiles.groups[i].title : ""
                        var name = win.promptText(qsTr("Renombrar grupo"), qsTr("Nombre:"),
                                                  current)
                        if (name !== "")
                            tileLayout.renameGroup(tiles.section, i, name)
                    }
                    onMoveRequested: (i, to) => {
                        tileLayout.moveGroup(tiles.section, i, to)
                        tiles.currentGroup = to
                    }
                    onRemoveRequested: (i) => {
                        if (win.confirm(qsTr("Quitar grupo"),
                                        qsTr("Los mosaicos de este grupo pasan al grupo vecino. ¿Seguir?")))
                            tileLayout.removeGroup(tiles.section, i)
                    }
                }

                Flickable {
                    id: canvasArea
                    anchors.top: tileConfig.groupTabs === 0 && groupTabs.visible
                                 ? groupTabs.bottom : parent.top
                    anchors.topMargin: tileConfig.groupTabs === 0 && groupTabs.visible ? 8 : 0
                    anchors.bottom: tileConfig.groupTabs === 1 && groupTabs.visible
                                    ? groupTabs.top : parent.bottom
                    anchors.bottomMargin: tileConfig.groupTabs === 1 && groupTabs.visible ? 8 : 0
                    anchors.left: tileConfig.groupTabs === 2 && groupTabs.visible
                                  ? groupTabs.right : parent.left
                    anchors.leftMargin: tileConfig.groupTabs === 2 && groupTabs.visible ? 8 : 0
                    anchors.right: tileConfig.groupTabs === 3 && groupTabs.visible
                                   ? groupTabs.left : parent.right
                    anchors.rightMargin: tileConfig.groupTabs === 3 && groupTabs.visible ? 8 : 0
                    clip: true
                    contentWidth: width
                    contentHeight: canvas.height
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar {}

                    Item {
                        id: canvas
                        width: canvasArea.width
                        height: tiles.rows * root.pitch + root.pad

                        // Centered when the matrix is narrower than the space.
                        readonly property int gridW: root.columns * root.pitch - root.gap
                        readonly property int xOffset: Math.max(0, Math.floor((width - gridW) / 2))

                        property Item dragTile: null
                        property string selectedId: ""

                        // --- geometry <-> matrix ---------------------------
                        // Only the current group is on screen, so a tile sits at
                        // its own (col, row): no shared axis to offset against.
                        function tileY(row) {
                            return row * root.pitch
                        }
                        function rowAtY(y) {
                            return Math.max(0, Math.round(y / root.pitch))
                        }
                        function colAtX(x, span) {
                            var c = Math.round((x - canvas.xOffset) / root.pitch)
                            return Math.max(0, Math.min(root.columns - span, c))
                        }

                        // --- drag target, recomputed once per cell ----------
                        readonly property int dragCol: dragTile
                            ? canvas.colAtX(dragTile.x, dragTile.span) : 0
                        readonly property int dragRow: dragTile
                            ? canvas.rowAtY(dragTile.y) : 0
                        // The engine answers, so the ghost never lies: 0 free,
                        // 1 swap, 2 refused. Only re-runs when a cell changes,
                        // because its inputs are integers.
                        readonly property int dragKind: dragTile
                            ? tileLayout.dropKind(tiles.section, dragTile.tileId,
                                                  tiles.currentGroup, canvas.dragCol,
                                                  canvas.dragRow)
                            : 2

                        function dropTile() {
                            if (!dragTile)
                                return
                            // Dropped on another tab: that is a move between
                            // groups, and the only drag route to one now that
                            // the others are off screen.
                            if (root.dragOverTab >= 0 && root.dragOverTab !== tiles.currentGroup) {
                                tileLayout.moveTileToGroup(tiles.section, dragTile.tileId,
                                                           root.dragOverTab)
                                return
                            }
                            tileLayout.moveTile(tiles.section, dragTile.tileId, tiles.currentGroup,
                                                canvas.dragCol, canvas.dragRow)
                        }

                        // --- keyboard selection -----------------------------
                        function selectFirst() {
                            if (tileRepeater.count > 0)
                                canvas.selectedId = tiles.get(0).tileId
                        }
                        function launchSelected() {
                            if (canvas.selectedId !== "")
                                win.launch(canvas.selectedId)
                        }
                        // Nearest tile in a direction, geometrically: the model
                        // order says nothing about where things ended up.
                        function moveSelection(dx, dy) {
                            var here = -1
                            var i
                            for (i = 0; i < tileRepeater.count; ++i) {
                                if (tiles.get(i).tileId === canvas.selectedId) {
                                    here = i
                                    break
                                }
                            }
                            if (here < 0) {
                                canvas.selectFirst()
                                return
                            }
                            var from = tiles.get(here)
                            var best = -1
                            var bestScore = 1e9
                            for (i = 0; i < tileRepeater.count; ++i) {
                                if (i === here)
                                    continue
                                var t = tiles.get(i)
                                var ddx = t.col - from.col
                                var ddy = t.row - from.row
                                if (dx !== 0 && (ddx * dx <= 0 || Math.abs(ddy) > 1))
                                    continue
                                if (dy !== 0 && ddy * dy <= 0)
                                    continue
                                var score = Math.abs(ddx) * (dx !== 0 ? 1 : 3)
                                            + Math.abs(ddy) * (dy !== 0 ? 1 : 3)
                                if (score < bestScore) {
                                    bestScore = score
                                    best = i
                                }
                            }
                            if (best >= 0) {
                                canvas.selectedId = tiles.get(best).tileId
                                canvas.ensureVisible(tiles.get(best))
                            }
                        }
                        function ensureVisible(t) {
                            var y = canvas.tileY(t.row)
                            if (y < canvasArea.contentY)
                                canvasArea.contentY = y
                            else if (y + root.cell > canvasArea.contentY + canvasArea.height)
                                canvasArea.contentY = y + root.cell - canvasArea.height
                        }
                        function jumpToLetter(letter) {
                            var i = tiles.indexOfLetter(letter)
                            if (i < 0)
                                return
                            var t = tiles.get(i)
                            canvas.selectedId = t.tileId
                            canvasArea.contentY = Math.max(0, canvas.tileY(t.row) - root.pitch)
                        }

                        // Right-click on empty canvas.
                        MouseArea {
                            anchors.fill: parent
                            acceptedButtons: Qt.RightButton
                            onClicked: canvasMenu.popup()
                        }

                        Repeater {
                            id: tileRepeater
                            model: tiles
                            delegate: TileMenuTile {
                                canvasItem: canvas
                                ui: root
                            }
                        }

                        // Drop ghost. Green = free, blue = swap, red = refused.
                        Rectangle {
                            visible: canvas.dragTile !== null
                            z: 90
                            x: canvas.xOffset + canvas.dragCol * root.pitch
                            y: canvas.dragRow * root.pitch
                            // Over a tab the drop is a group change, and the
                            // ghost would be pointing at a cell nobody is aiming
                            // for.
                            opacity: root.dragOverTab >= 0
                                     && root.dragOverTab !== tiles.currentGroup ? 0 : 1
                            width: canvas.dragTile
                                   ? canvas.dragTile.span * root.pitch - root.gap : 0
                            height: canvas.dragTile
                                    ? canvas.dragTile.vspan * root.pitch - root.gap : 0
                            radius: 8
                            color: "transparent"
                            border.width: 2
                            border.color: canvas.dragKind === 0 ? "#3daee9"
                                          : canvas.dragKind === 1 ? "#3a7d44" : "#b04a2f"
                        }
                    }
                }
                } // canvasCol

                // The vertical "en filas" results / recent strip. Occupies the
                // same slot as canvasCol; only one of the two is ever visible.
                TileSearchList {
                    id: searchList
                    visible: root.listShown
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    anchors.left: root.sidebarLeft ? parent.left : rail.right
                    anchors.right: root.sidebarLeft ? rail.left : parent.right
                    entries: root.listEntries
                    onLaunch: (id) => win.launch(id)
                    onAtTop: searchField.forceActiveFocus()
                }
            }

            // --- session / power row -------------------------------------
            Row {
                id: powerRow
                visible: power && power.available && tileConfig.showPower
                width: parent.width
                height: visible ? 40 : 0
                spacing: 8
                layoutDirection: Qt.RightToLeft

                Repeater {
                    model: [
                        { ic: "system-shutdown",    act: "shutdown", tip: qsTr("Apagar…") },
                        { ic: "system-reboot",      act: "reboot",   tip: qsTr("Reiniciar…") },
                        { ic: "system-log-out",     act: "logout",   tip: qsTr("Cerrar sesión…") },
                        { ic: "system-suspend",     act: "suspend",  tip: qsTr("Suspender") },
                        { ic: "system-lock-screen", act: "lock",     tip: qsTr("Bloquear") }
                    ]
                    delegate: Item {
                        id: powerBtn
                        required property var modelData
                        width: 36
                        height: 36
                        Rectangle {
                            anchors.fill: parent
                            radius: 6
                            color: powerMouse.containsMouse
                                   ? Qt.rgba(theme.highlight.r, theme.highlight.g,
                                             theme.highlight.b, 0.30)
                                   : "transparent"
                        }
                        Image {
                            anchors.centerIn: parent
                            width: 22
                            height: 22
                            source: "image://icon/" + powerBtn.modelData.ic + "@" + theme.revision
                            sourceSize: Qt.size(22 * Screen.devicePixelRatio,
                                                22 * Screen.devicePixelRatio)
                        }
                        ToolTip {
                            popupType: Popup.Window
                            visible: powerMouse.containsMouse
                            delay: 400
                            text: powerBtn.modelData.tip
                        }
                        MouseArea {
                            id: powerMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: {
                                win.hideMenu()
                                power[powerBtn.modelData.act]()
                            }
                        }
                    }
                }
            }
        }
    }

    // --- the tile context menu, one for the whole canvas -------------------
    // Declaring it inside the delegate would build one Menu (plus its colour
    // Instantiator, ~25 objects) per tile: measured at 783 MB and 3.5 s to open
    // "All Applications" with 450 tiles, against 182 MB and 0.4 s once shared.
    property Item ctxTile: null
    function openTileMenu(tile) {
        root.ctxTile = tile
        tileCtxMenu.popup()
    }

    Menu {
        id: tileCtxMenu
        popupType: Popup.Window
        // The menu mixes checkable items (which reserve an indicator column) with
        // IconMenuItems (which draw their own icon), so the implicit width comes
        // out a few pixels short of the longest label and clips it.
        width: Math.max(implicitWidth + 16, 240)
        readonly property Item t: root.ctxTile
        readonly property string tid: t ? t.tileId : ""

        TileSizeMenu {
            span: tileCtxMenu.t ? tileCtxMenu.t.span : 1
            vspan: tileCtxMenu.t ? tileCtxMenu.t.vspan : 1
            enabled: !tiles.searching
            onPicked: (w, h) => tileLayout.resizeTile(tiles.section, tileCtxMenu.tid, w, h)
        }

        Menu {
            id: colorMenu
            title: qsTr("Color de fondo")
            popupType: Popup.Window

            // The same eight quick colors the dock offers in its own right-click
            // submenu (they live in kdock's shared settings file).
            Instantiator {
                model: tileConfig.presetColors
                delegate: MenuItem {
                    id: presetItem
                    required property int index
                    required property string modelData
                    readonly property color swatch: modelData
                    checkable: true
                    checked: tileCtxMenu.t && tileCtxMenu.t.background.length > 0
                             && Qt.colorEqual(tileCtxMenu.t.background, presetItem.swatch)
                    onTriggered: tileLayout.setTileProperty(tiles.section, tileCtxMenu.tid,
                                                            "bg", presetItem.modelData)
                    contentItem: Row {
                        spacing: 8
                        Rectangle {
                            width: 22; height: 22; radius: 4
                            color: presetItem.swatch
                            border.width: 1
                            border.color: Qt.rgba(theme.foreground.r, theme.foreground.g,
                                                  theme.foreground.b, 0.35)
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Text {
                            text: qsTr("Color %1").arg(presetItem.index + 1)
                            color: presetItem.palette.windowText
                            font: presetItem.font
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }
                }
                onObjectAdded: (i, o) => colorMenu.insertItem(i, o)
                onObjectRemoved: (i, o) => colorMenu.removeItem(o)
            }

            MenuSeparator {}
            IconMenuItem {
                text: qsTr("Otro color…")
                iconName: "color-picker"
                onTriggered: {
                    var picked = win.pickColor(tileCtxMenu.t ? tileCtxMenu.t.background : "")
                    if (picked.length > 0)
                        tileLayout.setTileProperty(tiles.section, tileCtxMenu.tid, "bg", picked)
                }
            }
            IconMenuItem {
                text: qsTr("Sin color")
                iconName: "edit-undo"
                checkable: true
                checked: tileCtxMenu.t && tileCtxMenu.t.background.length === 0
                onTriggered: tileLayout.setTileProperty(tiles.section, tileCtxMenu.tid, "bg", "")
            }
        }

        Menu {
            title: qsTr("Imagen de fondo")
            popupType: Popup.Window
            IconMenuItem {
                text: qsTr("Elegir imagen…")
                iconName: "insert-image"
                onTriggered: {
                    var path = win.pickImage(tileCtxMenu.t ? tileCtxMenu.t.image : "")
                    if (path.length > 0)
                        tileLayout.setTileProperty(tiles.section, tileCtxMenu.tid, "image", path)
                }
            }
            IconMenuItem {
                text: qsTr("Quitar imagen")
                iconName: "edit-delete"
                enabled: tileCtxMenu.t && tileCtxMenu.t.image.length > 0
                onTriggered: tileLayout.setTileProperty(tiles.section, tileCtxMenu.tid, "image", "")
            }
        }

        MenuSeparator {}

        // With one group on screen at a time, this and dropping the tile on a tab
        // are the two ways to move it to another group.
        Menu {
            id: groupMenu
            title: qsTr("Mover a grupo")
            popupType: Popup.Window
            width: Math.max(implicitWidth + 16, 220)
            enabled: !tiles.searching

            Instantiator {
                model: tiles.searching ? [] : tiles.groups
                delegate: MenuItem {
                    id: groupItem
                    required property var modelData
                    text: groupItem.modelData.title.length > 0
                          ? groupItem.modelData.title
                          : qsTr("Grupo %1").arg(groupItem.modelData.index + 1)
                    checkable: true
                    checked: tileCtxMenu.t
                             && tileCtxMenu.t.group === groupItem.modelData.index
                    onTriggered: tileLayout.moveTileToGroup(tiles.section, tileCtxMenu.tid,
                                                            groupItem.modelData.index)
                }
                onObjectAdded: (i, o) => groupMenu.insertItem(i, o)
                onObjectRemoved: (i, o) => groupMenu.removeItem(o)
            }

            MenuSeparator {}
            IconMenuItem {
                text: qsTr("Grupo nuevo…")
                iconName: "list-add"
                onTriggered: {
                    var name = win.promptText(qsTr("Nuevo grupo"), qsTr("Nombre:"), "")
                    if (name === "")
                        return
                    var g = tileLayout.addGroup(tiles.section, name)
                    tileLayout.moveTileToGroup(tiles.section, tileCtxMenu.tid, g)
                }
            }
        }

        MenuSeparator {}

        MenuItem {
            text: qsTr("Mostrar ícono")
            checkable: true
            checked: tileCtxMenu.t ? tileCtxMenu.t.showIcon : false
            onTriggered: tileLayout.setTileProperty(tiles.section, tileCtxMenu.tid, "showIcon",
                                                    !tileCtxMenu.t.showIcon)
        }
        MenuItem {
            text: qsTr("Mostrar nombre")
            checkable: true
            checked: tileCtxMenu.t ? tileCtxMenu.t.showLabel : false
            onTriggered: tileLayout.setTileProperty(tiles.section, tileCtxMenu.tid, "showLabel",
                                                    !tileCtxMenu.t.showLabel)
        }
        IconMenuItem {
            text: qsTr("Renombrar…")
            iconName: "edit-rename"
            onTriggered: {
                var newName = win.promptText(qsTr("Renombrar mosaico"), qsTr("Nombre:"),
                                             tileCtxMenu.t ? tileCtxMenu.t.name : "")
                if (newName.length > 0)
                    tileLayout.setTileProperty(tiles.section, tileCtxMenu.tid, "label", newName)
            }
        }
        IconMenuItem {
            text: qsTr("Cambiar ícono…")
            iconName: "preferences-desktop-icons"
            onTriggered: {
                var newIcon = win.pickIcon(tileCtxMenu.t ? tileCtxMenu.t.icon : "")
                if (newIcon.length > 0)
                    tileLayout.setTileProperty(tiles.section, tileCtxMenu.tid, "icon", newIcon)
            }
        }

        MenuSeparator {}

        IconMenuItem {
            text: tileCtxMenu.t && tileCtxMenu.t.favorite ? qsTr("Quitar de favoritos")
                                                          : qsTr("Agregar a favoritos")
            iconName: tileCtxMenu.t && tileCtxMenu.t.favorite ? "non-starred-symbolic"
                                                              : "starred-symbolic"
            onTriggered: appMenu.toggleFavorite(tileCtxMenu.tid)
        }
        IconMenuItem {
            text: qsTr("Restablecer mosaico")
            iconName: "edit-undo"
            enabled: !tiles.searching
            onTriggered: tileLayout.resetTile(tiles.section, tileCtxMenu.tid)
        }
    }

    Menu {
        id: canvasMenu
        popupType: Popup.Window
        width: Math.max(implicitWidth + 16, 240)
        IconMenuItem {
            text: qsTr("Nuevo grupo…")
            iconName: "list-add"
            enabled: !tiles.searching
            onTriggered: {
                var name = win.promptText(qsTr("Nuevo grupo"), qsTr("Nombre:"), "")
                if (name !== "")
                    tileLayout.addGroup(tiles.section, name)
            }
        }
        IconMenuItem {
            text: qsTr("Restablecer esta sección")
            iconName: "edit-undo"
            enabled: !tiles.searching && tiles.customized
            onTriggered: {
                if (win.confirm(qsTr("Restablecer sección"),
                                qsTr("Los mosaicos de esta sección vuelven a acomodarse solos. ¿Seguir?")))
                    tileLayout.resetSection(tiles.section)
            }
        }
        MenuSeparator {}
        IconMenuItem {
            text: qsTr("Configurar el menú…")
            iconName: "configure"
            onTriggered: win.openSettings()
        }
    }

    // --- keyboard -----------------------------------------------------------
    Keys.onEscapePressed: win.hideMenu()
    Keys.onLeftPressed: canvas.moveSelection(-1, 0)
    Keys.onRightPressed: canvas.moveSelection(1, 0)
    Keys.onUpPressed: canvas.moveSelection(0, -1)
    Keys.onDownPressed: canvas.moveSelection(0, 1)
    Keys.onReturnPressed: canvas.launchSelected()
    Keys.onEnterPressed: canvas.launchSelected()

    // Reset the query and hand focus to the search box. Called from onVisibleChanged
    // (first construction) AND from C++ showOn() on every reopen: reopening from the
    // widget re-shows the same window without the root Item's `visible` ever changing,
    // so onVisibleChanged does not fire the second time around.
    function prepareForShow() {
        searchField.text = ""
        canvas.selectedId = ""
        if (tileConfig.showSearch)
            searchField.forceActiveFocus()
        else
            root.forceActiveFocus()
    }

    onVisibleChanged: {
        if (visible)
            prepareForShow()
    }
}
