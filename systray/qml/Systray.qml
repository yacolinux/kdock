// The tray strip: the dock's old inline systray block (Dock.qml systrayComp),
// lifted into its own resizable window. Each SNI item is an icon with its own
// mouse handling and its own menu, drawn over DBusMenu by SystrayMenu.qml.
//
// Context properties (set in SystrayWindow): systray (SystrayModel), theme,
// config (SystrayConfig), win (SystrayWindow). Menus are Popup.Window, i.e.
// their own xdg surface — which now has a real toplevel to parent to, unlike
// inside the dock.

import QtQuick
import QtQuick.Controls

Item {
    id: root
    // Filled by SizeRootObjectToView, so width/height are the window size.
    property int iconPx: config.iconSize
    property bool menuOpen: false

    readonly property int pad: 10
    readonly property int cols: config.columns > 0
        ? config.columns
        : Math.max(1, Math.floor((width - 2 * pad + config.iconSpacing)
                                 / (iconPx + config.iconSpacing)))

    Rectangle {
        anchors.fill: parent
        radius: config.cornerRadius
        color: Qt.rgba(theme.background.r, theme.background.g, theme.background.b,
                       config.backgroundOpacity)
        border.width: 1
        border.color: Qt.rgba(theme.foreground.r, theme.foreground.g, theme.foreground.b, 0.15)
    }

    Text {
        anchors.centerIn: parent
        visible: systray.count === 0
        text: qsTr("La bandeja está vacía")
        color: theme.foreground
        opacity: 0.55
        font.pixelSize: 13
    }

    Grid {
        id: grid
        anchors.centerIn: parent
        columns: root.cols
        spacing: config.iconSpacing
        Repeater {
            id: repeater
            model: systray
            delegate: Item {
                id: systrayItem
                width: root.iconPx
                height: root.iconPx

                ToolTip {
                    popupType: Popup.Window
                    visible: config.showTooltips && !root.menuOpen && systrayMouse.containsMouse
                    delay: 400
                    text: model.tooltip || model.service
                }

                Image {
                    anchors.centerIn: parent
                    width: Math.round(root.iconPx * 0.8)
                    height: width
                    // Themed icon when the item provides one; otherwise fall back
                    // to its raw IconPixmap via the systray provider.
                    source: model.iconName
                        ? "image://icon/" + model.iconName + win.iconSuffix
                        : "image://systray/" + model.service + "@" + model.iconSerial
                    sourceSize: Qt.size(root.iconPx * Screen.devicePixelRatio,
                                        root.iconPx * Screen.devicePixelRatio)
                    scale: systrayMouse.containsMouse ? 1.12 : 1.0
                    Behavior on scale { NumberAnimation { duration: 120 } }
                }

                MouseArea {
                    id: systrayMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
                    onClicked: (mouse) => {
                        const gx = mapToGlobal(mouse.x, mouse.y).x
                        const gy = mapToGlobal(mouse.x, mouse.y).y
                        if (mouse.button === Qt.LeftButton) {
                            // A menu-only item has nothing to activate: the spec
                            // says show the menu. Everything else gets Activate,
                            // and falls back to the menu if the item does not
                            // really implement it (the model turns that failure
                            // into menuReady).
                            if (model.itemIsMenu && model.hasMenu)
                                systrayItem.openMenu(gx, gy)
                            else
                                systray.activate(index, gx, gy)
                        } else if (mouse.button === Qt.RightButton) {
                            // Never ContextMenu when the item has a real menu:
                            // asking the item to draw it cannot work on Wayland
                            // (no surface to parent a popup to).
                            if (model.hasMenu)
                                systrayItem.openMenu(gx, gy)
                            else
                                systray.contextMenu(index, gx, gy)
                        } else if (mouse.button === Qt.MiddleButton) {
                            systray.secondaryActivate(index, gx, gy)
                        }
                    }
                }

                // ---- The item's own menu, drawn by us ----------------------
                // Fetching it is asynchronous, so a click only *asks*; the menu
                // opens when the layout arrives.
                property bool menuWanted: false
                property int pendingX: 0
                property int pendingY: 0
                function openMenu(gx, gy) {
                    systrayItem.menuWanted = true
                    systrayItem.pendingX = gx
                    systrayItem.pendingY = gy
                    systray.requestMenu(index)
                }

                SystrayMenu {
                    id: itemMenu
                    itemRow: index
                    service: model.service
                    onAboutToShow: { root.menuOpen = true; win.setMenuOpen(true) }
                    onOpened: systray.setMenuOpen(index, true)
                    onClosed: {
                        root.menuOpen = false
                        systray.setMenuOpen(index, false)
                        win.setMenuOpen(false)
                    }
                }

                Connections {
                    target: systray
                    function onMenuReady(row) {
                        if (row !== index || !systrayItem.menuWanted)
                            return
                        systrayItem.menuWanted = false
                        itemMenu.nodes = systray.menuTree(index)
                        itemMenu.popup(systrayItem.menuOriginX(), systrayItem.menuOriginY())
                    }
                    function onMenuFailed(row) {
                        if (row !== index || !systrayItem.menuWanted)
                            return
                        systrayItem.menuWanted = false
                        // No menu to draw: let the item try its own way.
                        systray.contextMenu(index, systrayItem.pendingX, systrayItem.pendingY)
                    }
                    function onMenuInvalidated(row) {
                        if (row === index && itemMenu.visible)
                            itemMenu.nodes = systray.menuTree(index)
                    }
                }

                // Opens away from the anchored edge, like every dock popup.
                function menuOriginX() {
                    if (config.edge === 2) return systrayItem.width
                    if (config.edge === 3) return -itemMenu.width
                    return 0
                }
                function menuOriginY() {
                    if (config.edge === 0) return -itemMenu.height
                    if (config.edge === 1) return systrayItem.height
                    return 0
                }
            }
        }
    }

    // Esc closes the window (the layer surface has keyboard focus while shown).
    focus: true
    Keys.onEscapePressed: win.hideWindow()
}
