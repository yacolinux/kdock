// SystrayMenu.qml — the context menu of a system-tray item, drawn by the dock.
//
// The item does not hand us a menu widget, it hands us a tree over DBusMenu
// (see src/dbusmenu.cpp), so the menu is built from data instead of declared.
// Submenus make this component instantiate *itself*, which QML refuses to do
// declaratively ("SystrayMenu is instantiated recursively" — the check is
// static): they are created with Qt.createComponent at runtime, which is the
// way around it.

import QtQuick
import QtQuick.Controls.Basic

Menu {
    id: control

    // One level of the tree from SystrayModel::menuTree(): a list of maps with
    // { id, label, separator, enabled, submenu, checkType, checked, iconName,
    //   hasIconData, serial, children }.
    property var nodes: []
    // Row of the owning tray item and its bus name, needed to talk back to the
    // model (triggering an entry, asking a lazy submenu to fill itself) and to
    // build the icon URLs.
    property int itemRow: -1
    property string service: ""
    // Set on a submenu: its own DBusMenu id, and the flag the parent's teardown
    // uses to tell a submenu from a plain entry. 0 = this is the root menu.
    property int nodeId: 0
    property bool isSubmenu: false

    // popupType: Popup.Window or the menu is clipped to the dock's own surface
    // on Wayland.
    popupType: Popup.Window

    // Items are allowed to leave a submenu empty until told it is about to be
    // shown; the fresh layout arrives through menuInvalidated.
    onAboutToShow: if (control.nodeId !== 0) systray.menuAboutToShow(control.itemRow,
                                                                    control.nodeId)

    // Built imperatively rather than with an Instantiator: the three kinds of
    // entry are three different types (MenuSeparator / MenuItem / Menu), and a
    // single Instantiator delegate can only produce one of them.
    property var _created: []
    property var _submenuComp: null

    onNodesChanged: control.rebuild()
    Component.onCompleted: control.rebuild()

    function rebuild() {
        for (let i = control._created.length - 1; i >= 0; --i) {
            const obj = control._created[i]
            if (!obj)
                continue
            // A submenu is held by the parent as a Menu, not as an item.
            if (obj.isSubmenu === true)
                control.removeMenu(obj)
            else
                control.removeItem(obj)
            obj.destroy()
        }
        control._created = []

        const list = control.nodes || []
        for (let n = 0; n < list.length; ++n) {
            const node = list[n]
            let obj = null
            if (node.separator) {
                obj = separatorComp.createObject(control.contentItem)
                if (obj) control.addItem(obj)
            } else if (node.submenu) {
                obj = control.createSubmenu(node)
                if (obj) control.addMenu(obj)
            } else {
                obj = entryComp.createObject(control.contentItem, { node: node })
                if (obj) control.addItem(obj)
            }
            if (obj)
                control._created.push(obj)
        }
    }

    function createSubmenu(node) {
        if (!control._submenuComp)
            control._submenuComp = Qt.createComponent("SystrayMenu.qml")
        if (control._submenuComp.status !== Component.Ready) {
            console.warn("SystrayMenu: submenu component not ready:",
                         control._submenuComp.errorString())
            return null
        }
        return control._submenuComp.createObject(control, {
            title: node.label,
            enabled: node.enabled,
            nodes: node.children,
            itemRow: control.itemRow,
            service: control.service,
            nodeId: node.id,
            isSubmenu: true
        })
    }

    Component {
        id: separatorComp
        MenuSeparator {}
    }

    Component {
        id: entryComp
        IconMenuItem {
            required property var node
            text: node.label
            enabled: node.enabled
            // Both toggle kinds render as a check: QQC's radio indicator needs
            // an exclusive group, which DBusMenu does not describe.
            checkable: node.checkType !== ""
            checked: node.checked
            iconName: node.iconName
            // Qt/KDE items send *both* a theme name and an embedded blob; the
            // name is preferred because it follows the icon theme and scales,
            // while the blob is a fixed 16 px snapshot. The blob (served by the
            // systray provider, which decodes it) is the fallback for entries
            // with no name — its serial busts QML's pixmap cache when the item
            // rebuilds its menu.
            iconSource: node.iconName === "" && node.hasIconData
                        ? "image://systray/menu:" + control.service + ":" + node.id
                          + "@" + node.serial
                        : ""
            onTriggered: systray.triggerMenuItem(control.itemRow, node.id)
        }
    }
}
