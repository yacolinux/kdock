// The hover preview of an application icon: a micro window that shows nothing
// but the capture of the app's window.
//
// It is a real `Window`, not a QQC2 `ToolTip` or `Popup`. The three flags are
// the whole design and none of them is decorative:
//
//   Qt.ToolTip                  With a transient parent (the dock, set for us
//                               because this is declared inside Dock.qml) Qt's
//                               xdg-shell integration turns this into an
//                               **xdg_popup without a grab**, which is what
//                               LayerSurface::attachPopup() hangs off the dock's
//                               layer surface — the same machinery the dock's
//                               menus already use. Two things follow, and both
//                               are requirements: an xdg_popup is not a toplevel,
//                               so plasma-window-management never reports it (it
//                               cannot show up in the dock itself or in any task
//                               bar), and it can be *positioned* relative to the
//                               dock. A toplevel would fail both, and a
//                               layer-shell client's toplevels additionally open
//                               *below* its own panel (CLAUDE-TRAMPS.md).
//                               Qt.Popup would grab input and eat the dock's
//                               right click.
//   Qt.WindowTransparentForInput The surface takes no pointer input. Should the
//                               popup ever land under the cursor, the icon would
//                               lose its hover, the preview would hide, the
//                               cursor would be back on the icon… forever. With
//                               an empty input region that cannot happen.
//   Qt.FramelessWindowHint      No decoration, which is the point.
//
// Everything about *when* it shows lives in Dock.qml (showAppPreview): this file
// is only the surface. In particular it is never shown before its first capture
// has landed, so an unauthorized ScreenShot2 degrades to "nothing appears"
// instead of an empty frame.

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

    flags: Qt.ToolTip | Qt.FramelessWindowHint | Qt.WindowTransparentForInput
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
    }
}
