// One window: its last capture, the app's icon as a badge, and the title.
//
// The roles come in as required properties, which QML fills from the model by
// name. `config`, `theme` and `previews` are engine context properties (set in
// previewwindow.cpp) — deliberately *not* redeclared here, or they would shadow
// themselves (the trap AGENTS.md documents for RelanzadorWidget).

import QtQuick
import Qt5Compat.GraphicalEffects

Item {
    id: card

    // ---- Injected by the ListView delegate ---------------------------------
    required property int index
    required property string uuid
    // The url-safe form of uuid: QUrl percent-encodes the braces of KWin's uuid,
    // which made the provider id stop matching the cache key and left every card
    // blank (bug 2026-07-30). Never build the image:// url from `uuid`.
    required property string thumbId
    required property string title
    required property string appName
    required property string iconName
    required property int thumbRevision
    required property real aspect
    required property bool active
    required property bool minimized

    // ---- Set by the strip --------------------------------------------------
    // Size along the strip's cross axis: the card's width on a vertical strip,
    // its height on a horizontal one.
    property int crossSize: 240
    property bool horizontal: false
    property bool showTitle: true

    readonly property bool hasThumb: thumbRevision > 0
    readonly property int titleHeight: showTitle ? 16 : 0
    readonly property int badgeSize: Math.max(16, Math.round(crossSize * 0.16))

    // The thumbnail keeps the window's aspect ratio; the card is the thumbnail
    // plus the title line.
    readonly property int thumbWidth: horizontal
        ? Math.max(32, Math.round((crossSize - titleHeight) * aspect))
        : crossSize
    readonly property int thumbHeight: horizontal
        ? Math.max(24, crossSize - titleHeight)
        : Math.max(24, Math.round(crossSize / Math.max(0.2, aspect)))

    width: thumbWidth
    height: thumbHeight + titleHeight

    scale: hover.hovered ? 1.03 : 1.0
    Behavior on scale {
        NumberAnimation { duration: 120; easing.type: Easing.OutQuad }
    }

    HoverHandler {
        id: hover
        // A card the user is pointing at is the one whose staleness shows most.
        onHoveredChanged: if (hovered) previews.refreshNow(card.index)
    }

    Rectangle {
        id: frame
        width: card.thumbWidth
        height: card.thumbHeight
        radius: 8
        color: Qt.rgba(theme.foreground.r, theme.foreground.g, theme.foreground.b, 0.10)
        border.width: card.active ? 2 : 1
        border.color: card.active
            ? theme.highlight
            : Qt.rgba(theme.foreground.r, theme.foreground.g, theme.foreground.b, 0.25)
        // Minimized windows stay in the strip but read as put away.
        opacity: card.minimized ? 0.55 : 1.0

        Image {
            id: thumb
            anchors.fill: parent
            anchors.margins: frame.border.width
            visible: card.hasThumb
            // The revision suffix is what busts QtQuick's URL-keyed cache when a
            // new capture lands (see ThumbnailImageProvider).
            source: card.hasThumb
                ? "image://thumb/" + card.thumbId + "@" + card.thumbRevision
                : ""
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            cache: false // the provider already holds exactly one copy

            layer.enabled: true
            layer.effect: OpacityMask {
                maskSource: Rectangle {
                    width: thumb.width
                    height: thumb.height
                    radius: frame.radius - 1
                }
            }
        }

        // No capture yet (or captures are not authorized): the app icon alone
        // still tells the user which window this is.
        Image {
            anchors.centerIn: parent
            visible: !card.hasThumb
            width: Math.min(64, frame.width * 0.5)
            height: width
            sourceSize.width: width
            sourceSize.height: width
            source: "image://icon/" + card.iconName + "@" + theme.revision
            asynchronous: true
        }

        // App badge, bottom-right, over the capture.
        Rectangle {
            visible: card.hasThumb
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 4
            width: card.badgeSize + 6
            height: width
            radius: width / 2
            color: Qt.rgba(theme.background.r, theme.background.g, theme.background.b, 0.75)

            Image {
                anchors.centerIn: parent
                width: card.badgeSize
                height: card.badgeSize
                sourceSize.width: card.badgeSize
                sourceSize.height: card.badgeSize
                source: "image://icon/" + card.iconName + "@" + theme.revision
                asynchronous: true
            }
        }
    }

    Text {
        visible: card.showTitle
        anchors.top: frame.bottom
        anchors.left: frame.left
        anchors.right: frame.right
        height: card.titleHeight
        verticalAlignment: Text.AlignVCenter
        horizontalAlignment: Text.AlignHCenter
        elide: Text.ElideRight
        // The window title is the useful half; the app name is the fallback for
        // windows that have not set one yet.
        text: card.title.length > 0 ? card.title : card.appName
        color: theme.foreground
        font.pixelSize: 11
    }

    TapHandler {
        acceptedButtons: Qt.LeftButton
        gesturePolicy: TapHandler.ReleaseWithinBounds
        onTapped: previews.activate(card.index)
    }
    TapHandler {
        acceptedButtons: Qt.MiddleButton
        gesturePolicy: TapHandler.ReleaseWithinBounds
        onTapped: previews.closeWindow(card.index)
    }
}
