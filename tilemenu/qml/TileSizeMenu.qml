// TileSizeMenu.qml - the tile size submenu, in cells.
//
// Written out rather than generated from a Repeater: six entries are shorter
// this way than the model that would build them, and the checkmark logic stays
// obvious.

import QtQuick
import QtQuick.Controls

Menu {
    id: sizeMenu
    title: qsTr("Tamaño")
    popupType: Popup.Window

    // Current size of the tile the menu was opened on.
    property int span: 1
    property int vspan: 1
    signal picked(int w, int h)

    MenuItem {
        text: qsTr("1 × 1")
        checkable: true
        checked: sizeMenu.span === 1 && sizeMenu.vspan === 1
        onTriggered: sizeMenu.picked(1, 1)
    }
    MenuItem {
        text: qsTr("2 × 1 (ancho)")
        checkable: true
        checked: sizeMenu.span === 2 && sizeMenu.vspan === 1
        onTriggered: sizeMenu.picked(2, 1)
    }
    MenuItem {
        text: qsTr("1 × 2 (alto)")
        checkable: true
        checked: sizeMenu.span === 1 && sizeMenu.vspan === 2
        onTriggered: sizeMenu.picked(1, 2)
    }
    MenuItem {
        text: qsTr("2 × 2")
        checkable: true
        checked: sizeMenu.span === 2 && sizeMenu.vspan === 2
        onTriggered: sizeMenu.picked(2, 2)
    }
    MenuItem {
        text: qsTr("4 × 2")
        checkable: true
        checked: sizeMenu.span === 4 && sizeMenu.vspan === 2
        onTriggered: sizeMenu.picked(4, 2)
    }
    MenuItem {
        text: qsTr("4 × 4")
        checkable: true
        checked: sizeMenu.span === 4 && sizeMenu.vspan === 4
        onTriggered: sizeMenu.picked(4, 4)
    }
}
