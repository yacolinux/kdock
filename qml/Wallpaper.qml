// The contents of one wallpaper surface: the picture, and the crossfade to the
// next one.
//
// Deliberately self-contained — no context properties, no theme, no config.
// Everything comes in through the three properties below, which is what makes
// it possible to instantiate this file on its own in a probe (same idea as
// AppPreviewWindow.qml).

import QtQuick

Item {
    id: root

    // Set from C++ (WallpaperWindow::setImage).
    property url source
    // Plasma's org.kde.image FillMode numbering, which is what the Wallpapers
    // tab stores. It maps one-to-one onto Image.fillMode.
    property int fillMode: 2
    // Shows through wherever the picture does not reach (fit / centered modes).
    property color backgroundColor: "black"

    function _qtFillMode(mode) {
        switch (mode) {
        case 0: return Image.Stretch
        case 1: return Image.PreserveAspectFit
        case 2: return Image.PreserveAspectCrop
        case 3: return Image.Tile
        case 4: return Image.TileVertically
        case 5: return Image.TileHorizontally
        case 6: return Image.Pad
        }
        return Image.PreserveAspectCrop
    }

    Rectangle {
        anchors.fill: parent
        color: root.backgroundColor
    }

    // Two layers so a change can fade instead of blinking. `back` is the one
    // being loaded; when it is ready it fades in and the two swap roles, so the
    // old picture stays on screen for the whole load — which matters, because a
    // wallpaper is megabytes and decoding it is not instant.
    Image {
        id: front
        anchors.fill: parent
        fillMode: root._qtFillMode(root.fillMode)
        asynchronous: true
        cache: false
        smooth: true
    }

    Image {
        id: back
        anchors.fill: parent
        fillMode: root._qtFillMode(root.fillMode)
        asynchronous: true
        cache: false
        smooth: true
        opacity: 0

        onStatusChanged: {
            if (status === Image.Ready && source != "")
                fade.restart()
        }
    }

    NumberAnimation {
        id: fade
        target: back
        property: "opacity"
        from: 0
        to: 1
        duration: 400
        onFinished: {
            // Hand the picture over without a frame of transparency: assign it
            // to the front layer first, then clear the back one.
            front.source = back.source
            back.opacity = 0
            back.source = ""
        }
    }

    onSourceChanged: {
        if (front.source == "") {
            // Nothing on screen yet: no fade, or the very first wallpaper would
            // come up through a black frame.
            front.source = root.source
            return
        }
        if (root.source == front.source)
            return
        back.source = root.source
    }
}
