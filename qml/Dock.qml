import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Qt5Compat.GraphicalEffects

Item {
    id: root

    readonly property bool horizontal: config.edge === 0 || config.edge === 1
    readonly property int pad: config.compact ? 2 : 10
    // Computed in C++ (DockConfig::dockThickness) so the layer-shell exclusive
    // zone in DockWindow::thickness() can never drift from what is drawn here.
    readonly property int thickness: config.dockThickness

    // App-icon labels. 0 = icon only (default, identical to the pre-label
    // appearance), 1 = name under the icon, 2 = name at its right, 3 = name
    // only. Every metric comes from config so QML and C++ agree on the cell
    // geometry; the icon itself always keeps its size (root.appIconPx).
    // fitLabelsDropped is the auto-shrink's last resort (see below): the names
    // go away, config is left alone, and the mode comes back on its own.
    readonly property int labelMode: fitLabelsDropped ? 0 : config.iconLabelMode
    // Also false with the apps block off: no app name is drawn, so none of them
    // must reach measureLabels() and reserve room in the dock's thickness.
    readonly property bool labelVisible: labelMode !== 0 && config.showAppIcons
    readonly property bool labelShowsIcon: labelMode !== 3
    readonly property int labelGap: config.iconLabelGap
    // Label metrics follow the auto-shrink factor: leaving the name box at its
    // full width would cancel most of the room won by shrinking the icons.
    readonly property int labelFontPx: Math.max(7, Math.round(config.iconLabelFontPx * fitScale))
    readonly property int labelLineHeight: Math.round(labelFontPx * 1.4)
    // A name may be drawn on one line or wrapped onto two (config.labelLines):
    // the box is that many line heights tall, and it is the box — not the line —
    // that every layout below reserves room for. DockConfig::iconLabelBoxHeight()
    // is the same formula on the C++ side, which is what feeds the thickness.
    readonly property int labelLines: config.labelLines
    readonly property int labelBoxHeight: labelLineHeight * labelLines
    // See labelW in the delegates: only wrapping needs the pixel.
    readonly property int labelSlack: labelLines > 1 ? 1 : 0
    // The name box. On a horizontal dock this is the *cap* each cell shrinks
    // within (see labelW in the delegates). On a vertical dock every cell shares
    // one box, so using the cap there left a band of dead dock to the right of
    // the longest name (bug 2026-07-30, error-texto-horiz.jpeg): the box follows
    // the widest name actually drawn instead, measured below and capped in C++.
    readonly property int labelBoxWidth:
        Math.round((horizontal ? config.iconLabelWidth : config.effectiveLabelWidth) * fitScale)

    // ---- Widest drawn name ------------------------------------------------
    // Measured off-screen, at the *unscaled* font (labelBoxWidth applies
    // fitScale), and reported to C++ so DockConfig::dockThickness() — and with it
    // the layer-shell exclusive zone — can never disagree with what is drawn.
    //
    // The app names come from the model (dockModel.labelStrings()), not from the
    // apps Repeater: that Repeater's id is declared inside the section delegate,
    // i.e. another component scope, and is **not reachable from here** — reading
    // it threw "dockRepeater is not defined" on every measurement and silently
    // disabled the whole thing (bug 2026-07-30). Section names do come from
    // sectionRepeater, whose id *is* in this component. App names are measured
    // bold (the active app's weight, so moving the focus never reflows the dock)
    // and section names regular — the same fonts the delegates draw with.
    TextMetrics {
        id: appNameProbe
        font.pixelSize: config.iconLabelFontPx
        font.bold: true
    }
    TextMetrics {
        id: secNameProbe
        font.pixelSize: config.iconLabelFontPx
        // Bold section names are wider than regular ones, so the probe has to
        // follow the setting or the reserved box comes out short and every
        // widget name gets elided.
        font.bold: config.labelBold
    }
    Timer {
        id: labelMeasureTimer
        interval: 16
        onTriggered: root.measureLabels()
    }
    function scheduleLabelMeasure() {
        labelMeasureTimer.restart()
    }
    function measureLabels() {
        let max = 0
        if (root.labelVisible) {
            const names = dockModel.labelStrings()
            for (let i = 0; i < names.length; ++i) {
                appNameProbe.text = names[i]
                max = Math.max(max, Math.ceil(appNameProbe.advanceWidth))
            }
        }
        if (root.widgetLabelVisible) {
            for (let j = 0; j < sectionRepeater.count; ++j) {
                const sec = sectionRepeater.itemAt(j)
                if (!sec || !sec.visible || !sec.labelled)
                    continue
                secNameProbe.text = sec.label
                max = Math.max(max, Math.ceil(secNameProbe.advanceWidth))
            }
        }
        // 0 is a real answer, not "unknown": no name is being drawn (labels off,
        // or dropped by the auto-shrink), so the dock should shrink to its icons
        // rather than keep reserving room for names nobody can see.
        config.setMeasuredLabelWidth(max)
    }
    onLabelVisibleChanged: scheduleLabelMeasure()
    onWidgetLabelVisibleChanged: scheduleLabelMeasure()
    Component.onCompleted: { scheduleLabelMeasure(); scheduleGapRuns() }
    Connections {
        target: config
        // The only two inputs of config.iconLabelFontPx. Deliberately *not*
        // hooked to dockThicknessChanged: setMeasuredLabelWidth emits that, which
        // would feed straight back in here.
        function onIconSizeChanged() { root.scheduleLabelMeasure() }
        function onIconLabelFontSizeChanged() { root.scheduleLabelMeasure() }
        // Not an input of iconLabelFontPx, but it does change how wide the
        // section names come out (secNameProbe above).
        function onLabelBoldChanged() { root.scheduleLabelMeasure() }
        // A rename — or a language change, which renames every section at once
        // (DockConfig::retranslate) — changes the text secNameProbe measures.
        function onWidgetNamesChanged() { root.scheduleLabelMeasure() }
    }
    Connections {
        target: dockModel
        // Rows appearing/going away, and renames in place (a window's app is
        // resolved late, a launcher is unpinned into a window row…).
        function onRowsInserted() { root.scheduleLabelMeasure() }
        function onRowsRemoved() { root.scheduleLabelMeasure() }
        function onModelReset() { root.scheduleLabelMeasure() }
        function onDataChanged() { root.scheduleLabelMeasure() }
    }

    // Same for every *other* section (widgets and blocks), configured apart
    // from the apps: same positions minus "name only" (a widget without its
    // icon would be unrecognizable). Width and font come from the settings
    // above, shared with the app labels.
    readonly property int widgetLabelMode: fitLabelsDropped ? 0 : config.widgetLabelMode
    readonly property bool widgetLabelVisible: widgetLabelMode !== 0
    // Reads widgetNamesRevision so a rename re-evaluates the binding, the same
    // way theme.revision invalidates the icon URLs.
    function widgetNameOf(token) {
        return config.widgetNamesRevision, config.widgetName(token)
    }

    // ---- Transparent separators (token "gap") -----------------------------
    // A gap expands like a spring and, on top of that, the panel background is
    // not painted over it: the dock is drawn as one rectangle per *run* between
    // gaps, so the desktop shows through in between and the dock reads as two.
    // The same rectangles go to C++ (dockWindow.setGapRects) to be cut out of
    // the surface's input region, or the hole would be see-through but still
    // eat the clicks.
    //
    // The geometry is read from sectionRepeater's items, never from ids inside
    // the delegate: those live in another component scope and are not reachable
    // from here (same reason labelStrings() exists — see the measurement block
    // above).
    property var gapRuns: [{ pos: 0, size: 0 }]
    Timer {
        id: gapTimer
        interval: 16
        onTriggered: root.computeGapRuns()
    }
    function scheduleGapRuns() { gapTimer.restart() }
    function computeGapRuns() {
        const total = root.horizontal ? slider.width : slider.height
        let holes = []
        for (let i = 0; i < sectionRepeater.count; ++i) {
            const sec = sectionRepeater.itemAt(i)
            if (!sec || !sec.isGap || !sec.visible)
                continue
            const p = sec.mapToItem(slider, 0, 0)
            const pos = root.horizontal ? p.x : p.y
            const size = root.horizontal ? sec.width : sec.height
            if (size > 0)
                holes.push({ pos: Math.round(pos), size: Math.round(size) })
        }
        holes.sort((a, b) => a.pos - b.pos)
        // Complement of the holes inside [0, total): what actually gets painted.
        let runs = []
        let at = 0
        for (let h = 0; h < holes.length; ++h) {
            const start = Math.max(at, holes[h].pos)
            if (start > at)
                runs.push({ pos: at, size: start - at })
            at = Math.max(at, holes[h].pos + holes[h].size)
        }
        if (at < total)
            runs.push({ pos: at, size: total - at })
        root.gapRuns = runs.length > 0 ? runs : [{ pos: 0, size: total }]
        // The input region follows the same rectangles, in surface coordinates.
        let rects = []
        for (let g = 0; g < holes.length; ++g) {
            rects.push(root.horizontal
                       ? { x: holes[g].pos, y: 0, width: holes[g].size, height: root.height }
                       : { x: 0, y: holes[g].pos, width: root.width, height: holes[g].size })
        }
        dockWindow.setGapRects(rects)
    }
    onWidthChanged: scheduleGapRuns()
    onHeightChanged: scheduleGapRuns()
    Connections {
        target: config
        // Anything that moves a section along the dock moves the holes with it.
        function onWidgetOrderChanged() { root.scheduleGapRuns() }
        function onPanelModeChanged() { root.scheduleGapRuns() }
        function onAlignmentChanged() { root.scheduleGapRuns() }
        function onDockLengthChanged() { root.scheduleGapRuns() }
        function onEdgeChanged() { root.scheduleGapRuns() }
        function onSpacingChanged() { root.scheduleGapRuns() }
        function onDockThicknessChanged() { root.scheduleGapRuns() }
    }

    // ---- Auto-shrink ------------------------------------------------------
    // The dock has a fixed length (the screen edge in panel mode, dockLength%
    // otherwise), but its content does not: enough apps, widgets or labels and
    // the sections stop fitting. QtQuick Layouts does not clip in that case, it
    // piles the leftover sections on top of each other (bug 2026-07-29,
    // bug-horizontal.jpg / bug-vertical.jpg). So every icon is scaled down by
    // `fitScale` until the content fits, down to config.autoShrinkMinIconSize.
    //
    // The factor is computed iteratively instead of with a binding: the needed
    // length depends on the scale, so a binding would be a loop. Each pass
    // corrects the scale by avail/needed and schedules another one; the needed
    // length is roughly affine in the scale, so it converges in a few passes.
    // The dock *thickness* deliberately stays untouched (it comes from C++ and
    // drives the layer-shell exclusive zone): only the length is at stake.
    //
    // Two things make the search harder than a plain proportional correction,
    // and both bit once (2026-07-29, vibra.jpg): the needed length is a
    // *staircase* in the scale, not a smooth curve (the label font is an
    // integer, so a dock in "name only" mode with a 9 px font has three states
    // — 9, 8, 7 — and dropping one of them frees ~10% of the length at once),
    // and below the scale floor there is nothing left to give. Hence
    // fitOverflowed (once something overflowed, this episode only shrinks) and
    // fitLabelsDropped (icons only as the last resort).
    property real fitScale: 1.0
    property int fitPasses: 0
    // Set when refit() moves the scale, and cleared only once refit() converges:
    // one pass emits *several* fitNeeded changes (the layout and the label
    // metrics settle in stages), and if any of them is mistaken for an external
    // one it resets fitPasses and the runaway guard never trips.
    property bool fitSelfScaled: false
    // Whether anything overflowed since the last external change. Once it did,
    // this episode may only shrink; that one rule is what stopped the dock from
    // ping-ponging forever at the exact app count where one staircase step
    // stops fitting.
    property bool fitOverflowed: false
    // Last resort, one step below the scale floor: with the content still over
    // the edge there is no room left but the names, and dropping them beats
    // eliding every one of them into an unreadable stub. Applies to the apps
    // *and* the widgets, and never touches config (the user's label mode stays
    // as chosen, and is restored as soon as the content fits again).
    property bool fitLabelsDropped: false
    // Room to spare of the icon-only layout the drop settled at. The labels get
    // another try once there is meaningfully more of it — which covers both
    // ways that can happen, content going away and the dock getting longer.
    // Retrying on every change instead would flash the names on and off once
    // per change while over capacity. -1 = not measured yet.
    property int fitDroppedSlack: -1
    readonly property real fitMinScale:
        Math.min(1, config.autoShrinkMinIconSize / Math.max(1, config.iconSize))
    // Length the sections have to fit in. Never derived from the content, or
    // the floating dock (which sizes itself to its content) would feed its own
    // measurement back into the scale.
    readonly property int fitAvailable: {
        const screenMain = horizontal ? Screen.width : Screen.height
        const own = horizontal ? width : height
        const limit = (config.panelMode || config.dockLength > 0) ? own : screenMain
        return Math.max(0, limit - 2 * pad)
    }
    readonly property int fitNeeded: horizontal ? sectionLayout.implicitWidth
                                                : sectionLayout.implicitHeight

    readonly property int appIconPx: Math.max(8, Math.round(config.iconSize * fitScale))
    readonly property int widgetIconPx: Math.max(8, Math.round(config.widgetIconSize * fitScale))
    readonly property int systrayIconPx: Math.max(8, Math.round(config.systrayIconSize * fitScale))
    // Gaps and the clock font shrink too: they are a large part of what a dock
    // full of sections is made of, and leaving them fixed keeps the content
    // over the edge no matter how small the icons get.
    readonly property int spacingPx: Math.max(1, Math.round(config.spacing * fitScale))
    readonly property int clockFontPx: config.clockFontSize > 0
        ? Math.max(7, Math.round(config.clockFontSize * fitScale))
        : Math.max(7, Math.round(widgetIconPx * 0.35))

    Timer {
        id: fitTimer
        interval: 16
        onTriggered: root.refit()
    }
    // Any change of what is drawn or of the room available restarts the search
    // from scratch (pass counter and overflow ceiling included).
    onFitAvailableChanged: root.scheduleRefit()
    onFitNeededChanged: {
        // A change of what has to fit that is not our own rescaling (an app
        // closed, a widget added) restarts the search from scratch: the pass
        // counter left over from the shrink sequence would otherwise block
        // growing back, and the overflow ceiling found for the old content no
        // longer applies. Our own passes keep both, which is what makes the
        // search terminate.
        if (fitSelfScaled) fitTimer.restart()
        else root.scheduleRefit()
    }
    Connections {
        target: config
        function onAutoShrinkIconsChanged() { root.resetFit() }
        function onAutoShrinkMinIconSizeChanged() { root.resetFit() }
        function onIconSizeChanged() { root.resetFit() }
        // A dock that dropped its labels must not keep ignoring the mode the
        // user just picked in the label menus or the settings dialog.
        function onIconLabelModeChanged() { root.resetFit() }
        function onWidgetLabelModeChanged() { root.resetFit() }
        function onIconLabelWidthChanged() { root.resetFit() }
        function onIconLabelFontSizeChanged() { root.resetFit() }
    }

    // New search episode: forget what the previous content taught us.
    function scheduleRefit() {
        fitPasses = 0
        fitOverflowed = false
        fitTimer.restart()
    }

    // Same, plus back to the configured labels: used when the label settings
    // themselves change, where the degraded state is stale by definition.
    function resetFit() {
        fitLabelsDropped = false
        fitDroppedSlack = -1
        scheduleRefit()
    }

    function refit() {
        if (!config.autoShrinkIcons) {
            fitScale = 1.0
            fitLabelsDropped = false
            fitDroppedSlack = -1
            return
        }
        const avail = fitAvailable
        const need = fitNeeded
        if (avail <= 0 || need <= 0 || fitPasses > 12)
            return

        if (fitLabelsDropped) {
            // Meaningfully more room to spare than the drop settled with (an
            // app closed, the screen or the dock got wider): give the names
            // another try and let the scale search run with them. If they still
            // do not fit, the rung below drops them again and re-measures the
            // slack against the new content, so this cannot loop.
            if (fitDroppedSlack >= 0
                    && avail - need > fitDroppedSlack + Math.max(8, avail * 0.02)) {
                fitLabelsDropped = false
                fitDroppedSlack = -1
                fitPasses = 0
                fitOverflowed = false
                fitSelfScaled = true
                fitTimer.restart()
                return
            }
        } else if (need > avail
                   && (fitScale <= fitMinScale + 0.005 || fitPasses >= 6)
                   && (config.iconLabelMode !== 0 || config.widgetLabelMode !== 0)) {
            // Out of room: either the scale is on its floor, or six passes of
            // shrinking were not enough. The second case is the common one with
            // a dock full of names: once the label font is clamped at 7 px the
            // only thing still shrinking is the elision of each name, so the
            // scale converges asymptotically towards a length that never fits
            // (measured: 55 apps, eight passes, still 16 px over). Names elided
            // to stubs are worthless anyway, which is why this is the point to
            // trade them for the icons. Six is generous: a shrink that does
            // converge takes one to three passes.
            //
            // The scale is deliberately left where it is — jumping back to 1.0
            // would flash full-size icons for a frame — and the new episode's
            // grow branch climbs back up with the room the names just freed.
            fitLabelsDropped = true
            fitDroppedSlack = -1
            fitPasses = 0
            fitOverflowed = false
            fitSelfScaled = true
            fitTimer.restart()
            return
        }

        var s = fitScale
        if (need > avail) {
            // Nothing in this episode may grow back after this.
            fitOverflowed = true
            // 0.995: land just inside the edge. Aiming exactly at it leaves a
            // few pixels over, because the needed length is affine (spacings
            // and text do not scale) rather than proportional to the factor.
            s = Math.max(fitMinScale, fitScale * avail / need * 0.995)
        } else if (fitScale < 1 && need * 1.01 < avail && !fitOverflowed) {
            // Room to spare: grow back, but stay short of the edge so the next
            // pass does not have to shrink again (that is what oscillates). The
            // 1% gate is wider than the 0.995 the shrink lands with; asking for
            // more free room than that (it used to be 6%) means closing one app
            // never grew the icons back at all.
            //
            // !fitOverflowed is what makes the search *monotone*, and it is
            // the rule that actually stops the flicker. Merely capping the
            // growth just below the scale that overflowed is not enough: a
            // whole staircase step fits inside that margin, so the dock crosses
            // it back and forth, narrowing the bracket by a hundredth at a time
            // (measured: 11 passes at 38 apps, all of them visible). The slack
            // this leaves instead is at most one step, and the next external
            // change (an app closed) opens an episode where growing is on again.
            s = Math.min(1, fitScale * avail / need * 0.995)
        }
        if (Math.abs(s - fitScale) > 0.005) {
            fitPasses += 1
            fitSelfScaled = true
            fitScale = s
            fitTimer.restart()
        } else {
            // Settled. From here on any change of the needed length comes from
            // the content, not from us.
            fitSelfScaled = false
            if (fitLabelsDropped && fitDroppedSlack < 0)
                fitDroppedSlack = avail - need
        }
    }

    // Effective dock background, and a text color with enough contrast over it.
    // Shared by both clock widgets and the app-icon labels, which otherwise
    // inherit the KDE theme's gray foreground and wash out against a custom
    // panel color. Perceptual luminance (not a flat average) so dark
    // blues/greens count as dark. Opacity deliberately plays no part: a
    // translucent dark panel still reads as dark, so the text stays light.
    //
    // Dark mode overrides both of them *here*, at read time: config.panelColor
    // is never written, so leaving dark mode restores the user's own scheme
    // with nothing to undo. config.opacity is applied downstream as always, so
    // a translucent dock stays translucent in dark mode too.
    readonly property color dockBaseColor: config.darkModeActive ? config.darkBackground
                                           : config.panelColorSet ? config.panelColor
                                                                  : theme.background
    readonly property bool dockBaseIsLight:
        (0.299 * dockBaseColor.r + 0.587 * dockBaseColor.g + 0.114 * dockBaseColor.b) > 0.5
    readonly property color dockTextColor: config.darkModeActive ? config.darkAccent
                                           : dockBaseIsLight ? "#141414" : "#F2F2F2"

    // Same idea for the widgets' icons: the standard icons (volume, network,
    // session…) are monochrome and drawn in a color meant for one background,
    // so a dark icon set over a light panel is unreadable. Resolve them against
    // an icon set built for the dock background instead of the global one
    // (Breeze / Breeze Dark by default). Empty = no override, use the theme's.
    readonly property string widgetIconTheme: {
        // Dark mode forces the dark-background set whatever the mode says: the
        // panel is dark by definition there, and the light set would be a black
        // icon on a black panel.
        if (config.darkModeActive)
            return config.widgetIconThemeDarkBg
        switch (config.widgetIconThemeMode) {
        case 1: return dockBaseIsLight ? config.widgetIconThemeLightBg   // match dock color
                                       : config.widgetIconThemeDarkBg
        case 2: return config.widgetIconThemeLightBg                     // always dark icons
        case 3: return config.widgetIconThemeDarkBg                      // always light icons
        }
        return ""
    }
    // Trailing part of the image://icon URL of every widget icon. The URL has
    // to change whenever the resolved icon changes, or QML serves the cached
    // pixmap: theme.revision covers theme edits, widgetIconTheme the override.
    readonly property string widgetIconSuffix:
        "@" + theme.revision + (widgetIconTheme !== "" ? "@" + widgetIconTheme : "")

    // Dynamic separators only distribute space when the dock spans the whole
    // edge (panel mode). Outside panel mode they are no-ops (size 0).
    readonly property bool hasSpring: config.widgetOrder.indexOf("spring") >= 0
                                      || config.widgetOrder.indexOf("gap") >= 0
    readonly property bool fillMain: config.panelMode && hasSpring

    // Relanzadores shown on this dock (per-dock visibility). Primary dock shows
    // all but those in relanzadoresHidden; others show only relanzadoresShown.
    readonly property var visibleRelanzadorIds: {
        if (!relanzadores) return []
        // Touch these so the binding re-runs on add/remove and config edits.
        var _n = relanzadores.count
        var _h = config.relanzadoresHidden
        var _s = config.relanzadoresShown
        return relanzadores.visibleIds(dockIsPrimary, _h, _s)
    }

    // Script runners shown on this dock (same per-dock scheme as relanzadores).
    readonly property var visibleScriptRunnerIds: {
        if (!scriptRunners) return []
        var _n = scriptRunners.count
        var _h = config.scriptRunnersHidden
        var _s = config.scriptRunnersShown
        return scriptRunners.visibleIds(dockIsPrimary, _h, _s)
    }

    // Incremented while any drag (app icon or section) is in progress, so
    // autohide does not hide the dock mid-drag.

    readonly property int fixedLength: {
        if (config.dockLength <= 0) return 0
        var screenMain = horizontal ? Screen.width : Screen.height
        return Math.round(screenMain * config.dockLength / 100)
    }

    property int dragCount: 0

    width: horizontal
           ? (config.dockLength > 0 ? Math.max(fixedLength, thickness)
              : config.panelMode ? Math.max(Window.width, thickness)
                                 : Math.max(sectionLayout.implicitWidth + 2 * pad, thickness))
           : thickness
    height: horizontal
            ? thickness
            : (config.dockLength > 0 ? Math.max(fixedLength, thickness)
               : config.panelMode ? Math.max(Window.height, thickness)
                                  : Math.max(sectionLayout.implicitHeight + 2 * pad, thickness))

    property bool menuOpen: false
    readonly property bool dragging: dragCount > 0
    readonly property bool revealed: !config.autohide || dockHover.hovered || menuOpen || dragging

    onRevealedChanged: if (revealed) dockWindow.setHidden(false)

    // ---- Section helpers -------------------------------------------------

    function sectionVisible(token) {
        switch (token) {
        case "menu": return config.showMenuButton
        case "tilemenu": return config.showTileMenu && tileLauncher
        case "apps": return config.showAppIcons
        case "clipboard": return config.showClipboard && clipboardHistory
        case "disks": return config.showDisks && disks && disks.available
        case "network": return config.showNetwork && network && network.available
        case "iconthemes": return config.showIconThemes && appearance
        case "colorschemes": return config.showColorSchemes && appearance
        case "volume": return config.showVolume && volume.available
        case "brightness": return config.showBrightness && brightness.available
        case "battery": return config.showBattery && battery
                               && (battery.available || battery.profilesAvailable)
        case "clock": return config.showClock
        case "clock2": return config.showClock2
        case "overview": return config.showOverview && overview && overview.available
        case "movetodesktop": return config.showMoveToDesktop && desktopControl && desktopControl.available
        case "movetoscreen": return config.showMoveToScreen && monitorControl && monitorControl.available
        case "maxmin": return config.showMaxMin && maxmin && maxmin.available
        case "closewindow": return config.showCloseWindow && activeWindow && activeWindow.available
        case "nextwallpaper": return config.showNextWallpaper && wallpaperControl && wallpaperControl.available
        case "darkmode": return config.showDarkMode
        case "pager": return config.showPager && virtualDesktops && virtualDesktops.count > 0
        case "autohide": return config.showAutohideToggle
        case "showdesktop": return config.showDesktopButton && showdesktop && showdesktop.showDesktopSupported
        case "systray": return systray && config.showSystray && systray.count > 0
        case "relanzadores": return relanzadores && root.visibleRelanzadorIds.length > 0
        case "scriptrunners": return scriptRunners && root.visibleScriptRunnerIds.length > 0
        case "session": return config.showSessionButton && power && power.available
        case "settings": return config.showSettingsButton
        case "spring": return true
        case "sep": return true
        case "gap": return true
        }
        return false
    }

    function componentFor(token) {
        switch (token) {
        case "menu": return menuComp
        case "tilemenu": return tileMenuComp
        // Null, not just an invisible section: the section Loader instantiates
        // its component even when the section is hidden, and the apps block is
        // a Repeater over every launcher and window — it would keep rebuilding
        // itself behind a dock that draws none of it.
        case "apps": return config.showAppIcons ? appsComp : null
        case "clipboard": return clipboardComp
        case "disks": return disksComp
        case "network": return networkComp
        case "iconthemes": return iconThemesComp
        case "colorschemes": return colorSchemesComp
        case "volume": return volumeComp
        case "brightness": return brightnessComp
        case "battery": return batteryComp
        case "clock": return clockComp
        case "clock2": return clock2Comp
        case "overview": return overviewComp
        case "movetodesktop": return moveDesktopComp
        case "movetoscreen": return moveScreenComp
        case "maxmin": return maxMinComp
        case "closewindow": return closeWindowComp
        case "nextwallpaper": return nextWallpaperComp
        case "darkmode": return darkModeComp
        case "pager": return pagerComp
        case "autohide": return autohideComp
        case "showdesktop": return showDesktopComp
        case "systray": return systrayComp
        case "relanzadores": return relanzadoresComp
        case "scriptrunners": return scriptRunnersComp
        case "session": return sessionComp
        case "settings": return settingsComp
        case "spring": return springComp
        case "sep": return staticSepComp
        case "gap": return springComp
        }
        return null
    }

    // Blocks contain several inner icons with their own mouse handling, so
    // they are drop-only anchors (not draggable as a unit). Single widgets
    // and springs are draggable and dispatch their action from the section.
    function isBlock(token) {
        return token === "apps" || token === "systray" || token === "relanzadores"
               || token === "pager"
               || token === "scriptrunners" || token === "menu" || token === "tilemenu"
               || token === "session"
               || token === "battery" || token === "clipboard" || token === "disks"
               || token === "network" || token === "iconthemes"
               || token === "colorschemes"
    }

    function sectionTooltip(token) {
        switch (token) {
        case "volume": return volume.muted ? qsTr("Muted") : Math.round(volume.volume * 100) + " %"
        case "brightness": return Math.round(brightness.brightness * 100) + " %"
        case "clock": return Qt.formatDateTime(new Date(), "dddd, d MMMM yyyy, HH:mm:ss")
        case "clock2": return Qt.formatDateTime(new Date(), "dddd, d MMMM yyyy, HH:mm:ss")
        case "overview": return overview && overview.active ? qsTr("Close Overview") : qsTr("Open Overview")
        case "movetodesktop": return qsTr("Move window to next desktop")
        case "movetoscreen": return qsTr("Move window to next monitor (right-click: previous)")
        case "maxmin": return qsTr("Maximize window (right-click: minimize)")
        case "closewindow": return qsTr("Close window (right-click: send to next desktop, staying here)")
        case "nextwallpaper": return qsTr("Next wallpaper image")
        case "darkmode": return qsTr("Modo normal (clic derecho: modo oscuro)")
        case "iconthemes": return qsTr("Iconset de KDE")
        case "colorschemes": return qsTr("Esquema de color de KDE")
        case "autohide": return config.autohide ? qsTr("Dock auto-hides") : qsTr("Dock stays visible")
        case "showdesktop": return qsTr("Show desktop")
        case "settings": return qsTr("Configure kdock")
        case "tilemenu": return qsTr("Menú de mosaicos (pantalla completa)")
        case "spring": return qsTr("Dynamic separator")
        case "sep": return qsTr("Static separator")
        case "gap": return qsTr("Transparent separator")
        }
        return ""
    }

    function sectionClick(token) {
        if (token === "volume") volume.toggleMute()
        else if (token === "brightness") brightness.setBrightness(1.0)
        else if (token === "overview" && overview) overview.toggle()
        else if (token === "movetodesktop" && desktopControl) desktopControl.moveToNextDesktop()
        else if (token === "movetoscreen" && monitorControl) monitorControl.moveToNextScreen()
        else if (token === "maxmin" && maxmin) maxmin.maximize()
        else if (token === "closewindow" && activeWindow) activeWindow.closeActive()
        else if (token === "nextwallpaper" && wallpaperControl) wallpaperControl.nextWallpaper(config.screenName)
        else if (token === "darkmode") config.setDarkModeActive(false)
        else if (token === "autohide") config.autohide = !config.autohide
        else if (token === "showdesktop" && showdesktop) showdesktop.minimizeAllWindows()
        else if (token === "settings") dockWindow.openSettings()
        else if (token === "clock2") clock2.launch()
    }

    // Widgets whose right click is an action of their own instead of the
    // section menu. Shift+right-click still opens the menu on all of them
    // (see secMouse.onClicked).
    function sectionHasAltClick(token) {
        return token === "volume" || token === "movetoscreen" || token === "maxmin"
               || token === "closewindow" || token === "darkmode"
    }

    function sectionAltClick(token) {
        if (token === "volume") dockWindow.openAudioSettings()
        else if (token === "movetoscreen" && monitorControl) monitorControl.moveToPreviousScreen()
        else if (token === "maxmin" && maxmin) maxmin.minimize()
        else if (token === "closewindow" && activeWindow) activeWindow.sendActiveToNextDesktop()
        else if (token === "darkmode") config.setDarkModeActive(true)
    }

    function sectionWheel(token, dy) {
        const step = dy > 0 ? 0.05 : -0.05
        if (token === "volume") volume.setVolume(volume.volume + step)
        else if (token === "brightness") brightness.setBrightness(brightness.brightness + step)
    }

    HoverHandler {
        id: dockHover
    }

    Item {
        id: slider
        width: root.width
        height: root.height

        readonly property int hideDistance: root.thickness + config.effectiveMargin

        x: {
            if (root.revealed || root.horizontal) return 0
            return config.edge === 2 ? -hideDistance : hideDistance
        }
        y: {
            if (root.revealed || !root.horizontal) return 0
            return config.edge === 1 ? -hideDistance : hideDistance
        }

        Behavior on x {
            NumberAnimation {
                duration: 200
                easing.type: Easing.InOutQuad
                onRunningChanged: if (!running && !root.revealed) dockWindow.setHidden(true)
            }
        }
        Behavior on y {
            NumberAnimation {
                duration: 200
                easing.type: Easing.InOutQuad
                onRunningChanged: if (!running && !root.revealed) dockWindow.setHidden(true)
            }
        }

        // The panel background, one rectangle per run between transparent
        // separators (root.gapRuns). With no gap there is exactly one run and
        // this is the single rectangle it always was.
        Repeater {
            model: root.gapRuns
            delegate: Rectangle {
                id: background
                required property var modelData

                x: root.horizontal ? modelData.pos : 0
                y: root.horizontal ? 0 : modelData.pos
                width: root.horizontal ? modelData.size : slider.width
                height: root.horizontal ? slider.height : modelData.size

                // Every run is rounded on its own, so a gap leaves two pills
                // rather than one panel with a bite out of it.
                radius: config.panelMode && root.gapRuns.length < 2
                        ? 0 : (config.compact ? 6 : 12)
                readonly property color baseColor: root.dockBaseColor
                color: Qt.rgba(baseColor.r, baseColor.g, baseColor.b, config.opacity)
                border.width: config.panelMode && root.gapRuns.length < 2 ? 0 : 1
                border.color: Qt.rgba(theme.foreground.r, theme.foreground.g,
                                      theme.foreground.b, 0.18)

                // Optional tiled background image, drawn over the base color.
                // Scaled to the dock thickness and repeated along its length;
                // orientation-aware. Honors config.opacity, and is masked to the
                // run's rounded corners.
                Image {
                    id: bgImage
                    anchors.fill: parent
                    anchors.margins: background.border.width
                    visible: config.panelImage.length > 0
                    source: config.panelImageUrl
                    fillMode: root.horizontal ? Image.TileHorizontally : Image.TileVertically
                    opacity: config.opacity
                    asynchronous: true
                    cache: true
                    layer.enabled: visible && background.radius > 0
                    layer.effect: OpacityMask {
                        maskSource: Rectangle {
                            width: bgImage.width
                            height: bgImage.height
                            radius: background.radius
                        }
                    }
                }
            }
        }

        GridLayout {
            id: sectionLayout
            // Single line in both orientations.
            flow: root.horizontal ? GridLayout.TopToBottom : GridLayout.LeftToRight
            rows: root.horizontal ? 1 : -1
            columns: root.horizontal ? -1 : 1
            columnSpacing: root.spacingPx
            rowSpacing: root.spacingPx

            width: root.horizontal
                   ? (root.fillMain ? root.width - 2 * root.pad : implicitWidth)
                   : implicitWidth
            height: root.horizontal
                    ? implicitHeight
                    : (root.fillMain ? root.height - 2 * root.pad : implicitHeight)

            x: {
                if (root.horizontal) {
                    if (root.fillMain) return root.pad
                    if (!config.panelMode) return (parent.width - width) / 2
                    if (config.alignment === 0) return root.pad
                    if (config.alignment === 2) return parent.width - width - root.pad
                    return (parent.width - width) / 2
                }
                return (parent.width - width) / 2
            }
            y: {
                if (!root.horizontal) {
                    if (root.fillMain) return root.pad
                    if (!config.panelMode) return (parent.height - height) / 2
                    if (config.alignment === 0) return root.pad
                    if (config.alignment === 2) return parent.height - height - root.pad
                    return (parent.height - height) / 2
                }
                return (parent.height - height) / 2
            }

            Repeater {
                id: sectionRepeater
                model: config.widgetOrder
                // Sections appearing or going away change the widest drawn name.
                onCountChanged: root.scheduleLabelMeasure()

                delegate: Item {
                    id: sec
                    required property int index
                    required property string modelData

                    readonly property string token: modelData
                    // The transparent separator expands exactly like a spring;
                    // what it adds is the hole (see root.gapRuns).
                    readonly property bool isGap: token === "gap"
                    readonly property bool isSpring: token === "spring" || isGap
                    readonly property bool isStaticSep: token === "sep"
                    readonly property bool block: root.isBlock(token)
                    readonly property bool draggable: !block

                    // Name of this section, drawn around its content. The apps
                    // block is excluded: it labels each of its own icons with
                    // the separate app setting; the separators draw no name.
                    readonly property bool labelled: root.widgetLabelVisible && !isSpring
                                                     && !isStaticSep && token !== "apps"
                    readonly property string label: labelled ? root.widgetNameOf(token) : ""
                    // Renames and show/hide both move the widest drawn name.
                    onLabelChanged: root.scheduleLabelMeasure()
                    onVisibleChanged: root.scheduleLabelMeasure()
                    // Measured off-screen, never from the drawn Text: its width
                    // feeds the section size, which would loop back into it.
                    TextMetrics {
                        id: secLabelMetrics
                        font.pixelSize: root.labelFontPx
                        text: sec.label
                    }
                    // Capped to the configured box on a horizontal dock (each
                    // section shrinks to its own name); fixed on a vertical one
                    // so the dock keeps a single width.
                    // advanceWidth, not width: TextMetrics.width can come out a
                    // pixel or two under Text.implicitWidth for the same string
                    // and font, and the box built from it then elides a name
                    // that fits ("Reloj" drawn as "Re…"). advanceWidth matches
                    // implicitWidth exactly (measured 2026-08-07).
                    // The +1 is not cosmetic: with wrapping on, a box exactly
                    // as wide as the measured advance can still make Qt break
                    // the last character onto the second line ("Clock" drawn as
                    // "Cloc/k"), so a name that fits gets one pixel of slack.
                    readonly property int labelW: !labelled ? 0
                        : (root.horizontal
                           ? Math.min(Math.ceil(secLabelMetrics.advanceWidth) + root.labelSlack,
                                      root.labelBoxWidth)
                           : root.labelBoxWidth)

                    visible: root.sectionVisible(token)

                    implicitWidth: isSpring ? 0 : secVisual.implicitWidth
                    implicitHeight: isSpring ? 0 : secVisual.implicitHeight

                    Layout.fillWidth: isSpring && root.horizontal
                    Layout.fillHeight: isSpring && !root.horizontal
                    Layout.alignment: Qt.AlignCenter
                    // A section is never squeezed below the size it draws at:
                    // the dynamic separators (minimum 0) are what gives the
                    // room back. Without this the layout shrinks the cells while
                    // their content keeps its size, and the icons overlap.
                    Layout.minimumWidth: isSpring ? 0 : implicitWidth
                    Layout.minimumHeight: isSpring ? 0 : implicitHeight

                    // Drop target: any dragged section that enters swaps here.
                    DropArea {
                        anchors.fill: parent
                        onEntered: (drag) => {
                            if (drag.source && drag.source.sectionIndex !== undefined
                                    && drag.source.sectionIndex !== sec.index)
                                config.moveSection(drag.source.sectionIndex, sec.index)
                        }
                    }

                    Item {
                        id: secVisual
                        anchors.fill: sec.isSpring ? sec : undefined
                        width: sec.isSpring ? sec.width : implicitWidth
                        height: sec.isSpring ? sec.height : implicitHeight
                        // Size of the widget itself, and of the widget plus its
                        // name (implicit*): the label is laid out around the
                        // content, which keeps its own size in every mode.
                        readonly property int contentW: contentLoader.item ? contentLoader.item.implicitWidth : root.appIconPx
                        readonly property int contentH: contentLoader.item ? contentLoader.item.implicitHeight : root.appIconPx
                        implicitWidth: {
                            switch (sec.labelled ? root.widgetLabelMode : 0) {
                            case 1: case 4: return Math.max(contentW, sec.labelW)
                            case 2: case 5: return contentW + root.labelGap + sec.labelW
                            }
                            return contentW
                        }
                        implicitHeight: {
                            switch (sec.labelled ? root.widgetLabelMode : 0) {
                            case 1: case 4: return contentH + root.labelGap + root.labelBoxHeight
                            case 2: case 5: return Math.max(contentH, root.labelBoxHeight)
                            }
                            return contentH
                        }
                        anchors.horizontalCenter: sec.isSpring ? undefined : sec.horizontalCenter
                        anchors.verticalCenter: sec.isSpring ? undefined : sec.verticalCenter

                        readonly property int sectionIndex: sec.index

                        ToolTip {
                            popupType: Popup.Window
                            visible: config.showTooltips && secMouse.containsMouse
                                     && sec.draggable && !secMouse.drag.active
                                     && sec.token !== "clock2"
                            delay: 400
                            text: root.sectionTooltip(sec.token)
                        }


                        Drag.active: secMouse.drag.active
                        Drag.source: secVisual
                        Drag.hotSpot.x: width / 2
                        Drag.hotSpot.y: height / 2
                        Drag.onActiveChanged: root.dragCount += Drag.active ? 1 : -1

                        states: State {
                            when: secMouse.drag.active
                            AnchorChanges {
                                target: secVisual
                                anchors.horizontalCenter: undefined
                                anchors.verticalCenter: undefined
                            }
                            ParentChange { target: secVisual; parent: root }
                        }

                        // Faint line so an (expanded) spring is discoverable.
                        Rectangle {
                            visible: sec.isSpring
                            anchors.centerIn: parent
                            width: root.horizontal ? 2 : Math.min(parent.width, root.appIconPx) * 0.4
                            height: root.horizontal ? Math.min(parent.height, root.appIconPx) * 0.4 : 2
                            radius: 1
                            color: Qt.rgba(theme.foreground.r, theme.foreground.g,
                                           theme.foreground.b, 0.15)
                        }

                        Loader {
                            id: contentLoader
                            // Centered when there is no label; pushed aside by
                            // the name otherwise (same layout as an app cell).
                            // Centering reads secVisual.contentW/H, never the
                            // Loader's own width/height: a Loader sizes its item
                            // from its own size, so reading it here closes a
                            // binding loop (secVisual.width -> implicitWidth ->
                            // item implicit size -> Loader size) and leaves the
                            // widget drawn at a stale position.
                            x: root.widgetLabelMode === 2 && sec.labelled ? 0
                               : root.widgetLabelMode === 5 && sec.labelled
                                 ? sec.labelW + root.labelGap
                                 : (secVisual.width - secVisual.contentW) / 2
                            y: root.widgetLabelMode === 1 && sec.labelled ? 0
                               : root.widgetLabelMode === 4 && sec.labelled
                                 ? root.labelBoxHeight + root.labelGap
                                 : (secVisual.height - secVisual.contentH) / 2
                            sourceComponent: root.componentFor(sec.token)
                            Binding {
                                target: contentLoader.item
                                property: "hovered"
                                value: secMouse.containsMouse
                                // Separators draw no hover state and declare no
                                // such property: binding it warns on every load.
                                when: contentLoader.item !== null && !sec.block
                                      && !sec.isSpring && !sec.isStaticSep
                            }
                        }

                        // Section name. One label for the whole section, blocks
                        // (systray, relanzadores…) included.
                        Text {
                            visible: sec.labelled
                            text: sec.label
                            width: sec.labelW
                            height: root.labelBoxHeight
                            x: root.widgetLabelMode === 2 ? secVisual.contentW + root.labelGap
                               : root.widgetLabelMode === 5 ? 0
                                                            : (secVisual.width - width) / 2
                            y: root.widgetLabelMode === 1 ? secVisual.contentH + root.labelGap
                               : root.widgetLabelMode === 4 ? 0
                                                            : (secVisual.height - height) / 2
                            elide: Text.ElideRight
                            // With two lines allowed the name wraps inside the
                            // box instead of being elided, so a long title can be
                            // read whole; the elide still catches what does not
                            // fit in those lines. WrapAnywhere and not Wrap: a
                            // single long word (and every CJK name) has no space
                            // to break at, and Wrap would leave the second line
                            // empty and elide anyway.
                            wrapMode: root.labelLines > 1 ? Text.WrapAtWordBoundaryOrAnywhere
                                                          : Text.NoWrap
                            maximumLineCount: root.labelLines
                            horizontalAlignment: root.widgetLabelMode === 2 ? Text.AlignLeft
                                                 : root.widgetLabelMode === 5 ? Text.AlignRight
                                                                              : Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            font.pixelSize: root.labelFontPx
                            font.bold: config.labelBold
                            color: root.dockTextColor
                        }

                        // Section-level interaction for draggable (single) widgets
                        // and springs. Blocks keep their own inner mouse handling.
                        MouseArea {
                            id: secMouse
                            anchors.fill: parent
                            enabled: sec.draggable
                            visible: sec.draggable
                            hoverEnabled: true
                            acceptedButtons: Qt.LeftButton | Qt.MiddleButton | Qt.RightButton
                            onPressed: (mouse) => {
                                drag.target = mouse.button === Qt.LeftButton ? secVisual : null
                            }
                            onReleased: secVisual.Drag.drop()
                            onClicked: (mouse) => {
                                if (mouse.button === Qt.RightButton) {
                                    // A few widgets spend their right click on a
                                    // second action (mixer, previous monitor,
                                    // minimize). Shift is the universal escape
                                    // hatch back to the section menu, so the
                                    // rename / label / colour items stay
                                    // reachable on every widget.
                                    if ((mouse.modifiers & Qt.ShiftModifier)
                                        || !root.sectionHasAltClick(sec.token))
                                        sectionMenu.popup()
                                    else
                                        root.sectionAltClick(sec.token)
                                } else if (mouse.button === Qt.LeftButton)
                                    root.sectionClick(sec.token)
                            }
                            onWheel: (wheel) => root.sectionWheel(sec.token, wheel.angleDelta.y)
                            // Pull a fresh audio state when the pointer enters the
                            // volume widget, so the icon is honest and the click
                            // toggles in the direction the user expects (the cache
                            // can go stale if Plasma changes the volume externally).
                            onContainsMouseChanged: {
                                if (containsMouse && sec.token === "volume")
                                    volume.refresh()
                            }
                        }

                        Menu {
                            id: sectionMenu
                            popupType: Popup.Window
                            // A Menu doesn't size itself to its widest item.
                            width: Math.max(implicitWidth + 64, 220)
                            onAboutToShow: root.menuOpen = true
                            onClosed: root.menuOpen = false
                            IconMenuItem {
                                text: qsTr("Add dynamic separator")
                                iconName: "list-add"
                                onTriggered: config.insertSpring(sec.index + 1)
                            }
                            IconMenuItem {
                                text: qsTr("Add static separator")
                                iconName: "list-add"
                                onTriggered: config.insertSeparator(sec.index + 1)
                            }
                            IconMenuItem {
                                text: qsTr("Add transparent separator")
                                iconName: "list-add"
                                onTriggered: config.insertGap(sec.index + 1)
                            }
                            IconMenuItem {
                                visible: sec.isSpring || sec.isStaticSep
                                height: visible ? implicitHeight : 0
                                text: sec.isGap ? qsTr("Remove transparent separator")
                                                : sec.isSpring ? qsTr("Remove dynamic separator")
                                                               : qsTr("Remove static separator")
                                iconName: "list-remove"
                                onTriggered: config.removeSectionAt(sec.index)
                            }
                            MenuSeparator {}
                            BackgroundColorMenu {}
                            ModeMenu {}
                            IconLabelMenu {}
                            WidgetLabelMenu {}
                            MenuSeparator {}
                            IconMenuItem {
                                text: qsTr("Nombre…")
                                iconName: "edit-rename"
                                onTriggered: dockWindow.openSettingsToDock()
                            }
                            IconMenuItem {
                                text: qsTr("Dock settings…")
                                iconName: "configure"
                                onTriggered: dockWindow.openSettings()
                            }
                            IconMenuItem {
                                text: qsTr("Mover Sig. Monitor")
                                iconName: "go-next"
                                onTriggered: dockWindow.moveToNextMonitor()
                            }
                            IconMenuItem {
                                text: qsTr("Crear dock vacío")
                                iconName: "list-add"
                                onTriggered: dockWindow.createEmptyDock()
                            }
                            MenuSeparator {}
                            IconMenuItem {
                                text: qsTr("Borrar este Dock")
                                iconName: "edit-delete"
                                onTriggered: dockWindow.deleteDock()
                            }
                        }
                    }
                }
            }
        }
    }

    // ---- Section components ---------------------------------------------

    // Apps block: pinned launchers + running windows + static separators,
    // with per-icon drag & drop (unchanged behavior).
    Component {
        id: appsComp
        Grid {
            id: appsGrid
            columns: root.horizontal ? Math.max(1, dockRepeater.count) : 1
            spacing: root.spacingPx
            // No-ops while every cell is the same size (icon-only mode); they
            // keep the static separators centered once labels make the app
            // cells bigger than them.
            horizontalItemAlignment: Grid.AlignHCenter
            verticalItemAlignment: Grid.AlignVCenter
            move: Transition {
                NumberAnimation { properties: "x,y"; duration: 150; easing.type: Easing.OutQuad }
            }

            Repeater {
                id: dockRepeater
                model: dockModel

                delegate: Item {
                    id: delegateRoot
                    required property int index
                    required property string name
                    required property string iconName
                    required property bool pinned
                    required property int windowCount
                    required property bool active
                    required property bool minimized
                    required property bool isSeparator
                    required property string title

                    // ---- Cell geometry (icon + optional name label) --------
                    // The icon box is always root.appIconPx; the label is laid
                    // out around it, so turning labels on never resizes icons.
                    readonly property bool labelled: root.labelVisible && !delegateRoot.isSeparator

                    // Natural width of the name, measured off-screen: taking it
                    // from labelText.implicitWidth instead would feed the Text's
                    // own width binding back into itself. Always measured bold
                    // (the active app's weight) so the cell keeps its width and
                    // the dock does not reflow every time the focus moves.
                    TextMetrics {
                        id: labelMetrics
                        font.pixelSize: root.labelFontPx
                        font.bold: true
                        text: delegateRoot.name
                    }

                    // Label box width: on a horizontal dock each cell shrinks to
                    // its own name (capped, then elided), which keeps the dock
                    // compact. On a vertical dock the width is the dock's
                    // thickness, so it stays fixed for every app instead of
                    // jumping around as windows open and close.
                    readonly property int labelW: !labelled ? 0
                        : (root.horizontal
                           ? Math.min(Math.ceil(labelMetrics.advanceWidth) + root.labelSlack,
                                      root.labelBoxWidth)
                           : root.labelBoxWidth)

                    readonly property int cellW: {
                        switch (labelled ? root.labelMode : 0) {
                        case 1: case 4: return Math.max(root.appIconPx, labelW)
                        case 2: case 5: return root.appIconPx + root.labelGap + labelW
                        case 3: return labelW
                        }
                        return root.appIconPx
                    }
                    readonly property int cellH: {
                        switch (labelled ? root.labelMode : 0) {
                        case 1: case 4: return root.appIconPx + root.labelGap + root.labelBoxHeight
                        case 2: case 5: return Math.max(root.appIconPx, root.labelBoxHeight)
                        case 3: return root.labelBoxHeight
                        }
                        return root.appIconPx
                    }

                    width: delegateRoot.isSeparator ? (root.horizontal ? config.separatorSize : root.appIconPx) : cellW
                    height: delegateRoot.isSeparator ? (root.horizontal ? root.appIconPx : config.separatorSize) : cellH

                    Rectangle {
                        visible: delegateRoot.isSeparator
                        anchors.centerIn: parent
                        width: root.horizontal ? 2 : parent.width * 0.6
                        height: root.horizontal ? parent.height * 0.6 : 2
                        radius: 1
                        color: Qt.rgba(theme.foreground.r, theme.foreground.g, theme.foreground.b, 0.25)
                    }

                    DropArea {
                        visible: !delegateRoot.isSeparator
                        anchors.fill: parent
                        onEntered: (drag) => {
                            if (drag.source && drag.source.itemIndex !== undefined
                                    && drag.source.itemIndex !== delegateRoot.index)
                                dockModel.moveItem(drag.source.itemIndex, delegateRoot.index)
                        }
                    }

                    Item {
                        id: content
                        visible: !delegateRoot.isSeparator
                        // The whole cell (icon + label) drags, drops and reacts
                        // to clicks as one; in icon-only mode it is exactly the
                        // icon, as before.
                        width: delegateRoot.cellW
                        height: delegateRoot.cellH
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.verticalCenter: parent.verticalCenter

                        readonly property int itemIndex: delegateRoot.index

                        ToolTip {
                            popupType: Popup.Window
                            visible: config.showTooltips && mouseArea.containsMouse
                                     && !contextMenu.visible && !mouseArea.drag.active
                            delay: 600
                            text: delegateRoot.title || delegateRoot.name
                        }

                        Drag.active: mouseArea.drag.active
                        Drag.source: content
                        Drag.hotSpot.x: width / 2
                        Drag.hotSpot.y: height / 2
                        Drag.onActiveChanged: root.dragCount += Drag.active ? 1 : -1

                        states: State {
                            when: mouseArea.drag.active
                            AnchorChanges {
                                target: content
                                anchors.horizontalCenter: undefined
                                anchors.verticalCenter: undefined
                            }
                            ParentChange { target: content; parent: root }
                        }

                        // Running-app background: vivid fill derived from the
                        // icon's dominant color, shown only while the app runs.
                        // Grows beyond the icon into the surrounding space so
                        // the color is visible without shrinking the icon:
                        // generously across the dock thickness, and up to half
                        // the icon spacing along the dock (to avoid touching
                        // neighbours). Bigger "Icon spacing" => bigger highlight.
                        Rectangle {
                            visible: config.iconRunningBackground && !delegateRoot.isSeparator
                                     && delegateRoot.windowCount > 0
                            anchors.fill: parent
                            readonly property int alongRoom: Math.max(0, Math.floor(root.spacingPx / 2) - 1)
                            readonly property int crossRoom: Math.max(0, Math.floor((config.compact ? 12 : 20) / 2) - 2)
                            anchors.leftMargin: -(root.horizontal ? alongRoom : crossRoom)
                            anchors.rightMargin: -(root.horizontal ? alongRoom : crossRoom)
                            anchors.topMargin: -(root.horizontal ? crossRoom : alongRoom)
                            anchors.bottomMargin: -(root.horizontal ? crossRoom : alongRoom)
                            radius: config.compact ? 6 : 10
                            // Dark mode drops the per-icon coloring and paints
                            // every highlight with the single accent color.
                            readonly property color dom: config.darkModeActive
                                ? config.darkAccent
                                : iconColors
                                  ? iconColors.dominant(delegateRoot.iconName, theme.revision)
                                  : theme.highlight
                            color: Qt.rgba(dom.r, dom.g, dom.b, 0.85)
                        }

                        // Running-app edge line: a single bar drawn beside the
                        // icon, always on the screen-edge side. Active window =
                        // longer/brighter bar. Uses the icon's dominant color so
                        // it can sit alongside the color background.
                        Rectangle {
                            id: runLine
                            visible: config.iconRunningLine && !delegateRoot.isSeparator
                                     && delegateRoot.windowCount > 0
                            readonly property bool activeApp: delegateRoot.active
                            // Over the colored background use the inverted color so
                            // the bar stays visible; otherwise the dominant color.
                            readonly property color indColor: config.darkModeActive
                                // Same single-color rule, except over the accent
                                // fill, where the accent would be invisible: the
                                // dark background is the other half of the palette.
                                ? (config.iconRunningBackground ? config.darkBackground
                                                                : config.darkAccent)
                                : iconColors
                                  ? (config.iconRunningBackground
                                      ? iconColors.contrasting(delegateRoot.iconName, theme.revision)
                                      : iconColors.dominant(delegateRoot.iconName, theme.revision))
                                  : theme.highlight
                            color: Qt.rgba(indColor.r, indColor.g, indColor.b, activeApp ? 1.0 : 0.55)
                            radius: 1.5
                            readonly property int lengthPx: Math.round(root.appIconPx * (activeApp ? 0.7 : 0.45))
                            width:  root.horizontal ? lengthPx : 3
                            height: root.horizontal ? 3 : lengthPx
                            anchors.horizontalCenter: root.horizontal ? parent.horizontalCenter : undefined
                            anchors.verticalCenter:   root.horizontal ? undefined : parent.verticalCenter
                            // Always on the screen-edge side (edge: Bottom/Top/Left/Right).
                            anchors.top:    config.edge === 0 ? parent.bottom : undefined
                            anchors.bottom: config.edge === 1 ? parent.top    : undefined
                            anchors.right:  config.edge === 2 ? parent.left   : undefined
                            anchors.left:   config.edge === 3 ? parent.right   : undefined
                            anchors.margins: config.compact ? 0 : 2
                        }

                        // Fixed icon box: always root.appIconPx, whatever the
                        // label mode. The label is placed beside it (never
                        // inside), so showing the name cannot shrink the icon.
                        Item {
                            id: iconBox
                            visible: root.labelShowsIcon
                            width: root.appIconPx
                            height: root.appIconPx
                            x: root.labelMode === 2 ? 0
                               : root.labelMode === 5 ? delegateRoot.labelW + root.labelGap
                                                      : (content.width - width) / 2
                            y: root.labelMode === 1 ? 0
                               : root.labelMode === 4 ? root.labelBoxHeight + root.labelGap
                                                      : (content.height - height) / 2

                            Image {
                                id: icon
                                anchors.fill: parent
                                source: "image://icon/" + delegateRoot.iconName + "@" + theme.revision
                                sourceSize: Qt.size(root.appIconPx * Screen.devicePixelRatio,
                                                    root.appIconPx * Screen.devicePixelRatio)
                                opacity: 1.0
                                scale: mouseArea.containsMouse || mouseArea.drag.active ? 1.12 : 1.0
                                Behavior on scale { NumberAnimation { duration: 120 } }
                            }
                        }

                        // Application name. Its height is the line height the
                        // C++ side used to size the dock, so the text box and
                        // the reserved space always match.
                        Text {
                            id: labelText
                            visible: delegateRoot.labelled
                            text: delegateRoot.name
                            width: delegateRoot.labelW
                            height: root.labelBoxHeight
                            x: root.labelMode === 2 ? root.appIconPx + root.labelGap
                               : root.labelMode === 5 ? 0
                                                      : (content.width - width) / 2
                            y: root.labelMode === 1 ? root.appIconPx + root.labelGap
                               : root.labelMode === 4 ? 0
                                                      : (content.height - height) / 2
                            elide: Text.ElideRight
                            // With two lines allowed the name wraps inside the
                            // box instead of being elided, so a long title can be
                            // read whole; the elide still catches what does not
                            // fit in those lines. WrapAnywhere and not Wrap: a
                            // single long word (and every CJK name) has no space
                            // to break at, and Wrap would leave the second line
                            // empty and elide anyway.
                            wrapMode: root.labelLines > 1 ? Text.WrapAtWordBoundaryOrAnywhere
                                                          : Text.NoWrap
                            maximumLineCount: root.labelLines
                            // Side labels hug the icon, so they align toward it.
                            horizontalAlignment: root.labelMode === 2 ? Text.AlignLeft
                                                 : root.labelMode === 5 ? Text.AlignRight
                                                                        : Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            font.pixelSize: root.labelFontPx
                            font.bold: config.labelBold || delegateRoot.active
                            color: root.dockTextColor
                        }

                        Row {
                            id: dotsRow
                            visible: config.iconRunningDots
                                     && root.horizontal && delegateRoot.windowCount > 0
                            // Same coloring method as the edge line: icon's
                            // dominant color (inverted over the colored background
                            // so it stays visible), brighter for the active window.
                            readonly property color indColor: config.darkModeActive
                                // Same single-color rule, except over the accent
                                // fill, where the accent would be invisible: the
                                // dark background is the other half of the palette.
                                ? (config.iconRunningBackground ? config.darkBackground
                                                                : config.darkAccent)
                                : iconColors
                                  ? (config.iconRunningBackground
                                      ? iconColors.contrasting(delegateRoot.iconName, theme.revision)
                                      : iconColors.dominant(delegateRoot.iconName, theme.revision))
                                  : theme.highlight
                            anchors.horizontalCenter: parent.horizontalCenter
                            anchors.top: parent.bottom
                            anchors.topMargin: config.compact ? 0 : 2
                            spacing: 3
                            Repeater {
                                model: Math.min(delegateRoot.windowCount, 3)
                                Rectangle {
                                    width: 5; height: 5; radius: 2.5
                                    color: Qt.rgba(dotsRow.indColor.r, dotsRow.indColor.g, dotsRow.indColor.b,
                                                   delegateRoot.active ? 1.0 : 0.55)
                                }
                            }
                        }
                        Column {
                            id: dotsColumn
                            visible: config.iconRunningDots
                                     && !root.horizontal && delegateRoot.windowCount > 0
                            readonly property color indColor: config.darkModeActive
                                // Same single-color rule, except over the accent
                                // fill, where the accent would be invisible: the
                                // dark background is the other half of the palette.
                                ? (config.iconRunningBackground ? config.darkBackground
                                                                : config.darkAccent)
                                : iconColors
                                  ? (config.iconRunningBackground
                                      ? iconColors.contrasting(delegateRoot.iconName, theme.revision)
                                      : iconColors.dominant(delegateRoot.iconName, theme.revision))
                                  : theme.highlight
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.left: config.edge === 2 ? undefined : parent.right
                            anchors.right: config.edge === 2 ? parent.left : undefined
                            anchors.margins: config.compact ? 0 : 2
                            spacing: 3
                            Repeater {
                                model: Math.min(delegateRoot.windowCount, 3)
                                Rectangle {
                                    width: 5; height: 5; radius: 2.5
                                    color: Qt.rgba(dotsColumn.indColor.r, dotsColumn.indColor.g, dotsColumn.indColor.b,
                                                   delegateRoot.active ? 1.0 : 0.55)
                                }
                            }
                        }

                        MouseArea {
                            id: mouseArea
                            anchors.fill: parent
                            hoverEnabled: true
                            acceptedButtons: Qt.LeftButton | Qt.MiddleButton | Qt.RightButton
                            onPressed: (mouse) => {
                                drag.target = mouse.button === Qt.LeftButton ? content : null
                            }
                            onReleased: content.Drag.drop()
                            onClicked: (mouse) => {
                                if (mouse.button === Qt.LeftButton)
                                    dockModel.activate(delegateRoot.index)
                                else if (mouse.button === Qt.MiddleButton)
                                    dockModel.launch(delegateRoot.index)
                                else
                                    contextMenu.popup()
                            }
                        }

                        Menu {
                            id: contextMenu
                            popupType: Popup.Window
                            // A translated label can be noticeably longer than the capabase one, and a
                            // QtQuick Menu does not size itself to its widest item: without this the
                            // last letters are clipped by the popup border (see CLAUDE.md). The 64 px
                            // of slack is measured, not decorative: 16 still clipped in Spanish.
                            width: Math.max(implicitWidth + 64, 220)
                            onAboutToShow: {
                                root.menuOpen = true
                                windowMenuInstantiator.model = dockModel.windowList(delegateRoot.index)
                            }
                            onClosed: root.menuOpen = false

                            Instantiator {
                                id: windowMenuInstantiator
                                // Per-window list only makes sense when windows
                                // are grouped under one icon (grouped mode).
                                active: config.groupWindows && delegateRoot.windowCount > 1
                                delegate: MenuItem {
                                    required property var modelData
                                    text: modelData ? modelData.title : ""
                                    font.bold: modelData !== undefined && modelData.activated === true
                                    onTriggered: dockModel.activateWindow(delegateRoot.index,
                                                                          modelData.windowIndex)
                                }
                                onObjectAdded: (i, o) => contextMenu.insertItem(i, o)
                                onObjectRemoved: (i, o) => contextMenu.removeItem(o)
                            }

                            MenuSeparator { visible: config.groupWindows && delegateRoot.windowCount > 0 }
                            IconMenuItem {
                                text: qsTr("Open new instance")
                                iconName: "list-add"
                                onTriggered: dockModel.launch(delegateRoot.index)
                            }
                            IconMenuItem {
                                text: delegateRoot.pinned ? qsTr("Unpin") : qsTr("Pin to dock")
                                iconName: delegateRoot.pinned ? "window-unpin" : "window-pin"
                                onTriggered: dockModel.togglePinned(delegateRoot.index)
                            }
                            IconMenuItem {
                                visible: delegateRoot.windowCount > 0
                                height: visible ? implicitHeight : 0
                                text: delegateRoot.windowCount > 1 ? qsTr("Close all windows")
                                                                   : qsTr("Close window")
                                iconName: "window-close"
                                onTriggered: dockModel.closeAll(delegateRoot.index)
                            }
                            // Move this app's windows between virtual desktops
                            // without following them there. Only with a window
                            // to move and a compositor that can do it (KWin).
                            Menu {
                                id: desktopMenu
                                title: qsTr("Escritorio")
                                popupType: Popup.Window
                                // A Menu does not size itself to its widest
                                // item; without this the last letter is clipped
                                // by the popup border.
                                width: Math.max(implicitWidth + 64, 210)
                                enabled: delegateRoot.windowCount > 0 && virtualDesktops
                                         && virtualDesktops.count > 0
                                IconMenuItem {
                                    text: qsTr("Ventana aquí")
                                    iconName: "go-home"
                                    // No-op when there is no current desktop
                                    // (KWin unreachable): sendToDesktop guards.
                                    onTriggered: dockModel.sendToDesktop(
                                                     delegateRoot.index,
                                                     virtualDesktops ? virtualDesktops.current : 0)
                                }
                                MenuSeparator {}
                                // Five fixed items rather than a Repeater: the
                                // supported number of desktops is fixed at five
                                // (DockConfig::kMaxDesktops), and a Menu with
                                // dynamically inserted children is the fiddly
                                // part of QtQuick.Controls. Each one hides
                                // itself when its desktop doesn't exist.
                                IconMenuItem {
                                    readonly property int position: 1
                                    visible: virtualDesktops && virtualDesktops.count >= position
                                    height: visible ? implicitHeight : 0
                                    enabled: virtualDesktops && virtualDesktops.current !== position
                                    text: qsTr("Enviar a %1").arg(
                                              virtualDesktops ? virtualDesktops.nameOf(position) : "")
                                    iconName: "go-next"
                                    onTriggered: dockModel.sendToDesktop(delegateRoot.index, position)
                                }
                                IconMenuItem {
                                    readonly property int position: 2
                                    visible: virtualDesktops && virtualDesktops.count >= position
                                    height: visible ? implicitHeight : 0
                                    enabled: virtualDesktops && virtualDesktops.current !== position
                                    text: qsTr("Enviar a %1").arg(
                                              virtualDesktops ? virtualDesktops.nameOf(position) : "")
                                    iconName: "go-next"
                                    onTriggered: dockModel.sendToDesktop(delegateRoot.index, position)
                                }
                                IconMenuItem {
                                    readonly property int position: 3
                                    visible: virtualDesktops && virtualDesktops.count >= position
                                    height: visible ? implicitHeight : 0
                                    enabled: virtualDesktops && virtualDesktops.current !== position
                                    text: qsTr("Enviar a %1").arg(
                                              virtualDesktops ? virtualDesktops.nameOf(position) : "")
                                    iconName: "go-next"
                                    onTriggered: dockModel.sendToDesktop(delegateRoot.index, position)
                                }
                                IconMenuItem {
                                    readonly property int position: 4
                                    visible: virtualDesktops && virtualDesktops.count >= position
                                    height: visible ? implicitHeight : 0
                                    enabled: virtualDesktops && virtualDesktops.current !== position
                                    text: qsTr("Enviar a %1").arg(
                                              virtualDesktops ? virtualDesktops.nameOf(position) : "")
                                    iconName: "go-next"
                                    onTriggered: dockModel.sendToDesktop(delegateRoot.index, position)
                                }
                                IconMenuItem {
                                    readonly property int position: 5
                                    visible: virtualDesktops && virtualDesktops.count >= position
                                    height: visible ? implicitHeight : 0
                                    enabled: virtualDesktops && virtualDesktops.current !== position
                                    text: qsTr("Enviar a %1").arg(
                                              virtualDesktops ? virtualDesktops.nameOf(position) : "")
                                    iconName: "go-next"
                                    onTriggered: dockModel.sendToDesktop(delegateRoot.index, position)
                                }
                            }
                            MenuSeparator {}
                            Menu {
                                id: locationMenu
                                title: qsTr("Ubicación")
                                popupType: Popup.Window
                                IconMenuItem {
                                    text: qsTr("Arriba")
                                    iconName: "arrow-up"
                                    onTriggered: config.edge = 1   // Top
                                }
                                IconMenuItem {
                                    text: qsTr("Abajo")
                                    iconName: "arrow-down"
                                    onTriggered: config.edge = 0   // Bottom
                                }
                                IconMenuItem {
                                    text: qsTr("Izquierda")
                                    iconName: "arrow-left"
                                    onTriggered: config.edge = 2   // Left
                                }
                                IconMenuItem {
                                    text: qsTr("Derecha")
                                    iconName: "arrow-right"
                                    onTriggered: config.edge = 3   // Right
                                }
                            }
                            BackgroundColorMenu {}
                            ModeMenu {}
                            IconLabelMenu {}
                            WidgetLabelMenu {}
                            Menu {
                                id: dockMenu
                                title: qsTr("Dock")
                                popupType: Popup.Window
                                // A Menu doesn't size itself to its widest item,
                                // so "Crear dock vacío" would lose its last
                                // letter to the popup border.
                                width: Math.max(implicitWidth + 64, 200)
                                IconMenuItem {
                                    text: qsTr("Nombre…")
                                    iconName: "edit-rename"
                                    onTriggered: dockWindow.openSettingsToDock()
                                }
                                IconMenuItem {
                                    text: qsTr("Dock settings…")
                                    iconName: "configure"
                                    onTriggered: dockWindow.openSettings()
                                }
                                IconMenuItem {
                                    text: qsTr("Mover Sig. Monitor")
                                    iconName: "go-next"
                                    onTriggered: dockWindow.moveToNextMonitor()
                                }
                                IconMenuItem {
                                    text: qsTr("Crear dock vacío")
                                    iconName: "list-add"
                                    onTriggered: dockWindow.createEmptyDock()
                                }
                                MenuSeparator {}
                                IconMenuItem {
                                    text: qsTr("Reiniciar")
                                    iconName: "view-refresh"
                                    onTriggered: dockWindow.restart()
                                }
                                IconMenuItem {
                                    text: qsTr("Salir")
                                    iconName: "application-exit"
                                    onTriggered: dockWindow.quit()
                                }
                                MenuSeparator {}
                                IconMenuItem {
                                    text: qsTr("Borrar este Dock")
                                    iconName: "edit-delete"
                                    onTriggered: dockWindow.deleteDock()
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    Component {
        id: volumeComp
        Item {
            property bool hovered: false
            implicitWidth: root.widgetIconPx
            implicitHeight: root.widgetIconPx
            Image {
                anchors.centerIn: parent
                width: Math.round(root.widgetIconPx * 0.8)
                height: width
                source: "image://icon/" + volume.iconName + root.widgetIconSuffix
                sourceSize: Qt.size(root.widgetIconPx * Screen.devicePixelRatio,
                                    root.widgetIconPx * Screen.devicePixelRatio)
                opacity: volume.muted ? 0.55 : 1.0
                scale: parent.hovered ? 1.12 : 1.0
                Behavior on scale { NumberAnimation { duration: 120 } }
            }
            Rectangle {
                visible: !volume.muted
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.top: parent.bottom
                anchors.topMargin: config.compact ? 0 : 2
                width: Math.round(parent.width * 0.6 * Math.min(1, volume.volume))
                height: 3
                radius: 1.5
                color: theme.highlight
            }
        }
    }

    Component {
        id: brightnessComp
        Item {
            property bool hovered: false
            implicitWidth: root.widgetIconPx
            implicitHeight: root.widgetIconPx
            Image {
                anchors.centerIn: parent
                width: Math.round(root.widgetIconPx * 0.8)
                height: width
                source: "image://icon/display-brightness-symbolic" + root.widgetIconSuffix
                sourceSize: Qt.size(root.widgetIconPx * Screen.devicePixelRatio,
                                    root.widgetIconPx * Screen.devicePixelRatio)
                scale: parent.hovered ? 1.12 : 1.0
                Behavior on scale { NumberAnimation { duration: 120 } }
            }
            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.top: parent.bottom
                anchors.topMargin: config.compact ? 0 : 2
                width: Math.round(parent.width * 0.6 * Math.min(1, brightness.brightness))
                height: 3
                radius: 1.5
                color: theme.highlight
            }
        }
    }

    Component {
        id: clockComp
        Item {
            property bool hovered: false
            // Grows with the font so a large size is not clipped.
            implicitWidth: Math.max(root.widgetIconPx, clockCol.implicitWidth)
            implicitHeight: Math.max(root.widgetIconPx, clockCol.implicitHeight)
            Column {
                id: clockCol
                anchors.centerIn: parent
                spacing: 0
                Text {
                    id: clockTime
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: clock.timeString
                    font.pixelSize: root.clockFontPx
                    font.bold: true
                    color: root.dockTextColor
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    visible: config.clockShowDate
                    text: clock.dateString
                    font.pixelSize: Math.round(clockTime.font.pixelSize * 0.57)
                    font.bold: config.labelBold
                    color: root.dockTextColor
                    opacity: 0.75
                }
            }
        }
    }

    Component {
        id: autohideComp
        Item {
            property bool hovered: false
            implicitWidth: root.widgetIconPx
            implicitHeight: root.widgetIconPx
            Image {
                anchors.centerIn: parent
                width: Math.round(root.widgetIconPx * 0.8)
                height: width
                source: "image://icon/" + (config.autohide ? "window-unpin" : "window-pin") + root.widgetIconSuffix
                sourceSize: Qt.size(root.widgetIconPx * Screen.devicePixelRatio,
                                    root.widgetIconPx * Screen.devicePixelRatio)
                scale: parent.hovered ? 1.12 : 1.0
                Behavior on scale { NumberAnimation { duration: 120 } }
            }
        }
    }

    // Settings button: opens kdock's configuration dialog (single-icon widget,
    // click dispatched via the section MouseArea -> sectionClick).
    Component {
        id: settingsComp
        Item {
            property bool hovered: false
            implicitWidth: root.widgetIconPx
            implicitHeight: root.widgetIconPx
            Image {
                anchors.centerIn: parent
                width: Math.round(root.widgetIconPx * 0.85)
                height: width
                source: "image://icon/configure" + root.widgetIconSuffix
                sourceSize: Qt.size(root.widgetIconPx * Screen.devicePixelRatio,
                                    root.widgetIconPx * Screen.devicePixelRatio)
                opacity: 0.85
                scale: parent.hovered ? 1.12 : 1.0
                Behavior on scale { NumberAnimation { duration: 120 } }
            }
        }
    }

    // Session/power button. Block: owns its own MouseArea + menu with the five
    // session actions (logout/reboot/shutdown/lock/suspend via PowerControl).
    Component {
        id: sessionComp
        Item {
            id: sessionRoot
            implicitWidth: root.widgetIconPx
            implicitHeight: root.widgetIconPx

            ToolTip {
                popupType: Popup.Window
                visible: config.showTooltips && sessionMouse.containsMouse
                         && !sessionMenu.visible
                delay: 400
                text: qsTr("Session")
            }
            Image {
                id: sessionIcon
                anchors.centerIn: parent
                width: Math.round(root.widgetIconPx * 0.85)
                height: width
                source: "image://icon/system-shutdown" + root.widgetIconSuffix
                sourceSize: Qt.size(root.widgetIconPx * Screen.devicePixelRatio,
                                    root.widgetIconPx * Screen.devicePixelRatio)
                opacity: 0.85
                scale: sessionMouse.containsMouse ? 1.12 : 1.0
                Behavior on scale { NumberAnimation { duration: 120 } }
            }
            MouseArea {
                id: sessionMouse
                anchors.fill: parent
                hoverEnabled: true
                onClicked: sessionMenu.popup()
            }
            Menu {
                id: sessionMenu
                popupType: Popup.Window
                // A translated label can be noticeably longer than the capabase one, and a
                // QtQuick Menu does not size itself to its widest item: without this the
                // last letters are clipped by the popup border (see CLAUDE.md). The 64 px
                // of slack is measured, not decorative: 16 still clipped in Spanish.
                width: Math.max(implicitWidth + 64, 200)
                onAboutToShow: root.menuOpen = true
                onClosed: root.menuOpen = false
                IconMenuItem {
                    text: qsTr("Lock")
                    iconName: "system-lock-screen"
                    onTriggered: power.lock()
                }
                IconMenuItem {
                    text: qsTr("Suspend")
                    iconName: "system-suspend"
                    onTriggered: power.suspend()
                }
                MenuSeparator {}
                IconMenuItem {
                    text: qsTr("Log Out…")
                    iconName: "system-log-out"
                    onTriggered: power.logout()
                }
                IconMenuItem {
                    text: qsTr("Restart…")
                    iconName: "system-reboot"
                    onTriggered: power.reboot()
                }
                IconMenuItem {
                    text: qsTr("Shut Down…")
                    iconName: "system-shutdown"
                    onTriggered: power.shutdown()
                }
            }
        }
    }

    // Battery / power-profile widget. Block: owns its own MouseArea + a menu
    // that lists the available power profiles (power-profiles-daemon).
    Component {
        id: batteryComp
        Item {
            id: batteryRoot
            implicitWidth: root.widgetIconPx
            implicitHeight: root.widgetIconPx

            ToolTip {
                popupType: Popup.Window
                visible: config.showTooltips && batteryMouse.containsMouse
                         && !profileMenu.visible
                delay: 400
                text: battery.tooltipText
            }

            function profileIcon(p) {
                if (p === "power-saver") return "power-profile-power-saver"
                if (p === "balanced") return "power-profile-balanced"
                if (p === "performance") return "power-profile-performance"
                return "power-profile-balanced"
            }
            function profileLabel(p) {
                if (p === "power-saver") return qsTr("Power Saver")
                if (p === "balanced") return qsTr("Balanced")
                if (p === "performance") return qsTr("Performance")
                return p
            }

            Image {
                id: batteryIcon
                anchors.centerIn: parent
                width: Math.round(root.widgetIconPx * 0.85)
                height: width
                source: "image://icon/" + battery.iconName + root.widgetIconSuffix
                sourceSize: Qt.size(root.widgetIconPx * Screen.devicePixelRatio,
                                    root.widgetIconPx * Screen.devicePixelRatio)
                scale: batteryMouse.containsMouse ? 1.12 : 1.0
                Behavior on scale { NumberAnimation { duration: 120 } }
            }
            // Charge level bar (only when a real battery exists).
            Rectangle {
                visible: battery.available
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.top: parent.bottom
                anchors.topMargin: config.compact ? 0 : 2
                width: Math.round(parent.width * 0.6 * Math.min(1, battery.percentage / 100))
                height: 3
                radius: 1.5
                color: battery.percentage <= 15 && !battery.charging
                       ? "#e05050" : theme.highlight
            }
            MouseArea {
                id: batteryMouse
                anchors.fill: parent
                hoverEnabled: true
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                onClicked: {
                    if (battery.profilesAvailable)
                        profileMenu.popup()
                }
            }
            // Static profile items (power-profiles-daemon exposes at most these
            // three). Built statically rather than via an Instantiator because a
            // dynamically-populated `popupType: Popup.Window` menu opens its
            // window before the items are inserted on Wayland, showing an empty
            // white box. Each item hides itself when the daemon doesn't offer it.
            Menu {
                id: profileMenu
                popupType: Popup.Window
                // A translated label can be noticeably longer than the capabase one, and a
                // QtQuick Menu does not size itself to its widest item: without this the
                // last letters are clipped by the popup border (see CLAUDE.md). The 64 px
                // of slack is measured, not decorative: 16 still clipped in Spanish.
                width: Math.max(implicitWidth + 64, 200)
                onAboutToShow: root.menuOpen = true
                onClosed: root.menuOpen = false
                IconMenuItem {
                    visible: battery.profiles.indexOf("power-saver") >= 0
                    height: visible ? implicitHeight : 0
                    text: batteryRoot.profileLabel("power-saver")
                           + (battery.activeProfile === "power-saver" ? "  ✓" : "")
                    iconName: batteryRoot.profileIcon("power-saver")
                    onTriggered: battery.setProfile("power-saver")
                }
                IconMenuItem {
                    visible: battery.profiles.indexOf("balanced") >= 0
                    height: visible ? implicitHeight : 0
                    text: batteryRoot.profileLabel("balanced")
                           + (battery.activeProfile === "balanced" ? "  ✓" : "")
                    iconName: batteryRoot.profileIcon("balanced")
                    onTriggered: battery.setProfile("balanced")
                }
                IconMenuItem {
                    visible: battery.profiles.indexOf("performance") >= 0
                    height: visible ? implicitHeight : 0
                    text: batteryRoot.profileLabel("performance")
                           + (battery.activeProfile === "performance" ? "  ✓" : "")
                    iconName: batteryRoot.profileIcon("performance")
                    onTriggered: battery.setProfile("performance")
                }
            }
        }
    }

    // Application menu (XDG / KMenu-like). Block: it owns its own button
    // mouse handling and popup.
    Component {
        id: menuComp
        Item {
            id: menuRoot
            // Alias context properties to avoid self-shadowing inside
            // AppMenuPopup's same-named required properties.
            property var _theme: theme
            property var _config: config
            // Applications Menu is exempt from Widget icon scale: it always
            // draws at the full icon size.
            implicitWidth: root.appIconPx
            implicitHeight: root.appIconPx

            ToolTip {
                popupType: Popup.Window
                visible: config.showTooltips && menuMouse.containsMouse
                         && !(menuLoader.item && menuLoader.item.visible)
                delay: 400
                text: qsTr("Applications")
            }

            Image {
                id: menuIcon
                anchors.centerIn: parent
                width: root.appIconPx
                height: width
                source: "image://icon/" + (config.menuIcon || "applications-all") + root.widgetIconSuffix
                sourceSize: Qt.size(root.appIconPx * Screen.devicePixelRatio,
                                    root.appIconPx * Screen.devicePixelRatio)
                scale: menuMouse.containsMouse
                        || (menuLoader.item && menuLoader.item.visible) ? 1.12 : 1.0
                Behavior on scale { NumberAnimation { duration: 120 } }
            }
            MouseArea {
                id: menuMouse
                anchors.fill: parent
                hoverEnabled: true
                // This is a block, so the section-level MouseArea that gives the
                // draggable widgets their context menu is disabled here
                // (secMouse: enabled: sec.draggable) — the right button has to be
                // handled by this one.
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                // The modal popup dismisses itself on the press that lands on
                // this icon, so by onClicked it is already hidden. Suppress the
                // reopen when the click is the one that just closed it (second
                // click on the icon = toggle closed).
                onClicked: (mouse) => {
                    if (mouse.button === Qt.RightButton) { menuCtxMenu.popup(); return }
                    if (menuLoader.active && menuLoader.item.visible) {
                        menuLoader.item.close(); return
                    }
                    if (Date.now() - menuLoader.closedAt < 300) return
                    menuLoader.active = true
                    menuLoader.item.open()
                }
            }

            Menu {
                id: menuCtxMenu
                popupType: Popup.Window
                // A translated label can be noticeably longer than the capabase one, and a
                // QtQuick Menu does not size itself to its widest item: without this the
                // last letters are clipped by the popup border (see CLAUDE.md). The 64 px
                // of slack is measured, not decorative: 16 still clipped in Spanish.
                width: Math.max(implicitWidth + 64, 220)
                onAboutToShow: root.menuOpen = true
                onClosed: root.menuOpen = false
                IconMenuItem {
                    text: qsTr("Edit menu…")
                    iconName: "kmenuedit"
                    onTriggered: appMenu.launchMenuEditor()
                }
                MenuSeparator {}
                IconMenuItem {
                    text: qsTr("Dock settings…")
                    iconName: "configure"
                    onTriggered: dockWindow.openSettings()
                }
            }

            Loader {
                id: menuLoader
                active: false
                property double closedAt: 0
                sourceComponent: AppMenuPopup {
                    id: menuPopup
                    theme: menuRoot._theme
                    config: menuRoot._config
                    parent: menuRoot
                    onVisibleChanged: {
                        root.menuOpen = visible
                        if (!visible) {
                            menuLoader.closedAt = Date.now()
                            idleTimer.restart()
                        }
                    }
                    x: {
                        if (menuRoot._config.edge === 2) return menuRoot.width + 8
                        if (menuRoot._config.edge === 3) return -width - 8
                        return -width / 2 + menuRoot.width / 2
                    }
                    y: {
                        if (menuRoot._config.edge === 0) return -height - 8
                        if (menuRoot._config.edge === 1) return menuRoot.height + 8
                        return 0
                    }
                    Timer {
                        id: idleTimer
                        interval: 30000
                        onTriggered: menuLoader.active = false
                    }
                }
            }
        }
    }

    // Full-screen tile menu. A block, like the application menu: it owns its
    // own mouse handling. There is no popup here — the menu is a window of the
    // separate kdock-tilemenu process, and this widget only toggles it.
    Component {
        id: tileMenuComp
        Item {
            id: tileRoot
            // Same exemption the Applications Menu gets: always the base icon
            // size, never the widget icon scale.
            implicitWidth: root.appIconPx
            implicitHeight: root.appIconPx

            ToolTip {
                popupType: Popup.Window
                visible: config.showTooltips && tileMouse.containsMouse
                delay: 400
                text: qsTr("Menú de mosaicos")
            }

            Image {
                id: tileIcon
                anchors.centerIn: parent
                width: root.appIconPx
                height: width
                source: "image://icon/" + (config.tileMenuIcon || "view-list-icons")
                        + root.widgetIconSuffix
                sourceSize: Qt.size(root.appIconPx * Screen.devicePixelRatio,
                                    root.appIconPx * Screen.devicePixelRatio)
                scale: tileMouse.containsMouse ? 1.12 : 1.0
                Behavior on scale { NumberAnimation { duration: 120 } }
            }
            MouseArea {
                id: tileMouse
                anchors.fill: parent
                hoverEnabled: true
                // A block, so the section-level MouseArea is disabled here and
                // the right button has to be handled by this one.
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                onClicked: (mouse) => {
                    if (mouse.button === Qt.RightButton) { tileCtxMenu.popup(); return }
                    // The dock's connector, so the menu opens on this monitor.
                    tileLauncher.toggle(config.screenName)
                }
            }

            Menu {
                id: tileCtxMenu
                popupType: Popup.Window
                // A translated label can be noticeably longer than the capabase one, and a
                // QtQuick Menu does not size itself to its widest item: without this the
                // last letters are clipped by the popup border (see CLAUDE.md). The 64 px
                // of slack is measured, not decorative: 16 still clipped in Spanish.
                width: Math.max(implicitWidth + 64, 220)
                onAboutToShow: root.menuOpen = true
                onClosed: root.menuOpen = false
                IconMenuItem {
                    text: qsTr("Configurar el menú de mosaicos…")
                    iconName: "configure"
                    onTriggered: tileLauncher.openSettings()
                }
                MenuSeparator {}
                IconMenuItem {
                    text: qsTr("Dock settings…")
                    iconName: "configure"
                    onTriggered: dockWindow.openSettings()
                }
            }
        }
    }

    // Clipboard history. Block: it owns its own button mouse handling, the
    // history popup (left click) and a context menu (right click).
    Component {
        id: clipboardComp
        Item {
            id: clipRoot
            // Alias context properties to avoid self-shadowing inside
            // ClipboardPopup's same-named required properties.
            property var _theme: theme
            property var _config: config
            implicitWidth: root.widgetIconPx
            implicitHeight: root.widgetIconPx

            ToolTip {
                popupType: Popup.Window
                visible: config.showTooltips && clipMouse.containsMouse
                         && !(clipLoader.item && clipLoader.item.visible)
                delay: 400
                text: qsTr("Portapapeles")
            }

            Image {
                id: clipIcon
                anchors.centerIn: parent
                width: root.widgetIconPx
                height: width
                source: "image://icon/edit-paste" + root.widgetIconSuffix
                sourceSize: Qt.size(root.widgetIconPx * Screen.devicePixelRatio,
                                    root.widgetIconPx * Screen.devicePixelRatio)
                scale: clipMouse.containsMouse
                        || (clipLoader.item && clipLoader.item.visible) ? 1.12 : 1.0
                Behavior on scale { NumberAnimation { duration: 120 } }
            }
            MouseArea {
                id: clipMouse
                anchors.fill: parent
                hoverEnabled: true
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                // The modal popup dismisses itself on the press that lands on
                // this icon, so by onClicked it is already hidden. Suppress the
                // reopen when the click is the one that just closed it.
                onClicked: (mouse) => {
                    if (mouse.button === Qt.RightButton) { clipMenu.popup(); return }
                    if (clipLoader.active && clipLoader.item.visible) {
                        clipLoader.item.close(); return
                    }
                    if (Date.now() - clipLoader.closedAt < 300) return
                    clipLoader.active = true
                    clipLoader.item.open()
                }
            }

            Menu {
                id: clipMenu
                popupType: Popup.Window
                // Mixing checkable items (which reserve a tick column) with
                // IconMenuItem makes the implicit width come out short and the
                // last letter is clipped by the popup border.
                width: Math.max(implicitWidth + 64, 220)
                IconMenuItem {
                    text: qsTr("Borrar Historial")
                    iconName: "edit-clear-history"
                    onTriggered: clipboardHistory.clearHistory()
                }
                IconMenuItem {
                    text: qsTr("Ver historial actual")
                    iconName: "document-open"
                    onTriggered: clipboardHistory.openInEditor()
                }
                IconMenuItem {
                    text: qsTr("Guardar historial")
                    iconName: "document-save"
                    onTriggered: clipboardHistory.saveHistoryDialog()
                }
                MenuSeparator {}
                MenuItem {
                    text: qsTr("Guardar imágenes")
                    checkable: true
                    checked: clipboardHistory.captureImages
                    onTriggered: clipboardHistory.captureImages = checked
                }
            }

            Loader {
                id: clipLoader
                active: false
                property double closedAt: 0
                sourceComponent: ClipboardPopup {
                    id: clipPopup
                    theme: clipRoot._theme
                    config: clipRoot._config
                    parent: clipRoot
                    onVisibleChanged: {
                        root.menuOpen = visible
                        if (!visible) {
                            clipLoader.closedAt = Date.now()
                            idleClipTimer.restart()
                        }
                    }
                    x: {
                        if (clipRoot._config.edge === 2) return clipRoot.width + 8
                        if (clipRoot._config.edge === 3) return -width - 8
                        return -width / 2 + clipRoot.width / 2
                    }
                    y: {
                        if (clipRoot._config.edge === 0) return -height - 8
                        if (clipRoot._config.edge === 1) return clipRoot.height + 8
                        return 0
                    }
                    Timer {
                        id: idleClipTimer
                        interval: 30000
                        onTriggered: clipLoader.active = false
                    }
                }
            }
        }
    }

    // Removable disks (UDisks2). Block: its own button + volumes popup.
    Component {
        id: disksComp
        Item {
            id: disksRoot
            property var _theme: theme
            property var _config: config
            implicitWidth: root.widgetIconPx
            implicitHeight: root.widgetIconPx

            ToolTip {
                popupType: Popup.Window
                visible: config.showTooltips && disksMouse.containsMouse
                         && !(disksLoader.item && disksLoader.item.visible)
                delay: 400
                text: qsTr("Dispositivos extraíbles")
            }

            Image {
                id: disksIcon
                anchors.centerIn: parent
                width: root.widgetIconPx
                height: width
                source: "image://icon/drive-removable-media-usb" + root.widgetIconSuffix
                sourceSize: Qt.size(root.widgetIconPx * Screen.devicePixelRatio,
                                    root.widgetIconPx * Screen.devicePixelRatio)
                scale: disksMouse.containsMouse
                        || (disksLoader.item && disksLoader.item.visible) ? 1.12 : 1.0
                Behavior on scale { NumberAnimation { duration: 120 } }
            }
            MouseArea {
                id: disksMouse
                anchors.fill: parent
                hoverEnabled: true
                acceptedButtons: Qt.LeftButton
                onClicked: {
                    if (disksLoader.active && disksLoader.item.visible) {
                        disksLoader.item.close(); return
                    }
                    if (Date.now() - disksLoader.closedAt < 300) return
                    disksLoader.active = true
                    disksLoader.item.open()
                }
            }

            Loader {
                id: disksLoader
                active: false
                property double closedAt: 0
                sourceComponent: DisksPopup {
                    id: disksPopup
                    theme: disksRoot._theme
                    config: disksRoot._config
                    parent: disksRoot
                    onVisibleChanged: {
                        root.menuOpen = visible
                        if (!visible) {
                            disksLoader.closedAt = Date.now()
                            idleDisksTimer.restart()
                        }
                    }
                    x: {
                        if (disksRoot._config.edge === 2) return disksRoot.width + 8
                        if (disksRoot._config.edge === 3) return -width - 8
                        return -width / 2 + disksRoot.width / 2
                    }
                    y: {
                        if (disksRoot._config.edge === 0) return -height - 8
                        if (disksRoot._config.edge === 1) return disksRoot.height + 8
                        return 0
                    }
                    Timer {
                        id: idleDisksTimer
                        interval: 30000
                        onTriggered: disksLoader.active = false
                    }
                }
            }
        }
    }

    // Network status (NetworkManager). Block: its own button + connections popup.
    Component {
        id: networkComp
        Item {
            id: netRoot
            property var _theme: theme
            property var _config: config
            implicitWidth: root.widgetIconPx
            implicitHeight: root.widgetIconPx

            ToolTip {
                popupType: Popup.Window
                visible: config.showTooltips && netMouse.containsMouse
                         && !(netLoader.item && netLoader.item.visible)
                delay: 400
                text: network && network.primaryName ? network.primaryName : qsTr("Red")
            }

            Image {
                id: netIcon
                anchors.centerIn: parent
                width: root.widgetIconPx
                height: width
                source: "image://icon/" + (network ? network.iconName : "network-offline")
                        + root.widgetIconSuffix
                sourceSize: Qt.size(root.widgetIconPx * Screen.devicePixelRatio,
                                    root.widgetIconPx * Screen.devicePixelRatio)
                scale: netMouse.containsMouse
                        || (netLoader.item && netLoader.item.visible) ? 1.12 : 1.0
                Behavior on scale { NumberAnimation { duration: 120 } }
            }
            MouseArea {
                id: netMouse
                anchors.fill: parent
                hoverEnabled: true
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                onClicked: (mouse) => {
                    // The section menu never reaches a block widget (secMouse is
                    // disabled for those), so the right click is this widget's
                    // own menu — same arrangement as the clipboard.
                    if (mouse.button === Qt.RightButton) { netMenu.popup(); return }
                    if (netLoader.active && netLoader.item.visible) {
                        netLoader.item.close(); return
                    }
                    if (Date.now() - netLoader.closedAt < 300) return
                    netLoader.active = true
                    netLoader.item.open()
                }
            }

            Menu {
                id: netMenu
                popupType: Popup.Window
                width: Math.max(implicitWidth + 64, 220)
                IconMenuItem {
                    text: qsTr("Configurar redes…")
                    iconName: "configure"
                    onTriggered: dockWindow.openNetworkSettings()
                }
                IconMenuItem {
                    text: qsTr("Buscar redes Wi-Fi")
                    iconName: "view-refresh"
                    enabled: network && network.wifiAvailable && network.wifiEnabled
                    onTriggered: network.requestScan()
                }
                MenuSeparator {}
                MenuItem {
                    text: qsTr("Wi-Fi")
                    checkable: true
                    enabled: network && network.wifiAvailable
                    checked: network ? network.wifiEnabled : false
                    onTriggered: network.setWifiEnabled(checked)
                }
            }

            Loader {
                id: netLoader
                active: false
                property double closedAt: 0
                sourceComponent: NetworkPopup {
                    id: netPopup
                    theme: netRoot._theme
                    config: netRoot._config
                    parent: netRoot
                    onVisibleChanged: {
                        root.menuOpen = visible
                        if (!visible) {
                            netLoader.closedAt = Date.now()
                            idleNetTimer.restart()
                        }
                    }
                    x: {
                        if (netRoot._config.edge === 2) return netRoot.width + 8
                        if (netRoot._config.edge === 3) return -width - 8
                        return -width / 2 + netRoot.width / 2
                    }
                    y: {
                        if (netRoot._config.edge === 0) return -height - 8
                        if (netRoot._config.edge === 1) return netRoot.height + 8
                        return 0
                    }
                    Timer {
                        id: idleNetTimer
                        interval: 30000
                        onTriggered: netLoader.active = false
                    }
                }
            }
        }
    }

    Component {
        id: showDesktopComp
        Item {
            property bool hovered: false
            // Show Desktop is exempt from Widget icon scale: it always renders
            // at the base iconSize.
            implicitWidth: root.appIconPx
            implicitHeight: root.appIconPx
            Image {
                anchors.centerIn: parent
                width: root.appIconPx
                height: width
                source: "image://icon/user-desktop" + root.widgetIconSuffix
                sourceSize: Qt.size(root.appIconPx * Screen.devicePixelRatio,
                                    root.appIconPx * Screen.devicePixelRatio)
                opacity: 0.85
                scale: parent.hovered ? 1.12 : 1.0
                Behavior on scale { NumberAnimation { duration: 120 } }
            }
        }
    }

    // Virtual-desktop pager: one number per desktop, click to switch. A block,
    // because each number is its own click target. Deliberately plain — no
    // window miniatures, just the number and a filled box for the current one.
    Component {
        id: pagerComp
        Grid {
            id: pagerGrid
            columns: root.horizontal ? Math.max(1, pagerRepeater.count) : 1
            spacing: Math.max(1, Math.round(root.spacingPx / 2))
            // The number sits on theme.highlight when the desktop is current,
            // so it needs the same luminance test the dock does over its own
            // background (see root.dockBaseIsLight).
            readonly property color highlightTextColor: {
                const h = theme.highlight
                return (0.299 * h.r + 0.587 * h.g + 0.114 * h.b) > 0.5 ? "#141414" : "#F2F2F2"
            }
            Repeater {
                id: pagerRepeater
                // Capped at the same number of desktops the rest of kdock
                // supports (DockConfig::kMaxDesktops).
                model: virtualDesktops ? Math.min(virtualDesktops.count, 5) : 0
                Item {
                    id: pagerCell
                    required property int index
                    readonly property int position: index + 1
                    readonly property bool isCurrent:
                        virtualDesktops && virtualDesktops.current === pagerCell.position
                    // Square cells, a bit smaller than a widget icon: the three
                    // together cost about as much dock length as one widget,
                    // and the section layout centers the block in the row.
                    readonly property int side:
                        Math.max(Math.round(root.widgetIconPx * 0.62),
                                 pagerNumber.implicitWidth + 8)
                    width: pagerCell.side
                    height: pagerCell.side

                    Rectangle {
                        anchors.fill: parent
                        radius: 3
                        color: pagerCell.isCurrent ? theme.highlight : "transparent"
                        border.width: 1
                        border.color: root.dockTextColor
                        opacity: pagerCell.isCurrent ? 0.9
                                 : (pagerMouse.containsMouse ? 0.55 : 0.28)
                        Behavior on opacity { NumberAnimation { duration: 120 } }
                    }
                    Text {
                        id: pagerNumber
                        anchors.centerIn: parent
                        text: pagerCell.position
                        font.pixelSize: Math.max(8, Math.round(root.widgetIconPx * 0.52))
                        font.bold: config.labelBold || pagerCell.isCurrent
                        // pagerGrid contains this delegate, so its id resolves
                        // here (the reverse would not).
                        color: pagerCell.isCurrent ? pagerGrid.highlightTextColor
                                                   : root.dockTextColor
                    }
                    MouseArea {
                        id: pagerMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: if (virtualDesktops) virtualDesktops.switchTo(pagerCell.position)
                    }
                }
            }
        }
    }

    // Systray block: several icons, each with its own mouse handling.
    Component {
        id: systrayComp
        Row {
            spacing: root.spacingPx
            Repeater {
                id: systrayRepeater
                model: systray
                Item {
                    id: systrayItem
                    width: root.systrayIconPx
                    height: root.systrayIconPx

                    ToolTip {
                        popupType: Popup.Window
                        visible: config.showTooltips && systrayMouse.containsMouse
                        delay: 400
                        text: model.tooltip || model.service
                    }

                    Image {
                        anchors.centerIn: parent
                        width: Math.round(root.systrayIconPx * 0.75)
                        height: width
                        // Themed icon when the item provides one; otherwise fall
                        // back to its raw IconPixmap via the systray provider.
                        source: model.iconName
                            ? "image://icon/" + model.iconName + "@" + theme.revision
                            : "image://systray/" + model.service + "@" + model.iconSerial
                        sourceSize: Qt.size(root.systrayIconPx * Screen.devicePixelRatio,
                                            root.systrayIconPx * Screen.devicePixelRatio)
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
                                // A menu-only item has nothing to activate: the
                                // spec says show the menu. Everything else gets
                                // Activate, and falls back to the menu if the
                                // item does not really implement it (the model
                                // turns that failure into menuReady).
                                if (model.itemIsMenu && model.hasMenu)
                                    systrayItem.openMenu(gx, gy)
                                else
                                    systray.activate(index, gx, gy)
                            } else if (mouse.button === Qt.RightButton) {
                                // Never ContextMenu when the item has a real
                                // menu: asking the item to draw it cannot work
                                // on Wayland (no surface to parent a popup to).
                                if (model.hasMenu)
                                    systrayItem.openMenu(gx, gy)
                                else
                                    systray.contextMenu(index, gx, gy)
                            } else if (mouse.button === Qt.MiddleButton) {
                                systray.secondaryActivate(index, gx, gy)
                            }
                        }
                    }

                    // ---- The item's own menu, drawn by us ------------------
                    // Fetching it is asynchronous, so a click only *asks*; the
                    // menu opens when the layout arrives.
                    property bool menuWanted: false
                    function openMenu(gx, gy) {
                        systrayItem.menuWanted = true
                        systrayItem.pendingX = gx
                        systrayItem.pendingY = gy
                        systray.requestMenu(index)
                    }
                    property int pendingX: 0
                    property int pendingY: 0

                    SystrayMenu {
                        id: itemMenu
                        itemRow: index
                        service: model.service
                        onAboutToShow: root.menuOpen = true
                        onOpened: systray.setMenuOpen(index, true)
                        onClosed: {
                            root.menuOpen = false
                            systray.setMenuOpen(index, false)
                        }
                    }

                    Connections {
                        target: systray
                        function onMenuReady(row) {
                            if (row !== index || !systrayItem.menuWanted)
                                return
                            systrayItem.menuWanted = false
                            itemMenu.nodes = systray.menuTree(index)
                            itemMenu.popup(systrayItem.menuOriginX(),
                                           systrayItem.menuOriginY())
                        }
                        function onMenuFailed(row) {
                            if (row !== index || !systrayItem.menuWanted)
                                return
                            systrayItem.menuWanted = false
                            // No menu to draw: let the item try its own way.
                            systray.contextMenu(index, systrayItem.pendingX,
                                                systrayItem.pendingY)
                        }
                        function onMenuInvalidated(row) {
                            // Keep an open menu in step with the item.
                            if (row === index && itemMenu.visible)
                                itemMenu.nodes = systray.menuTree(index)
                        }
                    }

                    // Opens away from the dock edge, like every other popup.
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
    }

    // KDE appearance pickers: same widget twice, differing only in the icon and
    // the popup mode (see ThemeListPopup / AppearanceControl). Deliberately do
    // not touch kdock's own icon-theme override: a dock that has one keeps its
    // icons and only the rest of the desktop follows the choice.
    Component {
        id: iconThemesComp
        Item {
            id: itRoot
            property var _theme: theme
            property var _config: config
            implicitWidth: root.widgetIconPx
            implicitHeight: root.widgetIconPx

            ToolTip {
                popupType: Popup.Window
                visible: config.showTooltips && itMouse.containsMouse
                         && !(itLoader.item && itLoader.item.visible)
                delay: 400
                text: appearance && appearance.currentIconTheme
                      ? qsTr("Iconset: %1").arg(appearance.currentIconTheme)
                      : qsTr("Iconset de KDE")
            }

            Image {
                anchors.centerIn: parent
                width: root.widgetIconPx
                height: width
                source: "image://icon/preferences-desktop-icons" + root.widgetIconSuffix
                sourceSize: Qt.size(root.widgetIconPx * Screen.devicePixelRatio,
                                    root.widgetIconPx * Screen.devicePixelRatio)
                scale: itMouse.containsMouse
                        || (itLoader.item && itLoader.item.visible) ? 1.12 : 1.0
                Behavior on scale { NumberAnimation { duration: 120 } }
            }
            MouseArea {
                id: itMouse
                anchors.fill: parent
                hoverEnabled: true
                acceptedButtons: Qt.LeftButton
                onClicked: {
                    if (itLoader.active && itLoader.item.visible) {
                        itLoader.item.close(); return
                    }
                    if (Date.now() - itLoader.closedAt < 300) return
                    itLoader.active = true
                    itLoader.item.open()
                }
            }

            Loader {
                id: itLoader
                active: false
                property double closedAt: 0
                sourceComponent: ThemeListPopup {
                    id: itPopup
                    mode: "icons"
                    theme: itRoot._theme
                    config: itRoot._config
                    parent: itRoot
                    onVisibleChanged: {
                        root.menuOpen = visible
                        if (!visible) {
                            itLoader.closedAt = Date.now()
                            idleItTimer.restart()
                        }
                    }
                    x: {
                        if (itRoot._config.edge === 2) return itRoot.width + 8
                        if (itRoot._config.edge === 3) return -width - 8
                        return -width / 2 + itRoot.width / 2
                    }
                    y: {
                        if (itRoot._config.edge === 0) return -height - 8
                        if (itRoot._config.edge === 1) return itRoot.height + 8
                        return 0
                    }
                    Timer {
                        id: idleItTimer
                        interval: 30000
                        onTriggered: itLoader.active = false
                    }
                }
            }
        }
    }

    Component {
        id: colorSchemesComp
        Item {
            id: csRoot
            property var _theme: theme
            property var _config: config
            implicitWidth: root.widgetIconPx
            implicitHeight: root.widgetIconPx

            ToolTip {
                popupType: Popup.Window
                visible: config.showTooltips && csMouse.containsMouse
                         && !(csLoader.item && csLoader.item.visible)
                delay: 400
                text: appearance && appearance.currentColorScheme
                      ? qsTr("Colores: %1").arg(appearance.currentColorScheme)
                      : qsTr("Esquema de color de KDE")
            }

            Image {
                anchors.centerIn: parent
                width: root.widgetIconPx
                height: width
                source: "image://icon/preferences-desktop-color" + root.widgetIconSuffix
                sourceSize: Qt.size(root.widgetIconPx * Screen.devicePixelRatio,
                                    root.widgetIconPx * Screen.devicePixelRatio)
                scale: csMouse.containsMouse
                        || (csLoader.item && csLoader.item.visible) ? 1.12 : 1.0
                Behavior on scale { NumberAnimation { duration: 120 } }
            }
            MouseArea {
                id: csMouse
                anchors.fill: parent
                hoverEnabled: true
                acceptedButtons: Qt.LeftButton
                onClicked: {
                    if (csLoader.active && csLoader.item.visible) {
                        csLoader.item.close(); return
                    }
                    if (Date.now() - csLoader.closedAt < 300) return
                    csLoader.active = true
                    csLoader.item.open()
                }
            }

            Loader {
                id: csLoader
                active: false
                property double closedAt: 0
                sourceComponent: ThemeListPopup {
                    id: csPopup
                    mode: "colors"
                    theme: csRoot._theme
                    config: csRoot._config
                    parent: csRoot
                    onVisibleChanged: {
                        root.menuOpen = visible
                        if (!visible) {
                            csLoader.closedAt = Date.now()
                            idleCsTimer.restart()
                        }
                    }
                    x: {
                        if (csRoot._config.edge === 2) return csRoot.width + 8
                        if (csRoot._config.edge === 3) return -width - 8
                        return -width / 2 + csRoot.width / 2
                    }
                    y: {
                        if (csRoot._config.edge === 0) return -height - 8
                        if (csRoot._config.edge === 1) return csRoot.height + 8
                        return 0
                    }
                    Timer {
                        id: idleCsTimer
                        interval: 30000
                        onTriggered: csLoader.active = false
                    }
                }
            }
        }
    }

    // Relanzadores block: one RelanzadorWidget per relanzador.
    Component {
        id: relanzadoresComp
        Grid {
            columns: root.horizontal ? Math.max(1, root.visibleRelanzadorIds.length) : 1
            spacing: root.spacingPx
            Repeater {
                model: root.visibleRelanzadorIds
                Item {
                    required property int index
                    required property var modelData
                    // Alias context properties to avoid self-shadowing inside
                    // RelanzadorWidget's same-named properties.
                    property var _config: config
                    property var _theme: theme
                    property var _apps: apps
                    property var _relanzadores: relanzadores
                    width: root.appIconPx
                    height: root.appIconPx
                    RelanzadorWidget {
                        anchors.centerIn: parent
                        relanzador: _relanzadores.get(modelData)
                        iconSize: root.appIconPx
                        spacing: root.spacingPx
                        horizontal: root.horizontal
                        theme: _theme
                        config: _config
                        apps: _apps
                        relanzadores: _relanzadores
                    }
                }
            }
        }
    }

    // Script runners block: one ScriptRunnerWidget per visible script runner.
    Component {
        id: scriptRunnersComp
        Grid {
            columns: root.horizontal ? Math.max(1, root.visibleScriptRunnerIds.length) : 1
            spacing: root.spacingPx
            Repeater {
                model: root.visibleScriptRunnerIds
                Item {
                    required property int index
                    required property var modelData
                    property var _config: config
                    property var _theme: theme
                    property var _scriptRunners: scriptRunners
                    width: root.appIconPx
                    height: root.appIconPx
                    ScriptRunnerWidget {
                        anchors.centerIn: parent
                        scriptRunner: _scriptRunners.get(modelData)
                        iconSize: root.appIconPx
                        theme: _theme
                        scriptRunners: _scriptRunners
                        screenName: _config.screenName
                    }
                }
            }
        }
    }

    Component {
        id: springComp
        Item {
            // Expansion handled by the section wrapper's Layout.fill*.
            implicitWidth: 0
            implicitHeight: 0
        }
    }

    // Static separator: a fixed gap of config.separatorSize along the dock's
    // main axis, with the same thin line the apps block draws for its own
    // separators. Never grows the dock across its thickness: the cross-axis
    // size is that of a widget icon, which is already accounted for there.
    Component {
        id: staticSepComp
        Item {
            implicitWidth: root.horizontal ? config.separatorSize : root.widgetIconPx
            implicitHeight: root.horizontal ? root.widgetIconPx : config.separatorSize

            Rectangle {
                anchors.centerIn: parent
                width: root.horizontal ? 2 : parent.width * 0.6
                height: root.horizontal ? parent.height * 0.6 : 2
                radius: 1
                color: Qt.rgba(theme.foreground.r, theme.foreground.g,
                               theme.foreground.b, 0.25)
            }
        }
    }

    Component {
        id: overviewComp
        Item {
            property bool hovered: false
            implicitWidth: root.widgetIconPx
            implicitHeight: root.widgetIconPx
            Image {
                anchors.centerIn: parent
                width: root.widgetIconPx
                height: width
                source: "image://icon/view-grid" + root.widgetIconSuffix
                sourceSize: Qt.size(root.widgetIconPx * Screen.devicePixelRatio,
                                    root.widgetIconPx * Screen.devicePixelRatio)
                opacity: (overview && overview.active) ? 1.0 : 0.85
                scale: parent.hovered ? 1.12 : 1.0
                Behavior on scale { NumberAnimation { duration: 120 } }
            }
            Rectangle {
                visible: overview && overview.active
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.top: parent.bottom
                anchors.topMargin: config.compact ? 0 : 2
                width: parent.width * 0.5
                height: 3
                radius: 1.5
                color: theme.highlight
            }
        }
    }

    Component {
        id: moveDesktopComp
        Item {
            property bool hovered: false
            implicitWidth: root.widgetIconPx
            implicitHeight: root.widgetIconPx
            Image {
                anchors.centerIn: parent
                width: root.widgetIconPx
                height: width
                source: "image://icon/go-next" + root.widgetIconSuffix
                sourceSize: Qt.size(root.widgetIconPx * Screen.devicePixelRatio,
                                    root.widgetIconPx * Screen.devicePixelRatio)
                opacity: 0.85
                scale: parent.hovered ? 1.12 : 1.0
                Behavior on scale { NumberAnimation { duration: 120 } }
            }
        }
    }

    Component {
        id: moveScreenComp
        Item {
            property bool hovered: false
            implicitWidth: root.widgetIconPx
            implicitHeight: root.widgetIconPx
            Image {
                anchors.centerIn: parent
                width: root.widgetIconPx
                height: width
                source: "image://icon/video-display" + root.widgetIconSuffix
                sourceSize: Qt.size(root.widgetIconPx * Screen.devicePixelRatio,
                                    root.widgetIconPx * Screen.devicePixelRatio)
                opacity: 0.85
                scale: parent.hovered ? 1.12 : 1.0
                Behavior on scale { NumberAnimation { duration: 120 } }
            }
        }
    }

    Component {
        id: maxMinComp
        Item {
            property bool hovered: false
            implicitWidth: root.widgetIconPx
            implicitHeight: root.widgetIconPx
            Image {
                anchors.centerIn: parent
                width: root.widgetIconPx
                height: width
                // Static on purpose: "Window Maximize" is a toggle on KWin's
                // side and the dock has no view of the active window's
                // maximized state to mirror here.
                source: "image://icon/window-maximize" + root.widgetIconSuffix
                sourceSize: Qt.size(root.widgetIconPx * Screen.devicePixelRatio,
                                    root.widgetIconPx * Screen.devicePixelRatio)
                opacity: 0.85
                scale: parent.hovered ? 1.12 : 1.0
                Behavior on scale { NumberAnimation { duration: 120 } }
            }
        }
    }

    Component {
        id: closeWindowComp
        Item {
            property bool hovered: false
            implicitWidth: root.widgetIconPx
            implicitHeight: root.widgetIconPx
            Image {
                anchors.centerIn: parent
                width: root.widgetIconPx
                height: width
                source: "image://icon/window-close" + root.widgetIconSuffix
                sourceSize: Qt.size(root.widgetIconPx * Screen.devicePixelRatio,
                                    root.widgetIconPx * Screen.devicePixelRatio)
                opacity: 0.85
                scale: parent.hovered ? 1.12 : 1.0
                Behavior on scale { NumberAnimation { duration: 120 } }
            }
        }
    }

    Component {
        id: nextWallpaperComp
        Item {
            property bool hovered: false
            implicitWidth: root.widgetIconPx
            implicitHeight: root.widgetIconPx
            Image {
                anchors.centerIn: parent
                width: root.widgetIconPx
                height: width
                source: "image://icon/preferences-desktop-wallpaper" + root.widgetIconSuffix
                sourceSize: Qt.size(root.widgetIconPx * Screen.devicePixelRatio,
                                    root.widgetIconPx * Screen.devicePixelRatio)
                opacity: 0.85
                scale: parent.hovered ? 1.12 : 1.0
                Behavior on scale { NumberAnimation { duration: 120 } }
            }
        }
    }

    // Left click = normal, right click = dark. Deliberately not a toggle: each
    // button picks one mode, so the widget is also a readout of which one is on.
    Component {
        id: darkModeComp
        Item {
            property bool hovered: false
            implicitWidth: root.widgetIconPx
            implicitHeight: root.widgetIconPx
            Image {
                anchors.centerIn: parent
                width: root.widgetIconPx
                height: width
                source: "image://icon/"
                        + (config.darkModeActive ? "weather-clear-night" : "weather-clear")
                        + root.widgetIconSuffix
                sourceSize: Qt.size(root.widgetIconPx * Screen.devicePixelRatio,
                                    root.widgetIconPx * Screen.devicePixelRatio)
                opacity: 0.85
                scale: parent.hovered ? 1.12 : 1.0
                Behavior on scale { NumberAnimation { duration: 120 } }
            }
        }
    }

    Component {
        id: clock2Comp
        Item {
            property bool hovered: false
            // Grows with the font so a large size is not clipped.
            implicitWidth: Math.max(root.widgetIconPx, clock2Col.implicitWidth)
            implicitHeight: Math.max(root.widgetIconPx, clock2Col.implicitHeight)
            Column {
                id: clock2Col
                anchors.centerIn: parent
                spacing: 0
                Text {
                    id: clock2Time
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: clock2.timeString
                    font.pixelSize: config.clockFontSize > 0
                        ? root.clockFontPx
                        : Math.round(root.widgetIconPx * 0.455)
                    font.bold: true
                    color: root.dockTextColor
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    visible: config.clockShowDate
                    text: clock2.dateString
                    font.pixelSize: Math.round(clock2Time.font.pixelSize * 0.57)
                    font.bold: config.labelBold
                    color: root.dockTextColor
                    opacity: 0.75
                }
            }
            ToolTip {
                popupType: Popup.Window
                visible: config.showTooltips && parent.hovered
                delay: 400
                contentItem: Rectangle {
                    color: "#404040"
                    implicitWidth: 300
                    implicitHeight: 72
                    radius: 8
                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.left: parent.left
                        anchors.leftMargin: 16
                        anchors.right: parent.right
                        anchors.rightMargin: 16
                        spacing: 6
                        Text {
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                            text: clock2.popupTimeString
                            color: "#FFD700"
                            font.pixelSize: 22
                            font.bold: true
                        }
                        Text {
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                            text: clock2.popupDateString
                            color: "#FFFFFF"
                            font.pixelSize: 16
                        }
                    }
                }
            }
        }
    }
}
