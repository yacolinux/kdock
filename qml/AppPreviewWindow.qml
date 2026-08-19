// The hover preview of an application icon: a micro window with the capture of
// the app's window, and three buttons to manage it.
//
// It is a real `Window`, not a QQC2 `ToolTip` or `Popup`. The flags are the
// whole design and none of them is decorative:
//
//   Qt.ToolTip                  With a transient parent (the dock, set for us
//                               because this is declared inside Dock.qml) Qt's
//                               xdg-shell integration turns this into an
//                               **xdg_popup without a grab**, which is what
//                               LayerSurface::attachPopup() hangs off the dock's
//                               layer surface — the same machinery the dock's
//                               menus already use. Three things follow, and all
//                               three are requirements: an xdg_popup is not a
//                               toplevel, so plasma-window-management never
//                               reports it (it cannot show up in the dock itself
//                               or in any task bar), it can be *positioned*
//                               relative to the dock, and — without a grab — the
//                               surface still receives pointer input for its own
//                               MouseAreas while leaving the icon's right click
//                               alone. A toplevel would fail the first two, and a
//                               layer-shell client's toplevels additionally open
//                               *below* its own panel (CLAUDE-TRAMPS.md).
//                               Qt.Popup would grab input and eat that right
//                               click.
//   Qt.FramelessWindowHint      No decoration, which is the point.
//
// It used to carry Qt.WindowTransparentForInput as well, against a very real
// loop: should the popup land under the cursor, the icon would lose its hover,
// the preview would hide, the cursor would be back on the icon… forever. The
// buttons need input, so the flag is gone and the loop is closed from the other
// end — Dock.qml keeps the preview up while the pointer is on it instead of
// hiding it (see previewLastActivity there), and the popup is always placed
// across the dock's edge, never under the icon that summoned it.
//
// Everything about *when* it shows lives in Dock.qml (showAppPreview): this file
// is only the surface. In particular it is never shown before its first capture
// has landed, so an unauthorized ScreenShot2 degrades to "nothing appears"
// instead of an empty frame.
//
// Nothing here may reference the `theme`/`config` context properties: the file
// stays instantiable on its own (tst_qmlload compiles it from the qrc, and a
// QQuickView probe renders it with no context at all), so every colour and the
// icon-cache revision come in as properties from Dock.qml.

import QtQuick

Window {
    id: previewWindow

    // The window this preview is of. `thumbId` is the url-safe spelling of the
    // uuid — **never build the image url from the raw uuid**, QUrl percent-encodes
    // KWin's braces and the provider id stops matching the cache key.
    property string thumbId: ""
    // Cache-busting counter; 0 means there has never been a capture.
    property int revision: 0
    // Bound by Dock.qml from the dock's theme. Literal defaults on purpose: the
    // file stays instantiable on its own (a QQuickView probe needs no context
    // properties to render it), and nothing here can shadow `theme`.
    property color frameColor: "#202020"
    property color borderColor: "#60ffffff"
    property color buttonColor: "#ffffff"

    // The window-management strip. `showButtons` is config.appPreviewButtons;
    // the three state flags come from DockModel::previewWindow() and are
    // refreshed whenever the model reports the window changed, so the icons
    // follow what the window actually did.
    property bool showButtons: true
    property bool minimized: false
    property bool maximized: false
    property bool maximizable: false
    // Trailing part of the image://icon urls: Dock.qml's widgetIconSuffix, i.e.
    // the cache revision plus the icon set picked for the panel background. The
    // three glyphs are monochrome line art and the buttons sit on a scrim of
    // frameColor, so without the icon-set override a dark set lands on a dark
    // panel and the buttons look empty (CLAUDE-TRAMPS.md).
    property string iconSuffix: "@0"

    // The button strip, left to right. `act` is what the click dispatches on.
    //
    // The two "restore" glyphs are deliberately different names. Breeze draws
    // window-restore as a plain diamond and window-restore-pip as a small
    // window inside a big one: reusing one for both would leave the toggled
    // strip showing the same shape twice, with no way to tell the un-minimize
    // button from the un-maximize one. (view-restore, the obvious third
    // candidate, is an unreadable blob at 14 px — checked against the render,
    // which is the only way to choose an icon name.)
    readonly property var buttonModel: {
        var m = [{ act: "min",
                   icon: previewWindow.minimized ? "window-restore" : "window-minimize" }]
        if (previewWindow.maximizable)
            m.push({ act: "max",
                     icon: previewWindow.maximized ? "window-restore-pip" : "window-maximize" })
        m.push({ act: "close", icon: "window-close" })
        return m
    }

    // Any pointer activity anywhere on this surface, buttons included. Dock.qml
    // uses it as a timestamp, **not** as a hover flag: KWin does not reliably
    // deliver the pointer-leave of a popup surface, so a `containsMouse` here
    // would get stuck true and the preview would never close again.
    signal activity()
    signal activateRequested()
    signal closeRequested()
    signal minimizeToggled(bool on)
    signal maximizeToggled(bool on)

    flags: Qt.ToolTip | Qt.FramelessWindowHint
    color: "transparent"
    visible: false

    Rectangle {
        anchors.fill: parent
        color: previewWindow.frameColor
        border.width: 1
        border.color: previewWindow.borderColor

        Image {
            anchors.fill: parent
            anchors.margins: 1
            source: previewWindow.revision > 0
                ? "image://thumb/" + previewWindow.thumbId + "@" + previewWindow.revision
                : ""
            // The capture already keeps the window's aspect ratio and the surface
            // is sized from that same ratio, so this crops at most a rounding
            // pixel — and never letterboxes.
            fillMode: Image.PreserveAspectCrop
            // The provider is mutex-guarded precisely so it can answer off the
            // GUI thread (see ThumbnailCache).
            asynchronous: true
            // The provider already holds exactly one copy; QtQuick's URL cache
            // would only pin the previous revisions.
            cache: false
        }

        // Below the buttons in the stacking order, so it never steals their
        // clicks. Clicking the capture raises the window, which is the one
        // gesture everybody tries first.
        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            onEntered: previewWindow.activity()
            onPositionChanged: previewWindow.activity()
            onClicked: previewWindow.activateRequested()
        }

        Row {
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.margins: 4
            spacing: 3
            visible: previewWindow.showButtons

            // One styled delegate instead of three near-identical Rectangles,
            // and a plain array instead of three `visible:` bindings — a window
            // the compositor says cannot be maximized gets no button rather than
            // a dead one, and no hole in the row either. It is a Repeater and
            // not an inline `component`: those get their own component scope and
            // could not see `previewWindow` from here.
            Repeater {
                model: previewWindow.buttonModel
                delegate: Rectangle {
                    id: button
                    required property var modelData

                    width: 20
                    height: 20
                    radius: 3
                    // Over a screenshot no flat fill reads well, so the button
                    // is a scrim of the frame colour that firms up under the
                    // pointer.
                    color: Qt.rgba(previewWindow.frameColor.r, previewWindow.frameColor.g,
                                   previewWindow.frameColor.b,
                                   buttonArea.containsMouse ? 0.95 : 0.6)
                    border.width: 1
                    border.color: Qt.rgba(previewWindow.buttonColor.r, previewWindow.buttonColor.g,
                                          previewWindow.buttonColor.b,
                                          buttonArea.containsMouse ? 0.5 : 0.2)

                    Image {
                        anchors.centerIn: parent
                        width: 14
                        height: 14
                        sourceSize.width: 14
                        sourceSize.height: 14
                        source: "image://icon/" + button.modelData.icon
                                + previewWindow.iconSuffix
                    }

                    MouseArea {
                        id: buttonArea
                        anchors.fill: parent
                        hoverEnabled: true
                        // Same reason as the body's MouseArea: aiming at a
                        // button means holding the pointer still, and without
                        // these the inactivity watchdog would close the preview
                        // mid-aim.
                        onEntered: previewWindow.activity()
                        onPositionChanged: previewWindow.activity()
                        onClicked: {
                            previewWindow.activity()
                            if (button.modelData.act === "min")
                                previewWindow.minimizeToggled(!previewWindow.minimized)
                            else if (button.modelData.act === "max")
                                previewWindow.maximizeToggled(!previewWindow.maximized)
                            else
                                previewWindow.closeRequested()
                        }
                    }
                }
            }
        }
    }
}
