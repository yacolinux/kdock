// ControlManager.qml — the whole panel: background, tab bar, the Principal grid
// of cards, and the full view of whichever section is selected.
//
// The surface is a layer-shell one anchored to a screen edge (see cmwindow.h),
// so this item is handed exactly the size the config asks for: there is no
// geometry math here about where the dock is.
//
// The card context menu is declared *once*, at the root, and parameterized by
// "which card opened it". Declaring it inside the delegate would build one Menu
// (and one Popup.Window, i.e. one render thread) per card — the trap the tile
// menu paid 783 MB for.

import QtQuick
import QtQuick.Controls

Item {
    id: root
    focus: true

    readonly property int pad: 12
    readonly property int gap: cmConfig.cellSpacing

    readonly property color baseColor: (cmConfig.backgroundMode === 1
                                        && cmConfig.backgroundColorSet)
                                       ? cmConfig.backgroundColor : theme.background

    // Text colour for everything drawn straight on the panel: the tab bar, the
    // corner controls, and a full-size section. A custom panel colour can be
    // anything, and the theme foreground vanishes on half of them, so the same
    // perceptual test the dock uses for its clock picks one of the two extremes
    // instead. Transparency deliberately plays no part (a translucent dark
    // panel still reads as dark), and without a custom colour nothing changes:
    // the KDE scheme's own foreground over the KDE scheme's own background is
    // exactly what the user asked for.
    readonly property bool baseIsLight: root.isLight(root.baseColor)
    readonly property color panelTextColor:
        (cmConfig.foregroundMode === 1 && cmConfig.foregroundColorSet)
        ? cmConfig.foregroundColor
        : ((cmConfig.backgroundMode === 1 && cmConfig.backgroundColorSet)
           ? (root.baseIsLight ? "#141414" : "#F2F2F2")
           : theme.foreground)

    // Perceptual luminance, alpha ignored on purpose: a half-transparent white
    // is still a light surface, and blending it with whatever is behind would
    // make the answer depend on the wallpaper.
    function isLight(c) {
        return (0.299 * c.r + 0.587 * c.g + 0.114 * c.b) > 0.5
    }

    // --- tabs ---------------------------------------------------------------
    readonly property var tabModel: {
        var out = [{ id: "", label: qsTr("Principal"), icon: "view-list-details" }]
        var ids = cmConfig.visibleTabs()
        var all = win.sections
        for (var i = 0; i < ids.length; ++i) {
            for (var j = 0; j < all.length; ++j) {
                if (all[j].id === ids[i]) {
                    out.push({ id: all[j].id, label: all[j].label, icon: all[j].icon })
                    break
                }
            }
        }
        return out
    }

    // Every section that has a full view (hasTab), plus Principal, for the
    // right-click "Secciones" menu — the desktop canvas has no tab bar, so this
    // is how the user reaches any section. Independent of which tabs are enabled.
    readonly property var sectionMenuModel: {
        var out = [{ id: "", label: qsTr("Principal"), icon: "view-list-details" }]
        var all = win.sections
        for (var i = 0; i < all.length; ++i) {
            if (all[i].hasTab)
                out.push({ id: all[i].id, label: all[i].label, icon: all[i].icon })
        }
        return out
    }

    // --- grid metrics -------------------------------------------------------
    // The available width never depends on the cell size, so none of this feeds
    // back into itself. Cells can be taller than wide: the width follows the
    // stretch rules, the height is cmConfig.cellHeight.
    readonly property int canvasAvail: Math.max(120, canvasArea.width)
    readonly property int columns: cmConfig.columns > 0
        ? cmConfig.columns
        : Math.max(1, Math.floor((canvasAvail + gap) / (cmConfig.cellSize + gap)))
    readonly property int cellW: cmConfig.cellStretch
        ? Math.max(cmConfig.cellMin,
                   Math.min(cmConfig.cellMax,
                            Math.floor((canvasAvail + gap) / columns) - gap))
        : cmConfig.cellSize
    readonly property int cellH: cmConfig.cellHeight
    readonly property int pitchW: cellW + gap
    readonly property int pitchH: cellH + gap

    // Pointer position while a card is being dragged, in root coordinates.
    property point dragPointer: Qt.point(-1, -1)

    onColumnsChanged: cmLayout.setAutoColumns(columns)
    Component.onCompleted: cmLayout.setAutoColumns(columns)

    // --- background ---------------------------------------------------------
    Rectangle {
        id: panelBg
        anchors.fill: parent
        radius: cmConfig.cornerRadius
        color: Qt.rgba(root.baseColor.r, root.baseColor.g, root.baseColor.b,
                       cmConfig.backgroundOpacity)
        clip: true

        Image {
            anchors.fill: parent
            visible: cmConfig.backgroundImage.length > 0
            source: cmConfig.backgroundImageUrl
            fillMode: Image.PreserveAspectCrop
            opacity: cmConfig.backgroundOpacity
            asynchronous: true
        }
    }

    // Closing when the pointer leaves is opt-in: on a panel full of sliders it
    // is easy to overshoot, so it is off by default.
    HoverHandler {
        id: panelHover
    }
    onDragPointerChanged: leaveTimer.stop()
    Timer {
        id: leaveTimer
        interval: 700
        onTriggered: {
            if (!panelHover.hovered && !cmConfig.keepOpen && cmConfig.closeOnLeave)
                win.hidePanel()
        }
    }
    Connections {
        target: panelHover
        function onHoveredChanged() {
            if (panelHover.hovered)
                leaveTimer.stop()
            else if (cmConfig.closeOnLeave && !cmConfig.keepOpen)
                leaveTimer.restart()
        }
    }

    // --- corner controls ----------------------------------------------------
    Row {
        id: corner
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.topMargin: 6
        anchors.rightMargin: 8
        spacing: 6
        z: 50

        // The pin: with it on, none of the automatic close paths fire and the
        // panel behaves like a fixed piece of the desktop.
        Item {
            width: pinRow.width
            height: Math.round(24 * cmConfig.fontScale)
            anchors.verticalCenter: parent.verticalCenter

            Row {
                id: pinRow
                spacing: 6
                anchors.verticalCenter: parent.verticalCenter
                Rectangle {
                    width: 15; height: 15; radius: 4
                    anchors.verticalCenter: parent.verticalCenter
                    color: cmConfig.keepOpen ? theme.highlight : "transparent"
                    border.width: 1
                    border.color: Qt.rgba(root.panelTextColor.r, root.panelTextColor.g,
                                          root.panelTextColor.b, 0.5)
                    Text {
                        anchors.centerIn: parent
                        visible: cmConfig.keepOpen
                        text: "✓"
                        color: theme.background
                        font.pixelSize: Math.max(7, Math.round((11) * cmConfig.fontScale))
                        font.bold: true
                    }
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Fijo")
                    color: root.panelTextColor
                    opacity: 0.8
                    font.pixelSize: Math.max(7, Math.round((11) * cmConfig.fontScale))
                }
            }
            MouseArea {
                anchors.fill: parent
                onClicked: cmConfig.keepOpen = !cmConfig.keepOpen
            }
        }

        Item {
            width: Math.round(22 * cmConfig.fontScale)
            height: Math.round(22 * cmConfig.fontScale)
            anchors.verticalCenter: parent.verticalCenter
            Rectangle {
                anchors.fill: parent
                radius: 5
                color: cfgMouse.containsMouse
                       ? Qt.rgba(theme.highlight.r, theme.highlight.g, theme.highlight.b, 0.35)
                       : "transparent"
            }
            Image {
                anchors.centerIn: parent
                width: 14
                height: 14
                source: "image://icon/configure" + win.iconSuffix
                sourceSize: Qt.size(14 * Screen.devicePixelRatio, 14 * Screen.devicePixelRatio)
            }
            MouseArea {
                id: cfgMouse
                anchors.fill: parent
                hoverEnabled: true
                onClicked: win.openSettings()
            }
        }

        Item {
            width: Math.round(22 * cmConfig.fontScale)
            height: Math.round(22 * cmConfig.fontScale)
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
                color: root.panelTextColor
                font.pixelSize: Math.max(7, Math.round((13) * cmConfig.fontScale))
            }
            MouseArea {
                id: closeMouse
                anchors.fill: parent
                hoverEnabled: true
                onClicked: win.hidePanel()
            }
        }
    }

    // --- body ---------------------------------------------------------------
    // The desktop canvas has no tab bar (kdock-desktop only): the sections are
    // off and the bar has no use yet, so it is disabled and the body fills the
    // whole surface. When the bar is repurposed later this comes back.
    CmTabs {
        id: tabBar
        visible: false
        anchors.left: parent.left
        anchors.right: corner.left
        anchors.leftMargin: root.pad
        anchors.rightMargin: root.pad
        anchors.top: parent.top
        anchors.topMargin: 4
        anchors.bottomMargin: 4
        model: root.tabModel
        fg: root.panelTextColor
        currentId: win.currentTab
        onPicked: (id) => win.currentTab = id
    }

    Item {
        id: body
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: root.pad
        anchors.rightMargin: root.pad
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.topMargin: 6
        anchors.bottomMargin: 6

        // Background right-click: opens the canvas menu (the "Secciones"
        // navigation among the rest) from anywhere on the desktop — including
        // while a full section view is up, where the Principal grid's own
        // right-click MouseArea is not on screen. Behind the content (z: -1), so
        // a section's own controls keep their clicks; a right-click on empty
        // space falls through to here.
        MouseArea {
            anchors.fill: parent
            z: -1
            acceptedButtons: Qt.RightButton
            onClicked: canvasMenu.popup()
        }

        // --- one section, full size ---
        CmSectionView {
            anchors.fill: parent
            visible: win.currentTab !== ""
            // A hidden Loader still holds a live item; freeing it is what stops
            // the MPRIS poll and the access-point enumeration of the section the
            // user just left.
            active: win.currentTab !== ""
            sectionId: win.currentTab
            fg: root.panelTextColor
            compact: false
        }

        // --- the Principal grid ---
        Flickable {
            id: canvasArea
            anchors.fill: parent
            visible: win.currentTab === ""
            clip: true
            // AutoFlickIfNeeded and not the default AutoFlickDirection: the latter
            // calls itself flickable whenever contentHeight != height — content
            // *shorter* than the viewport included — and then steals the drag of
            // any slider inside it (see CmSlider). This one only flicks when there
            // is something to scroll.
            flickableDirection: Flickable.AutoFlickIfNeeded
            contentWidth: width
            contentHeight: canvas.height
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}

            Item {
                id: canvas
                width: canvasArea.width
                // At least the viewport height, so the empty-area right-click
                // (the RightButton MouseArea below fills this item) covers the
                // whole canvas — with no cards the content is only `pad` tall.
                height: Math.max(canvasArea.height, cards.rows * root.pitchH + root.pad)

                // Centered when the matrix is narrower than the space.
                readonly property int gridW: root.columns * root.pitchW - root.gap
                readonly property int xOffset: Math.max(0, Math.floor((width - gridW) / 2))

                property Item dragCard: null

                function rowAtY(y) {
                    return Math.max(0, Math.round(y / root.pitchH))
                }
                function colAtX(x, span) {
                    var c = Math.round((x - canvas.xOffset) / root.pitchW)
                    return Math.max(0, Math.min(root.columns - span, c))
                }

                // --- drag target, recomputed once per cell ------------------
                readonly property int dragCol: dragCard
                    ? canvas.colAtX(dragCard.x, dragCard.span) : 0
                readonly property int dragRow: dragCard
                    ? canvas.rowAtY(dragCard.y) : 0
                // The engine answers, so the ghost never lies: 0 free, 1 swap,
                // 2 refused. Only re-runs when a cell changes, because its
                // inputs are integers.
                readonly property int dragKind: dragCard
                    ? cmLayout.dropKind(dragCard.cardId, canvas.dragCol, canvas.dragRow)
                    : 2

                function dropCard() {
                    if (!dragCard)
                        return
                    cmLayout.moveCard(dragCard.cardId, canvas.dragCol, canvas.dragRow)
                }

                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.RightButton
                    onClicked: canvasMenu.popup()
                }

                Repeater {
                    id: cardRepeater
                    model: cards
                    delegate: CmCard {
                        canvasItem: canvas
                        ui: root
                    }
                }

                // Drop ghost. Blue = free, green = swap, red = refused.
                Rectangle {
                    visible: canvas.dragCard !== null
                    z: 90
                    x: canvas.xOffset + canvas.dragCol * root.pitchW
                    y: canvas.dragRow * root.pitchH
                    width: canvas.dragCard ? canvas.dragCard.span * root.pitchW - root.gap : 0
                    height: canvas.dragCard ? canvas.dragCard.vspan * root.pitchH - root.gap : 0
                    radius: 10
                    color: "transparent"
                    border.width: 2
                    border.color: canvas.dragKind === 0 ? "#3daee9"
                                  : canvas.dragKind === 1 ? "#3a7d44" : "#b04a2f"
                }
            }
        }

        // Nothing on Principal at all: say what to do instead of showing an
        // empty rectangle that reads as a bug.
        Text {
            anchors.centerIn: parent
            visible: win.currentTab === "" && cards.count === 0
            width: parent.width * 0.7
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            text: qsTr("No hay ninguna tarjeta en Principal. Elegí cuáles mostrar en "
                       + "Configuración → Secciones.")
            color: root.panelTextColor
            opacity: 0.55
            font.pixelSize: Math.max(7, Math.round((12) * cmConfig.fontScale))
        }
    }

    // --- corner resize grips -------------------------------------------------
    // A layer surface has no compositor resize, so the panel resizes itself:
    // dragging a corner maps the pointer delta to a new width/height (see
    // CmCornerGrip and win.setPanelSize). The top-right corner belongs to the
    // pin/config/close controls; the top-left one only makes sense when the
    // tab bar is not sitting under it.
    CmCornerGrip {
        corner: 0
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        fg: root.panelTextColor
        currentW: root.width
        currentH: root.height
    }
    CmCornerGrip {
        corner: 1
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        fg: root.panelTextColor
        currentW: root.width
        currentH: root.height
    }
    CmCornerGrip {
        corner: 2
        anchors.left: parent.left
        anchors.top: parent.top
        fg: root.panelTextColor
        currentW: root.width
        currentH: root.height
        visible: cmConfig.tabsPosition === 1
    }

    // --- the card context menu, one for the whole canvas --------------------
    property Item ctxCard: null
    function openCardMenu(card) {
        root.ctxCard = card
        cardMenu.popup()
    }

    Menu {
        id: cardMenu
        popupType: Popup.Window
        // A translated label can be noticeably longer than the capabase one, and
        // a QtQuick Menu does not size itself to its widest item: without this
        // the last letters are clipped by the popup border (see CLAUDE.md). The
        // 64 px of slack is measured, not decorative.
        width: Math.max(implicitWidth + 64, 240)
        readonly property Item c: root.ctxCard
        readonly property string cid: c ? c.cardId : ""

        Menu {
            title: qsTr("Tamaño")
            popupType: Popup.Window
            width: Math.max(implicitWidth + 64, 200)

            Repeater {
                model: [
                    { w: 1, h: 1 }, { w: 2, h: 1 }, { w: 2, h: 2 }, { w: 3, h: 1 },
                    { w: 3, h: 2 }, { w: 3, h: 3 }, { w: 4, h: 2 }, { w: 4, h: 3 },
                    { w: 6, h: 3 }
                ]
                delegate: MenuItem {
                    required property var modelData
                    text: modelData.w + " × " + modelData.h
                    checkable: true
                    checked: cardMenu.c && cardMenu.c.span === modelData.w
                             && cardMenu.c.vspan === modelData.h
                    onTriggered: cmLayout.resizeCard(cardMenu.cid, modelData.w, modelData.h)
                }
            }
        }

        Menu {
            id: colorMenu
            title: qsTr("Color de fondo")
            popupType: Popup.Window
            width: Math.max(implicitWidth + 64, 200)

            // The same eight quick colors the dock offers in its own right-click
            // submenu (they live in kdock's shared settings file).
            Instantiator {
                model: cmConfig.presetColors
                delegate: MenuItem {
                    id: presetItem
                    required property int index
                    required property string modelData
                    readonly property color swatch: modelData
                    checkable: true
                    checked: cardMenu.c && cardMenu.c.background.length > 0
                             && Qt.colorEqual(cardMenu.c.background, presetItem.swatch)
                    onTriggered: cmLayout.setCardProperty(cardMenu.cid, "bg",
                                                          presetItem.modelData)
                    contentItem: Row {
                        spacing: 8
                        Rectangle {
                            width: 20; height: 20; radius: 4
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
                    var picked = win.pickColor(cardMenu.c ? cardMenu.c.background : "")
                    if (picked.length > 0)
                        cmLayout.setCardProperty(cardMenu.cid, "bg", picked)
                }
            }
            IconMenuItem {
                text: qsTr("Sin color")
                iconName: "edit-undo"
                checkable: true
                checked: cardMenu.c && cardMenu.c.background.length === 0
                onTriggered: cmLayout.setCardProperty(cardMenu.cid, "bg", "")
            }
        }

        // The font colour of one card. Left alone (the "Automático" entry) it
        // is derived from the card's own background, which is right almost
        // always; this is the escape hatch for the colour where it is not.
        Menu {
            id: fgMenu
            title: qsTr("Color de fuente")
            popupType: Popup.Window
            width: Math.max(implicitWidth + 64, 200)

            Instantiator {
                model: cmConfig.presetColors
                delegate: MenuItem {
                    id: fgPresetItem
                    required property int index
                    required property string modelData
                    readonly property color swatch: modelData
                    checkable: true
                    checked: cardMenu.c && cardMenu.c.foreground.length > 0
                             && Qt.colorEqual(cardMenu.c.foreground, fgPresetItem.swatch)
                    onTriggered: cmLayout.setCardProperty(cardMenu.cid, "fg",
                                                          fgPresetItem.modelData)
                    contentItem: Row {
                        spacing: 8
                        Rectangle {
                            width: 20; height: 20; radius: 4
                            color: fgPresetItem.swatch
                            border.width: 1
                            border.color: Qt.rgba(theme.foreground.r, theme.foreground.g,
                                                  theme.foreground.b, 0.35)
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Text {
                            text: qsTr("Color %1").arg(fgPresetItem.index + 1)
                            color: fgPresetItem.palette.windowText
                            font: fgPresetItem.font
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }
                }
                onObjectAdded: (i, o) => fgMenu.insertItem(i, o)
                onObjectRemoved: (i, o) => fgMenu.removeItem(o)
            }

            MenuSeparator {}
            IconMenuItem {
                text: qsTr("Otro color…")
                iconName: "color-picker"
                onTriggered: {
                    var picked = win.pickColor(cardMenu.c ? cardMenu.c.foreground : "")
                    if (picked.length > 0)
                        cmLayout.setCardProperty(cardMenu.cid, "fg", picked)
                }
            }
            IconMenuItem {
                text: qsTr("Automático")
                iconName: "edit-undo"
                checkable: true
                checked: cardMenu.c && cardMenu.c.foreground.length === 0
                onTriggered: cmLayout.setCardProperty(cardMenu.cid, "fg", "")
            }
        }

        // Background transparency of this one card. The levels are transparency
        // (higher = more see-through); the stored value is the opposite opacity
        // percentage (100 - level). "General" drops the override and follows the
        // panel-wide setting (Configuración → Opacidad de los widgets).
        Menu {
            id: cardTransparencyMenu
            title: qsTr("Transparencia")
            popupType: Popup.Window
            width: Math.max(implicitWidth + 64, 200)

            Repeater {
                model: [10, 20, 40, 80]
                delegate: MenuItem {
                    required property var modelData
                    text: modelData + " %"
                    checkable: true
                    checked: cardMenu.c && cardMenu.c.cardOpacity >= 0
                             && cardMenu.c.cardOpacity === (100 - modelData)
                    onTriggered: cmLayout.setCardProperty(cardMenu.cid, "opacity",
                                                          100 - modelData)
                }
            }

            MenuSeparator {}
            IconMenuItem {
                text: qsTr("Opaco")
                checkable: true
                checked: cardMenu.c && cardMenu.c.cardOpacity === 100
                onTriggered: cmLayout.setCardProperty(cardMenu.cid, "opacity", 100)
            }
            IconMenuItem {
                text: qsTr("General")
                iconName: "edit-undo"
                checkable: true
                checked: cardMenu.c && cardMenu.c.cardOpacity < 0
                onTriggered: cmLayout.setCardProperty(cardMenu.cid, "opacity", -1)
            }
        }

        MenuSeparator {}

        MenuItem {
            text: qsTr("Mostrar el título")
            checkable: true
            checked: cardMenu.c ? cardMenu.c.showTitle : false
            onTriggered: cmLayout.setCardProperty(cardMenu.cid, "showTitle",
                                                  !cardMenu.c.showTitle)
        }
        IconMenuItem {
            text: qsTr("Renombrar…")
            iconName: "edit-rename"
            onTriggered: {
                var newName = win.promptText(qsTr("Renombrar la tarjeta"), qsTr("Nombre:"),
                                             cardMenu.c ? cardMenu.c.name : "")
                if (newName.length > 0)
                    cmLayout.setCardProperty(cardMenu.cid, "label", newName)
            }
        }
        IconMenuItem {
            text: qsTr("Ver la sección completa")
            iconName: "go-next"
            onTriggered: win.currentTab = cardMenu.cid
        }

        MenuSeparator {}

        IconMenuItem {
            text: qsTr("Quitar de Principal")
            iconName: "list-remove"
            onTriggered: cmConfig.setCardEnabled(cardMenu.cid, false)
        }
        IconMenuItem {
            text: qsTr("Restablecer la tarjeta")
            iconName: "edit-undo"
            onTriggered: cmLayout.resetCard(cardMenu.cid)
        }
    }

    Menu {
        id: canvasMenu
        popupType: Popup.Window
        width: Math.max(implicitWidth + 64, 240)

        // No tab bar on the desktop canvas, so every section's full view is
        // reached from here. Lists Principal plus every section that has a view,
        // whether or not its tab is enabled.
        Menu {
            id: sectionsMenu
            title: qsTr("Secciones")
            popupType: Popup.Window
            width: Math.max(implicitWidth + 64, 220)
            Repeater {
                model: root.sectionMenuModel
                delegate: IconMenuItem {
                    required property var modelData
                    text: modelData.label
                    iconName: modelData.icon
                    checkable: true
                    checked: win.currentTab === modelData.id
                    onTriggered: win.currentTab = modelData.id
                }
            }
        }

        Menu {
            id: wallpaperFolderMenu
            title: qsTr("Carpeta de wallpaper")
            popupType: Popup.Window
            width: Math.max(implicitWidth + 64, 280)
            property string currentFolder: ""

            ListModel {
                id: wallpaperFolderModel
            }

            function refreshFolders() {
                wallpaperFolderModel.clear()
                var folders = win.slideshowWallpaperFolders
                wallpaperFolderMenu.currentFolder = win.currentSlideshowWallpaperFolder()
                for (var i = 0; i < folders.length; ++i)
                    wallpaperFolderModel.append({ folder: folders[i] })
            }

            function folderName(path) {
                var trimmed = path
                while (trimmed.length > 1 && trimmed.endsWith("/"))
                    trimmed = trimmed.slice(0, -1)
                if (trimmed === "/")
                    return trimmed
                var slash = trimmed.lastIndexOf("/")
                return slash >= 0 ? trimmed.slice(slash + 1) : trimmed
            }

            onAboutToShow: refreshFolders()

            Repeater {
                model: wallpaperFolderModel
                delegate: IconMenuItem {
                    required property string folder
                    text: wallpaperFolderMenu.folderName(folder)
                    checkable: true
                    checked: folder === wallpaperFolderMenu.currentFolder
                    onTriggered: {
                        win.setCurrentSlideshowWallpaperFolder(folder)
                        wallpaperFolderMenu.currentFolder = folder
                    }
                }
            }

            MenuSeparator { visible: wallpaperFolderModel.count > 0 }
            IconMenuItem {
                text: qsTr("Configurar")
                iconName: "configure"
                enabled: typeof dock !== "undefined" && dock && dock.available
                onTriggered: {
                    if (typeof dock !== "undefined" && dock && dock.available)
                        dock.openWallpaperSettings()
                }
            }
        }

        Menu {
            id: screensaverMenu
            title: qsTr("Salvapantallas")
            popupType: Popup.Window
            width: Math.max(implicitWidth + 64, 280)

            IconMenuItem {
                text: qsTr("Slideshow")
                iconName: "view-preview"
                enabled: dock && dock.available
                onTriggered: if (dock && dock.available)
                                 dock.activateScreensaver(win.screenName, 0, "")
            }

            MenuSeparator {}

            Repeater {
                model: [
                    { page: "bouncing-ball.html", label: qsTr("Bouncing ball") },
                    { page: "fade-out.html", label: qsTr("Fade out") },
                    { page: "fish.html", label: qsTr("Fish") },
                    { page: "flying-toasters.html", label: qsTr("Flying toasters") },
                    { page: "globe.html", label: qsTr("Globe") },
                    { page: "hard-rain.html", label: qsTr("Hard rain") },
                    { page: "logo.html", label: qsTr("Logo") },
                    { page: "messages.html", label: qsTr("Messages") },
                    { page: "messages2.html", label: qsTr("Messages 2") },
                    { page: "rainstorm.html", label: qsTr("Rainstorm") },
                    { page: "spotlight.html", label: qsTr("Spotlight") },
                    { page: "warp.html", label: qsTr("Warp") }
                ]
                delegate: IconMenuItem {
                    required property var modelData
                    text: modelData.label
                    iconName: "preferences-desktop-screensaver"
                    enabled: dock && dock.available
                    onTriggered: if (dock && dock.available)
                                     dock.activateScreensaver(win.screenName, 1,
                                                              modelData.page)
                }
            }
        }

        MenuSeparator {}

        IconMenuItem {
            text: qsTr("Restablecer la disposición")
            iconName: "edit-undo"
            onTriggered: {
                if (win.confirm(qsTr("Restablecer la disposición"),
                                qsTr("Las tarjetas vuelven a acomodarse solas. ¿Seguir?")))
                    cmLayout.resetAll()
            }
        }
        MenuSeparator {}
        IconMenuItem {
            text: qsTr("Configurar Control Manager…")
            iconName: "configure"
            onTriggered: win.openSettings()
        }
    }

    // --- keyboard -----------------------------------------------------------
    Keys.onEscapePressed: if (!cmConfig.keepOpen) win.hidePanel()
}
