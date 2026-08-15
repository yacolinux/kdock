// SubMenuDelegate.qml - the row that opens a submenu, with an icon.
//
// Qt builds that row from the *parent* menu's `delegate`, not from anything we
// declare, so the stock QtQuick.Controls.Basic MenuItem is what draws it: no
// icon, while every leaf of our menus is an IconMenuItem. Assigning this as the
// parent menu's delegate closes that gap. The icon name travels on the submenu
// itself (`property string menuIcon`), which the created row reaches through
// its own `subMenu` property.

import QtQuick
import QtQuick.Controls

IconMenuItem {
    id: control

    // Why iconSource and not iconName: an item built from a Menu's delegate
    // gets a context in which IconMenuItem.qml cannot resolve the `theme`
    // context property, so building the URL over there (iconName does) throws
    // "Cannot read property 'revision' of undefined" once per row into stderr.
    // From this file `theme` resolves fine, so the URL is assembled here and
    // handed over ready-made — iconSource takes precedence and leaves
    // IconMenuItem's own expression short-circuited on the empty iconName.
    //
    // The theme guard covers the first evaluation, which Qt runs while the row
    // is still detached and every context property reads back undefined; once
    // _ready flips the binding re-runs with theme live, so a theme change still
    // busts the icon cache.
    property bool _ready: false
    Component.onCompleted: control._ready = true

    readonly property string _menuIcon: control._ready && control.subMenu
        && control.subMenu.menuIcon !== undefined ? control.subMenu.menuIcon : ""
    iconSource: _menuIcon !== ""
                ? "image://icon/" + _menuIcon + "@" + (theme ? theme.revision : 0) : ""

    // A submenu that only applies to some of the rows it could hang off cannot
    // hide itself: on a Menu, `visible` is the popup's, and this row is built
    // from the parent menu's delegate. So it travels as an opt-in property on
    // the submenu (default: shown, which is every other submenu of the dock).
    readonly property bool _rowVisible: !control._ready || !control.subMenu
        || control.subMenu.rowVisible === undefined ? true : control.subMenu.rowVisible
    visible: _rowVisible
    height: visible ? implicitHeight : 0

    // MenuItem draws its submenu arrow on the right, over the content item.
    // IconMenuItem's Row does not know about it (the stock IconLabel does), so
    // without this the widest label runs underneath the arrow.
    rightPadding: control.arrow ? control.arrow.width + control.spacing : control.padding
}
