# kdock — Agent Context

## Project Overview

kdock is a **standalone dock for Wayland desktops** (KDE Plasma 6, sway, hyprland, etc.) written **100% in Qt 6** with **zero KDE Frameworks / Plasma library dependencies**. All Wayland protocols are implemented directly from their XML definitions using `qtwaylandscanner`.

- Single binary, no external plugins.
- Build system: **CMake + Ninja**.
- **Multi-instance**: up to **3 docks per monitor** (`DockConfig::kMaxDocksPerScreen`). Each dock is identified by a **dockId** — slot 0 is the bare screen name (`kdock-<screen>.conf`, backward compatible), extra slots append `#<slot>` (`kdock-<screen>-<slot>.conf`). See `DockConfig::makeDockId/screenOfDockId/slotOfDockId`. Global data (the enabled-dock list `enabledScreens` — key name kept, now holds dockIds; the relanzadores) lives in the shared `~/.local/share/kdock/kdock.conf` (`DockConfig::settingsFilePath()`, which also migrates a legacy `~/.config/kdock/kdock.conf`). See **DockManager** below.

## Architecture

```
src/
  main.cpp                — Entry point; sets QT_WAYLAND_SHELL_INTEGRATION, builds shared singletons + DockManager
  dockmanager.{h,cpp}     — Owns one dock instance per enabled+connected monitor; hotplug, per-monitor configs
  layershell.{h,cpp}      — In-tree wlr-layer-shell-v1 Qt Wayland shell integration (static plugin)
  windowmonitor.{h,cpp}   — Backend factory; AbstractWindow base class. Lo que un solo backend sabe hacer se agrega como par "consulta de capacidad + no-op por defecto" (canChangeDesktop()/moveToDesktop(), hoy solo en PlasmaWindow), no como virtual pura: así el backend wlroots compila y el widget que la usa se apaga solo
  taskmanager.{h,cpp}     — wlroots backend: zwlr_foreign_toplevel_management_v1
  plasmawindowmonitor.{h,cpp} — KWin backend: org_kde_plasma_window_management
  desktopentry.{h,cpp}    — XDG .desktop file indexer and launcher (no KDE code)
  dockmodel.{h,cpp}       — QAbstractListModel merging pinned launchers + running windows + separators
  dockconfig.{h,cpp}      — QSettings-backed persistent config, exposed to QML
  appmenu.{h,cpp}          — XDG application menu backend (Kickoff-like): categories, search, favorites, .menu submenus
  xdgmenutree.{h,cpp}      — Pragmatic reader of the XDG .menu files: recovers the submenus kmenuedit and the browsers create
  theme.{h,cpp}           — KDE color scheme parser from ~/.config/kdeglobals with live-reload
  iconprovider.{h,cpp}    — QQuickImageProvider exposing XDG themed icons as image://icon/<name>
  iconcolorprovider.{h,cpp} — Dominant-color extractor for icons (QML "iconColors"); tints the running-app background
  dockwindow.{h,cpp}      — QQuickView layer-shell surface; maps DockConfig to layer-shell props
  settingsdialog.{h,cpp}  — Qt Widgets configuration dialog
  volumecontrol.{h,cpp}   — PipeWire volume via wpctl/pactl (no libpulse linkage); tiny default-sink indicator for the dock widget
  audiocontrol.{h,cpp}    — Full mixer backend for Settings → Audio (outputs/inputs/per-app streams via pactl); separate from VolumeControl
  batterycontrol.{h,cpp}  — Battery level/state via UPower + power profiles via power-profiles-daemon (system bus)
  diskscontrol.{h,cpp}    — Removable drives via UDisks2 (mount/unmount/eject/open); token disks
  nmdbus.{h,cpp}          — Shared NetworkManager D-Bus plumbing (constants, property reads, settings/secrets, AP security, metatypes)
  networkcontrol.{h,cpp}  — Connections, nearby APs and Wi-Fi state via NetworkManager; token network
  networksettings.{h,cpp} — NM connection CRUD + ipv4/ipv6 <-> IpConfig conversions for the Redes tab (no GUI)
  networksettingswidget.{h,cpp} — The Redes tab: devices/connections list + editor stack
  connectioneditor.{h,cpp}      — The connection form (General/Wi-Fi/Seguridad/Ethernet/IPv4/IPv6)
  routesdialog.{h,cpp}          — Static routes table for one IP family
  clipboardhistory.{h,cpp} — Clipboard history singleton, text + images (JSON index + PNGs in the XDG data dir)
  waylandclipboard.{h,cpp} — ext-data-control-v1 client: focus-independent clipboard read/write
  clockwidget.{h,cpp}     — Live clock with configurable 12/24h, date, seconds
  clockwidget2.{h,cpp}    — Second clock (same display) with a larger styled tooltip
  brightnesscontrol.{h,cpp} — Screen brightness via brightnessctl (no extra libraries)
  overviewcontrol.{h,cpp} — Toggles KWin's Overview effect via kglobalaccel (KDE only)
  desktopcontrol.{h,cpp}  — Moves active window to next virtual desktop via KWin shortcut (KDE only)
  monitorcontrol.{h,cpp}  — Moves active window to next monitor via KWin shortcut; right click = previous monitor (token movetoscreen)
  maxmincontrol.{h,cpp}   — Maximizes (left click) / minimizes (right click) the active window via KWin shortcuts (token maxmin, KDE only)
  activewindowcontrol.{h,cpp} — Closes the active window (left click) or pushes it to the next virtual desktop without following it (right click); token closewindow
  kwinshortcut.{h,cpp}    — Shared helper for the KDE-only widgets: sessionIsKde() + invoke(<KWin global shortcut name>)
  wallpapercontrol.{h,cpp} — Advances the KDE slideshow wallpaper to the next image on a specific monitor (token nextwallpaper)
  desktopwallpapers.{h,cpp} — A different wallpaper per virtual desktop: snapshots KDE's own config for desktop 1 and, on desktops 2..kMaxDesktops, applies per monitor either a static image or KDE's own slideshow rooted at a configurable folder (no widget; Settings -> Wallpapers)
  plasmascript.h          — Header-only helpers over org.kde.PlasmaShell.evaluateScript (escapeJs / evaluate / run / evaluateBlocking), shared by the two above
  powercontrol.{h,cpp}     — Session/power actions via KDE D-Bus (logout/reboot/shutdown/lock/suspend); tokens session + app-menu footer
  relanzadorconfig.{h,cpp}    — Config for one Relanzador (nested mini-dock); see relanzador.md
  relanzadormodel.{h,cpp}     — Model of the apps inside one Relanzador
  relanzadoresmanager.{h,cpp} — Global collection of Relanzadores (token relanzadores)
  scriptrunnerconfig.{h,cpp}  — Config for one Script Runner (id, title, iconName, script)
  scriptrunnersmanager.{h,cpp} — Global collection of Script Runners; run(id) executes the script via `sh -c` (token scriptrunners)
  iconpickerdialog.{h,cpp}    — Self-contained Qt icon picker (lists current theme icons; no KDE deps)
  coloredtabbar.{h,cpp}       — QTabBar painting a per-tab tint, so the settings dialog's ten tabs are told apart by color
  configarchive.{h,cpp}       — Export/import the whole config to/from a .zip (QtCore private QZip)
  systray.{h,cpp}         — DBus StatusNotifierItem host + watcher (SNI protocol, org.kde.* names)
  systrayimageprovider.{h,cpp} — image://systray/<service> provider for pixmap-only tray icons
  systraymodel.{h,cpp}    — QAbstractListModel filtering visible systray icons
  tilemenulauncher.{h,cpp} — kdock's whole side of the full-screen tile menu: find the
                            binary, toggle it over D-Bus, open its panel (token tilemenu)
qml/
  SystrayMenu.qml         — A tray item's own menu, built from the DBusMenu tree (recursive, created at runtime)
  Dock.qml                — Main UI: section-based layout (GridLayout driven by config.widgetOrder) rendering the apps block + widgets (volume, brightness, clock, clock2, overview, movetodesktop, movetoscreen, maxmin, closewindow, nextwallpaper, darkmode, autohide, showdesktop, systray, relanzadores, scriptrunners, session, settings) + separators (springs and static gaps) + app menu button; per-icon and per-section drag & drop, context menus, autohide
  AppMenuPopup.qml        — XDG application menu popup (Kickoff-like): search field, category sidebar, favorites, power/session footer row
  RelanzadorWidget.qml    — Icon widget for a Relanzador in the main dock
  RelanzadorPopup.qml     — Popup layout for a Relanzador
  ScriptRunnerWidget.qml  — Icon widget for a Script Runner (click → runs its sh script)
  IconMenuItem.qml        — MenuItem rendering its icon via image://icon (works in Popup.Window menus on Wayland)
  SubMenuDelegate.qml     — The row that opens a submenu, with an icon (a menu's `delegate`; see Submenu icons)
  ClipboardPopup.qml      — Clipboard history popup (search field + entry list)
  DisksPopup.qml          — Removable-drives popup (mount/unmount/eject/open)
  NetworkPopup.qml        — Connections popup (activate/deactivate, Wi-Fi toggle)
  BackgroundColorMenu.qml — Reusable right-click submenu: 4 panel color presets + "theme default"
  IconLabelMenu.qml       — Reusable right-click submenu: app-icon label mode (see App-icon labels)
  WidgetLabelMenu.qml     — Reusable right-click submenu: widget/section label mode (see Section labels)
protocols/
  *.xml                   — Vendored Wayland protocols (layer-shell, foreign-toplevel, plasma-window, xdg-shell)
```

## Key Technical Details

### Layer-Shell Integration (`src/layershell.cpp`)
- Implemented as a **static Qt Wayland shell integration plugin** (`KDockLayerShellPlugin`).
- Windows opt-in via the dynamic property `kdock.layershell = true` before platform window creation.
- Dynamic properties drive live updates:
  - `kdock.anchors` (uint bitmask: top=1, bottom=2, left=4, right=8)
  - `kdock.exclusiveZone` (int — reserved screen space)
  - `kdock.exclusiveEdge` (uint, protocol anchor value of the dock's edge) — **needed because KWin only derives the exclusive edge from the anchor for a single edge or a whole side** (`LayerSurfaceV1Interface::exclusiveEdge()` in `src/wayland/layershell_v1.cpp`): a surface anchored to an edge plus ONE corner gets `Qt::Edge()` → **no strut at all** → maximized windows pass under the dock. kdock sends the protocol v5 request `set_exclusive_edge` (vendored XML is now v5; the shell integration binds v5) whenever the compositor supports it, and `applyLayerProperties()` anchors the **full side** for any dock spanning the whole edge (`panelMode` with `dockLength==0`, or `dockLength==100` — the corner was meaningless there and its margin pushed the surface off-screen). Partial docks (0<`dockLength`<100 with start/end alignment) keep the corner anchor and rely on `set_exclusive_edge`; on compositors without v5 (sway) those keep today's no-strut behavior. Verified against KWin 6.6.6 source; the user's live symptom was a top dock with `dockLength=100`+alignment End letting maximized windows run under it (2026-08-08).
  - `kdock.layer` (uint — 0=background, 1=bottom, 2=top, 3=overlay)
  - `kdock.margins` (QMargins)
  - `kdock.keyboardInteractivity` (uint — 0=none, 1=exclusive, 2=on-demand)
- Popups (menus, tooltips) are delegated to the stock **xdg-shell** integration via `attachPopup()`.
- The `wl_output` for layer-surface creation is resolved from `QWindow::screen()->handle()` (cast to `QWaylandScreen*`) to correctly track `setScreen()` calls. Falls back to `QWaylandWindow::waylandScreen()` if the cast fails.
- Uses **QtWaylandClient private API** (`QWaylandShellIntegrationTemplate`, `QWaylandShellSurface`, `QWaylandWindow`). Breakage possible on Qt major version bumps.
- **Env-var leak to launched apps (important):** `main.cpp` sets `QT_WAYLAND_SHELL_INTEGRATION=kdock-layershell` via `qputenv` before `QApplication`. Since `kdock` is a launcher (`DesktopEntryIndex::launch` → `QProcess::startDetached`, plus other `QProcess`/`QProcessEnvironment::systemEnvironment()` sites), any child would inherit that value and **abort** with `No shell integration named "kdock-layershell" found` (the plugin is static, in-binary, installed nowhere). Fix: `main.cpp` calls `qunsetenv("QT_WAYLAND_SHELL_INTEGRATION")` **immediately after** the `QApplication` ctor — Qt reads the var only once at platform init, so the dock keeps working and every spawned child gets a clean env. Do **not** move the qputenv/qunsetenv logic or set the var for child processes.

### Window Monitoring Backends (`src/windowmonitor.cpp`)
- Runtime factory picks the first available protocol:
  1. `zwlr_foreign_toplevel_manager_v1` (wlroots compositors)
  2. `org_kde_plasma_window_management` (KWin / Plasma 6)
- KWin treats `org_kde_plasma_window_management` as **privileged**; the installed `.desktop` file must declare:
  ```ini
  X-KDE-Wayland-Interfaces=org_kde_plasma_window_management
  ```
  `kdock.desktop` already includes this. For development runs from `build/`, copy the `.desktop` to `~/.local/share/applications/` and adjust `Exec=` to the absolute binary path.

### Dock Model (`src/dockmodel.cpp`)
- `QAbstractListModel` roles: `name`, `iconName`, `pinned`, `windowCount`, `active`, `minimized`.
- Pinned items form a block at the front; drag-and-drop reordering is **not allowed** to cross the pinned/unpinned boundary.
- Window grouping key: lowercase desktop file id (or `app_id` fallback). Matching heuristics:
  1. Exact `app_id` match against `.desktop` file id.
  2. `StartupWMClass` match.
  3. Last segment of reverse-DNS (`org.kde.dolphin` → `dolphin`).
  4. Reverse segment search (`dolphin` → `org.kde.dolphin`).
  5. **Chromium web apps (PWAs)** — `DesktopEntryIndex::pwaEntry()`. KWin reports their `app_id` with an **extra underscore** before the 32-char extension id (`msedge-_<extid>-Default`) while the `.desktop` file has none, so 1-4 all miss and the window lands in its own dock row instead of grouping under its launcher. Matches on the extension id, which is the real identity. Runs **only after 1-4 fail**, so it can never alter an existing match.
     - Candidates: `StartupWMClass` in both conventions (`crx_<extid>` for Chrome/Brave/Vivaldi, `crx__<extid>` for Edge), plus any id embedding the extension id.
     - **Deterministic pick** (a single web app is often installed under several browsers, and Edge writes a second `NoDisplay` copy of each entry): same browser as the `app_id` first, then a visible entry over a `NoDisplay` one, then the lowest id. Returning the first hash hit instead would group an Edge window under the Brave launcher and could change between runs.
     - Do **not** add the "PID walk" (`--app-id` from `/proc/<pid>/cmdline`) that an earlier attempt used: every Edge window shares one PID and that cmdline only holds the *first* web app's id, so it overwrites correct entries with wrong ones. See `fix-dock-2026-07-28.md`.
  6. **Chromium install-directory fallback** — `DesktopEntryIndex::installDirEntry()`. A **freshly opened** browser window reaches kdock as `msedge`, not `microsoft-edge`: Chromium falls back to the name of the *install directory* of the real binary (`/opt/microsoft/msedge/microsoft-edge`), and KWin never sends a corrected `app_id` afterwards (measured, 2026-07-29). No `.desktop` declares that name, so 1-5 all miss, the window gets a stray icon, and the pinned launcher — still looking empty — starts **another instance on the next click**. This heuristic maps a directory name to the entry whose executable resolves (symlinks included: `/usr/bin/microsoft-edge-stable` → `/opt/.../msedge/...`) into a directory of that name. Generalizes to `brave`, `chrome`, `vivaldi`, `chromium` with no hardcoded table.
     - The map is built **lazily on first miss** (it needs `canonicalFilePath()` per entry) and cleared by `reload()`. Generic directories (`bin`, `usr`, `opt`, …) are skipped, or `/usr/bin/*` apps would all collide on `bin`.
     - Among entries sharing a directory the **browser wins over its own web apps** (they share the binary but pass `--app-id=`/`StartupWMClass=crx_*`), then visible over `NoDisplay`, then the lowest id.
- **Row keys and pinning (`togglePinned`)** — a row's `key` is the app key (`entry.id` lowercased) *except* in ungrouped mode (`groupWindows=false`), where every window past the first is keyed `win:<pointer>`. Pinning such a row from the context menu updates `pinned` **without a rebuild** (`m_updatingPinned` suppresses it), so `togglePinned()` must also **re-key the row to the app**. Otherwise the row becomes an orphan launcher as soon as its window closes (it survives because it is pinned, but no `keyForWindow()` can ever match it): clicking it finds no windows and starts a new instance *every time*, and each new window adds yet another icon. Symptom: two icons with the same name and icon, the first with no running-indicator. A dock restart hides it — `rebuild()` rebuilds the keys from the config — which is why it only shows on docks where an app was pinned by right-click since the last restart.
- `syncWindows()` (Q_INVOKABLE) iterates `monitor->windows` and calls `placeWindow()` for any window not yet in the model. Called 200 ms after construction to catch windows missed during the initial `rebuild()` (Wayland events still queued at that point).

### App-icon labels (`config.iconLabelMode` + `appsComp` in `Dock.qml`)
- Six modes (`DockConfig::IconLabelMode`): **icon only** (0, default — pixel-identical to the pre-label dock), **name below** (1), **name at the right** (2), **name only** (3), **name above** (4), **name at the left** (5). The values are persisted, hence the non-obvious order: 4 and 5 were added later. Scope: the `apps` block only (launchers + running windows); every other section uses `widgetLabelMode` (see below).
- **The icon never changes size**: the delegate wraps the icon in a fixed `iconBox` of `config.iconSize` and lays the label out *around* it; only the cell (`cellW`/`cellH`) grows. The running-app indicators (color background, edge line, dots) anchor to the **cell**, so they stay on the screen-edge side instead of colliding with a label drawn under the icon.
- Label width: `iconLabelWidth` is a **cap, not a size**. On a horizontal dock each cell shrinks to its own name and elides at the cap. On a vertical dock every cell shares one box width (so the thickness does not jump as windows open/close), and that shared width is the **widest name actually drawn**, not the cap — see the next bullet. The natural width comes from a `TextMetrics`, not from the `Text`'s own `implicitWidth` (that would feed its `width` binding back into itself).
- **Widest-name measurement** (`config.effectiveLabelWidth`, fix 2026-07-30): using the cap as the box width on a vertical dock left a band of dead dock to the right of the longest name (`44 + 2 + 110 + 12 = 168` px of thickness for names ~80 px wide; mirrored with *name at the left* on a right-edge dock). Now `Dock.qml` measures every name it draws off-screen — iterating `dockRepeater`/`sectionRepeater` with two probe `TextMetrics` (bold for app names, the active app's weight, so moving the focus never reflows the dock; regular for section names), at the **unscaled** font since `labelBoxWidth` applies `fitScale` — and reports the maximum through `config.setMeasuredLabelWidth()`. `DockConfig::effectiveLabelWidth()` = `min(measured, iconLabelWidth)` and is what `cellThicknessFor()` uses, so the drawn box and the exclusive zone come from one number. Reported `0` (labels off, or dropped by the auto-shrink) falls back to the cap = the historic behaviour. The measurement is debounced (16 ms `Timer`) and re-run from: both Repeaters' `onCountChanged`, the app delegate's `onNameChanged`, the section delegate's `onLabelChanged`/`onVisibleChanged`, `labelVisible`/`widgetLabelVisible`, and `config.iconSizeChanged`/`iconLabelFontSizeChanged` — the two inputs of `iconLabelFontPx`. **It is deliberately not hooked to `dockThicknessChanged`**: `setMeasuredLabelWidth()` emits that, so it would feed straight back in.
- Configurable from **Settings → Widgets** ("App name" + width + font size) *and* from the right-click submenu `qml/IconLabelMenu.qml`, present in both the app-icon menu and the widget-section menu (same dual use as `BackgroundColorMenu.qml`). The Settings combo is **two-way synced** because the context menu can change the mode while the dialog is open.
- **Nombres en dos renglones** (`config.labelLines`, 1 o 2, default 1, 2026-08-07): con 2, un nombre más largo que la caja **se envuelve** en vez de elidirse, así se puede leer entero ("Nombre Ficticio de Aplicación"). Vale para los nombres de apps *y* de widgets, como el ancho y el tamaño de fuente, y la caja pasa a medir dos alturas de línea — o sea que **mueve el grosor del dock**: la fórmula es `DockConfig::iconLabelBoxHeight()` (= `iconLabelLineHeight() * labelLines`), que es lo que usa `cellThicknessFor()` en las tres ramas horizontales, y el setter emite `dockThicknessChanged()`. En QML el espejo es `root.labelBoxHeight`, que reemplazó a `labelLineHeight` en todos los `cellH`/`implicitHeight`/`y` de las etiquetas; los dos `Text` llevan `wrapMode: WrapAtWordBoundaryOrAnywhere` (no `Wrap`: un nombre de una sola palabra larga —o cualquier nombre CJK— no tiene dónde cortar y el segundo renglón quedaría vacío) y `maximumLineCount: labelLines`, con el `elide` intacto para lo que no entre ni en dos líneas.
  El **ancho no cambia** con la opción, así que la medición del nombre más ancho (`effectiveLabelWidth`) sigue igual; lo único que se le suma es `root.labelSlack`, 1 px que solo existe con envoltura: una caja exactamente del ancho medido igual hace que Qt tire la última letra al segundo renglón ("Clock" dibujado como "Cloc/k"). Medido bajo Xvfb: 87 → 102 px de grosor al prenderla.

### Section labels and renaming (`config.widgetLabelMode` / `widgetName()`)
- Same idea for every section that is **not** an app (widgets *and* blocks: volume, clock, battery, systray, relanzadores, script runners, menu, session…), but a **separate setting** so a dock can name its widgets and not its apps or the other way round. Same enum minus `LabelOnly`, which `sanitizedWidgetLabelMode()` (anonymous namespace in `dockconfig.cpp`) folds back to `IconOnly`: a widget without its icon is unrecognizable. Springs and the `apps` block are excluded (the latter labels each of its own icons).
- Width and font size are **shared** with the app labels (`iconLabelWidth` / `iconLabelFontSize`); only the position is independent.
- Geometry: `DockConfig::cellThicknessFor(mode, iconPx)` is the single formula, used by `appCellThickness()` (icon size) and `widgetCellThickness()` (`max(iconSize, widgetIconSize)` — blocks draw taller content than a scaled widget icon). `dockThickness()` takes the max of both, so a widget label can never leave the exclusive zone short. In `Dock.qml` the section delegate mirrors the app delegate: `TextMetrics` for the natural width, `contentLoader` positioned around the label instead of `anchors.centerIn`.
- **Renaming** lives in `widgetNames/<token>` inside the dock's `.conf` (`m_widgetNames` + `widgetName()` / `setWidgetName()`); an empty name, or the default one, deletes the key. `DockConfig::defaultWidgetLabel()` is the single source of truth for the section names — `SettingsDialog::sectionLabel()` just forwards to it. QML re-evaluates a rename through `config.widgetNamesRevision`, read inside `root.widgetNameOf()` (same trick as `theme.revision` for icon URLs).
- **Trampa (mordió 2026-07-29)**: el `Loader` de la sección se centra con `(secVisual.width - secVisual.contentW) / 2`, **nunca** leyendo su propio `width`/`height`. Un `Loader` dimensiona su item desde su propio tamaño, así que leerlo ahí cierra el ciclo `secVisual.width → implicitWidth → item implicit → Loader size`: Qt avisa *"Binding loop detected for property x"* y el widget queda dibujado en una posición vieja (íconos superpuestos, `bug-horizontal.jpg` / `bug-vertical.jpg`). Cambiar el modo de etiqueta forzaba una reevaluación y "arreglaba" el síntoma, que es lo que despistaba.
- UI: mode from **Settings → Widgets** ("Widget name", two-way synced) or the right-click submenu `qml/WidgetLabelMenu.qml` (instantiated next to `IconLabelMenu` in both the app-icon and the widget-section menus); renaming from **Settings → Layout** ("Rename..." button, disabled for springs), where the list shows `renamed (default)`.

### Selectores de apariencia de KDE (`iconthemes` / `colorschemes`, `src/appearancecontrol.cpp`)
- Dos widgets que listan lo instalado y lo aplican al escritorio entero: **iconset** y **esquema de color**. Un solo backend (`AppearanceControl`, context property `appearance`) para los dos, porque el ctor de `DockWindow` ya recibe 18 objetos y ambos hacen lo mismo.
- **Listar leyendo archivos, no CLI**: la salida de `plasma-apply-colorscheme --list-schemes` está **traducida** (en es_AR arranca con "Su sistema tiene los siguientes esquemas de color:"), así que parsearla se rompe según el locale. Los iconsets salen de `Theme::availableIconThemes()` (ya filtra temas de cursor y `Hidden=true`); los esquemas, de `<data dirs>/color-schemes/*.colors` leídos con `QSettings` — de ahí salen también los tres colores del preview (`Colors:Window/Background|Foreground`, `Colors:Selection/Background`). El id de un esquema es el **basename del archivo**, que es lo que espera la herramienta.
- **Aplicar sí usa las herramientas de Plasma** (son las que avisan a las apps abiertas). `plasma-apply-colorscheme` está en el PATH, pero **`plasma-changeicons` no**: es un helper de `libexec` cuyo directorio cambia según la distro, así que `findTool()` prueba una lista de rutas y, si no aparece, cae a `kwriteconfig6` (el dock se entera igual porque `Theme` vigila `kdeglobals`; las apps ya abiertas, no).
- Escala: ~122 iconsets y ~445 esquemas en esta máquina ⇒ el popup **necesita buscador**. `qml/ThemeListPopup.qml` es uno solo con `mode: "icons"|"colors"`, con el patrón de foco de teclado de `AppMenuPopup` (`modal: true` + `popupType: Popup.Window` + `setKeyboardInteractive`). El parseo de los `.colors` se cachea y `refreshIfStale()` (stat de los directorios) lo revalida al abrir.
- El estado actual sale de `kdeglobals`: `[Icons] Theme` y `[General] ColorScheme`. **La segunda se lee como `value("ColorScheme")`, sin el grupo**: `QSettings` mapea la sección `[General]` de un INI al nivel raíz, y el path con grupo devuelve vacío sin avisar (fue exactamente el bug del "(sin definir)" eterno, 2026-08-05; lo mismo vale para el `Name=` de los `.colors`). Puede además no existir de verdad (escritorios cuyo esquema vino de un look-and-feel): entonces ninguna fila lleva el check, y eso sí no es un bug.
- Ambos son **bloques** (`isBlock()`), como los otros widgets con popup: manejan su propio mouse. Si no, el `Binding` de `hovered` de la sección avisa *"Property 'hovered' does not exist"* y el clic se lo lleva el `MouseArea` de la sección.
- Deliberadamente **no tocan el override de iconset propio de kdock** (`Theme::setIconTheme`): si está puesto, el dock conserva sus íconos y solo cambia el resto del escritorio. Dicho en el tooltip del checkbox de Ajustes.
- UI: Settings → Widgets → "Icon theme picker" / "Color scheme picker" (ambos apagados por defecto).

#### Favoritos (2026-08-05)
- Un **checkbox por fila** en las cuatro superficies (los dos widgets del dock y los dos botones de Colores). Lo tildado sube al tope de la lista; destildarlo lo devuelve a su lugar alfabético. Hay **una lista por tipo para todo el proceso**, así que un favorito marcado en un dock aparece en los demás y en el diálogo.
- Viven en `AppearanceControl` —que ya era la única instancia que ven las cuatro superficies—, no en `DockConfig`: así no hacen falta ni claves por dock ni sincronización entre solapas. Persisten en el `kdock.conf` **compartido** (`DockConfig::settingsFilePath()`), grupo `[Appearance]`, claves `favoriteIconThemes` / `favoriteColorSchemes` (lista de ids ordenada, porque el archivo se edita a mano). Un id de un tema desinstalado se ignora al listar pero **no se borra**: reinstalarlo recupera la marca.
- `iconThemes()` y `colorSchemes()` agregan la clave `fav` y devuelven **favoritos primero, alfabético dentro de cada bloque** (`sortedByFavorite()`), así que ninguna superficie repite el orden. `colorSchemes()` parchea `current`+`fav` sobre una **copia** del caché: reordenar el caché en su lugar haría que el orden dependiera de cuándo fue la última llamada.
- `setFavorite()` emite `favoritesChanged()` (distinta de `changed()`, que significa "se aplicó un tema"). **El popup que hizo el cambio no se reordena**: la fila saltaría de debajo del cursor que la acaba de tildar. En QML eso es la property `freezeOrder` alrededor del `setFavorite()`; en Qt Widgets, sencillamente no repoblar. El nuevo orden se ve al reabrir — y al instante en las *otras* superficies, que sí escuchan la señal.
- **"Mantener abierta"** (checkbox arriba a la derecha, junto al contador): con la opción puesta, elegir no cierra el popup, para probar varios temas seguidos. Es un ajuste **compartido** más, `AppearanceControl::keepPickerOpen()` en `[Appearance] keepPickerOpen`, así que vale para las cuatro superficies y sobrevive a la sesión. En Qt hay que **repoblar preservando `verticalScrollBar()->value()`**: sin eso la lista salta al tope al elegir y se lee como si el popup se hubiera reiniciado. En QML alcanza el `refreshTick++` de `onChanged` (el `ListView` conserva su `contentY` porque el modelo no cambia de tamaño).
- En `ThemeListPopup.qml` el `CheckBox` va dentro del `Row`, y **el `Row` lleva `z: 1`**: el `MouseArea` de la fila se declara último, así que sin eso se comería el clic del checkbox y tildar un favorito además le cambiaría el iconset a todo el escritorio. Los `Text`/`Image` no aceptan eventos, así que un clic en cualquier otro punto sigue cayendo en el `MouseArea`.

### El selector de temas del diálogo (`src/themepicker.{h,cpp}`, 2026-08-05)
- `ThemePickerButton` + `ThemePickerPopup` son el gemelo Qt Widgets de `ThemeListPopup.qml`, y son **el único selector de tema del diálogo**: reemplazan a los 20 `QComboBox` que había sobre las mismas listas (con 183 iconsets y 456 esquemas, un combo plano era inusable). Mismo layout que el popup del dock (buscador, contador, preview, tilde en el elegido, checkbox de favorito), misma lista y mismos favoritos.
- **Dos modos.** `ApplyToDesktop` aplica a KDE al instante (los dos "· Apply …" de Colores). `PickValue` solo reporta el id con `picked(QString)` y el llamador lo guarda; ahí el estado es el `m_currentId` del botón, no lo que tenga KDE puesto, y es lo que lleva el tilde. Los usan: el override de iconset del dock (`Theme::setIconTheme`, en General y Colores), los dos sets de íconos de widgets (`DockConfig::setWidgetIconTheme{Light,Dark}Bg`) y las seis filas de DarkMode.
- `setSpecialEntry()` es la fila sin preview ni favorito que encabeza la lista con **id vacío**: "(System default)" en el iconset del dock, "(no cambiar)" en las dos filas de sistema de DarkMode y "(seguir el del sistema)" en la del iconset del dock. En `PickValue` un id vacío es una elección legítima, así que `applyEntry()` no corta con el early-return que sí tiene `ApplyToDesktop`.
- **`setCurrentId()` es silencioso por contrato**: es lo que llama la copia sincronizada de la otra solapa, y emitir haría ping-pong. Por eso `addDarkAppearanceExtrasRow()` ya no necesita `QSignalBlocker` para los selectores (sí para su checkbox).
- Helpers de `SettingsDialog` para que General y Colores no dupliquen nada: `makeDockIconThemePicker()` y `makeWidgetIconSetPicker(parent, darkBg)`, cada uno con sus dos `connect` (escribir + re-leer en la señal que corresponde).
- El preview del **botón** se resuelve en el primer `paintEvent`, no en `refresh()`: el diálogo construye 20 pickers y cada preview cuesta ~12 ms, casi todos en solapas que el usuario quizá no abra. Y se dibuja con opacidad 0,4 cuando el botón está deshabilitado (las filas de DarkMode con su casilla apagada), porque el estilo solo atenúa el texto y un preview a todo color al lado se lee como control activo.
- El popup es un `QFrame(Qt::Popup)`, **no un `QMenu`** (que no hospeda un `QLineEdit` usable): se cierra solo al clic afuera, y `keyPressEvent` agrega Escape. Enter en el buscador aplica la primera coincidencia.
- Cada fila es un widget puesto con `setItemWidget()`, con `Qt::WA_TransparentForMouseEvents` en el contenedor y en las etiquetas: así el clic llega a `QListWidget::itemClicked` (que aplica el tema) y **solo el checkbox** se queda con el suyo. Es el equivalente del `z: 1` del QML.
- **Las filas se construyen perezosamente** (`buildVisibleRows()`, disparado al mostrar, al hacer scroll y al redimensionar el viewport). Medido: resolver los previews de los 123 iconsets cuesta **1,5 s** (~4 ms por ícono, y el caché de rutas no lo evita), o sea que poblar de golpe congelaba el diálogo. Con solo lo visible son ~30 resoluciones.
- El preview de iconset sale de `IconProvider::resolveInTheme()` (estática, sin motor QML): resuelve **por archivo** dentro de la cadena `Inherits` del tema. Nunca `QIcon::setThemeName()`, que es estado global del proceso — ver la trampa en `CLAUDE.md`. Los pixmaps se cachean por (modo, id, dpr).
- El botón se pinta a mano (`paintEvent`) en vez de usar un `QToolButton`: hace falta preview + nombre elidido a la izquierda + flecha de dropdown a la derecha, y un QToolButton centra el conjunto. Cuando `kdeglobals` no trae ninguna clave `ColorScheme` el botón dice **"(sin definir)"** en vez de mentir con el primer ítem, que es lo que hacía el combo viejo. Ojo con esa leyenda: se veía **siempre**, porque la clave se leía como `General/ColorScheme` y `QSettings` mapea la sección `[General]` al nivel raíz — ver la trampa en `CLAUDE.md` (arreglado 2026-08-05).

### Separador transparente (token `gap`, 2026-08-07)

Un tercer separador repetible, además de `spring` (dinámico) y `sep` (estático): **se estira
como un spring y además corta el fondo del dock**, así que el escritorio se ve por el medio y
el dock se lee como dos.

- **`DockConfig::isSpringToken()`** es nuevo: `gap` expande igual que `spring`, y varios
  lugares tienen que preguntar por los dos (`root.hasSpring` en `Dock.qml`, que es lo que
  enciende `fillMain`). En el delegate, `sec.isSpring` es `spring || gap` — o sea que todo el
  layout, el arrastre y la exclusión de etiquetas siguen valiendo sin tocarlos.
- **El fondo dejó de ser un `Rectangle`**: ahora es un `Repeater` sobre `root.gapRuns`, un
  rectángulo por *tramo* entre huecos. Sin ningún `gap` hay exactamente un tramo y el dibujo es
  el de siempre (radio 0 y sin borde en modo panel); con uno o más, **cada tramo se redondea
  por su cuenta**, así que quedan dos píldoras y no un panel mordido. La imagen de fondo
  opcional se dibuja por tramo (el mosaico reinicia en cada uno).
- **Un tramo vacío no se dibuja** (2026-08-07): si entre dos huecos —o entre un hueco y el
  borde— no hay ninguna sección *visible*, el tramo se descarta. Sin eso, un separador puesto
  al lado de un widget apagado (el menú de aplicaciones, por ejemplo) dejaba una astilla de
  panel pegada al borde de la pantalla, con solo el padding adentro, y se leía como *"el dock
  perdió su punta izquierda"*. `computeGapRuns()` junta también los rectángulos de las
  secciones visibles y se queda con los tramos que alguna interseca.
- **La región de entrada es el complemento de lo pintado**, no la lista de huecos: un tramo
  descartado también es transparente, así que también tiene que dejar pasar los clics.
- **`root.computeGapRuns()`** saca la geometría iterando `sectionRepeater.itemAt(i)` y
  mapeando con `mapToItem(slider, …)`: los ids de adentro del delegate no se ven desde la raíz
  (misma razón por la que existe `labelStrings()`). Calcula el **complemento** de los huecos y
  lo publica en `gapRuns`; está debounced con un `Timer` de 16 ms y se re-dispara desde
  `onWidthChanged`/`onHeightChanged` y las señales de `config` que mueven secciones
  (`widgetOrder`, `panelMode`, `alignment`, `dockLength`, `edge`, `spacing`, `dockThickness`).
- **El hueco también es hueco para el mouse**: la misma función manda los rectángulos a
  `DockWindow::setGapRects()`, que los resta de la región de entrada (`applyHiddenMask()`, el
  mismo `setMask()` que usa el auto-ocultado). Sin eso el hueco se vería pero se comería los
  clics. Sin ningún `gap` la máscara sigue siendo `QRegion()` vacía — el camino rápido de antes.
- **Lo que el hueco no libera es la zona exclusiva**: es un solo número por borde para toda la
  superficie, así que las ventanas maximizadas siguen frenando en el grosor del dock a lo
  largo de todo el borde. Es una limitación de layer-shell, no algo por arreglar.
- **Solo tiene lugar donde un spring lo tiene**: en modo panel o con `dockLength > 0`. Con el
  largo automático y sin panel, el dock se ajusta a su contenido y el hueco mide 0 px.
- UI: botón *Agregar separador transparente* en la solapa Diseño y en el menú contextual de
  cualquier sección, al lado de los otros dos.

### Auto-shrink de íconos (`config.autoShrinkIcons` + bloque "Auto-shrink" en `Dock.qml`)
- El dock tiene largo fijo (el borde de pantalla en panel mode, `dockLength%` si no) pero su contenido no: con suficientes apps, widgets o etiquetas las secciones dejan de entrar. QtQuick Layouts **no recorta**: dibuja las secciones sobrantes encima de las otras. Para evitarlo, `root.fitScale` escala **todo** lo que ocupa largo — `appIconPx`, `widgetIconPx`, `systrayIconPx`, `spacingPx`, `clockFontPx`, el ancho de caja y la fuente de las etiquetas — hasta que el contenido entra, con piso en `autoShrinkMinIconSize` (default 16 px, el ícono de app; los de widget/systray conservan su proporción y pueden quedar más chicos).
- **El factor se calcula iterando, no con un binding**: el largo necesario depende de la escala, así que un binding sería un loop. Cada pasada corrige por `avail/need * 0.995` y agenda otra (`fitTimer`, 16 ms); el largo necesario es *afín* en la escala (los textos y los gaps no escalan del todo), así que converge en 3-4 pasadas. `fitPasses` es solo un tope anti-runaway; la banda muerta (`> 0.005`) y el margen del 0.995 son lo que evita la oscilación al volver a crecer.
- **Crecer de nuevo al cerrar apps** (bug 2026-07-29, `FIX-ICON-RESIZE.md`): dos cosas lo bloqueaban. (a) El gate para crecer pedía 6 % de largo libre (`need * 1.06 < avail`), así que cerrar una app no movía nada; ahora pide 1 %, que sigue siendo más ancho que el 0,5 % con que aterriza el shrink —esa asimetría es la que impide que crecer y encoger se persigan— y el paso de crecimiento usa el mismo `0.995` que el de encoger. (b) `fitPasses` quedaba gastado por la secuencia de shrink y el tope `> 12` mataba el refit siguiente; ahora `onFitNeededChanged` **resetea el contador** salvo que el cambio de largo sea el que provocó nuestra propia pasada (flag `fitSelfScaled`, que pone `refit()` al mover `fitScale`). Resetear siempre —lo que sugería el doc del fix— dejaría el tope inservible, porque cada pasada cambia `fitNeeded`.
- **El largo necesario es una *escalera*, no una curva suave** (bug 2026-07-29): `labelFontPx` es un entero, así que un dock en modo "solo nombre" con fuente de 9 px tiene **tres** estados (9, 8, 7) y bajar uno libera ~10 % del largo de golpe. Al llegar justo al máximo de apps que entran, eso hacía que el dock **vibrara sin parar**: a fuente 9 no entra → encoge → cae a 8 → sobra 10 % → el gate del 1 % crece → vuelve a 9. La regla que lo corta es `fitOverflowed`: **una vez que algo desbordó, el episodio solo puede encoger**; crecer se habilita de nuevo solo en el episodio siguiente (o sea, ante un cambio externo del contenido). Limitar el crecimiento a un poco por debajo de la escala que desbordó **no alcanza** — un escalón entero cabe en ese margen y el dock lo cruza una docena de veces achicando el bracket de a centésimas (medido: 11 pasadas con 38 apps, todas visibles). El precio es asentarse hasta un escalón por debajo del óptimo: invisible.
- Además `fitPasses` no alcanzaba como red: una sola pasada emite **varios** `fitNeeded` (el layout y las `TextMetrics` se asientan por etapas) y si alguno se toma por externo resetea el contador. Por eso `fitSelfScaled` se limpia **solo cuando `refit()` converge**, no en el primer aviso.
- **Modo degradado a solo-íconos** (`fitLabelsDropped`): último peldaño cuando no hay más largo que dar — la escala está en el piso **o** seis pasadas de encoger no alcanzaron. Ese segundo disparador es el caso común con nombres: con la fuente clavada en 7 px lo único que sigue encogiendo es la elisión de cada nombre, así que la escala converge asintóticamente a un largo que nunca entra (medido: 55 apps, 8 pasadas, 16 px de más). Al degradar, `labelMode` y `widgetLabelMode` devuelven 0 (apps **y** widgets sin texto); `config` **no se toca**, así que los menús siguen mostrando lo que el usuario eligió. Es seguro para la *exclusive zone* porque `dockThickness()` ya usa `qMax(m_iconSize, …)` como piso: un ícono de tamaño completo entra en el grosor ya declarado, incluso viniendo del modo 3.
- **La vuelta** es lo delicado: al degradarse se anota la holgura con la que asentó el layout solo-íconos (`fitDroppedSlack = avail - need`) y los nombres se reintentan cuando hay holgura *bastante* mayor (`+ max(8, 2 %)`) — lo que cubre las dos causas, contenido que se va y dock/pantalla que crece. Si al reintentar siguen sin entrar, el peldaño degrada otra vez y re-mide la holgura contra el contenido nuevo, así que no hay ciclo; el costo es 1-2 frames con los nombres visibles en ese reintento. Y `resetFit()` (enganchado a `iconLabelMode`/`widgetLabelMode`/`iconLabelWidth`/`iconLabelFontSize`) limpia el estado degradado, o un dock degradado ignoraría el modo que el usuario acaba de elegir.
- `fitAvailable` **nunca** se deriva del contenido (usa `Screen.*` o el largo propio del dock en panel/dockLength): si no, el dock flotante —que se dimensiona a su contenido— se realimentaría.
- El **grosor** queda fuera a propósito: sale de C++ (`dockThickness()`) y alimenta la *exclusive zone* de layer-shell. Encogiendo íconos el dock no adelgaza; solo se aprovecha mejor el largo.
- Además cada sección declara `Layout.minimumWidth/Height = implicitWidth/Height` (los springs, 0): así el layout **nunca** aprieta una sección por debajo de lo que dibuja, y los separadores dinámicos son los que devuelven el espacio.
- UI: Settings → Widgets → "Auto-shrink" + "Minimum icon size".

### Autohide (`src/dockwindow.cpp` + `qml/Dock.qml`)
- QML handles the slide animation via `Behavior on x/y`.
- When the animation finishes hiding, `dockWindow.setHidden(true)` is called.
- `setHidden(true)` shrinks the input region (`QWindow::setMask`) to a 3px hover strip on the screen edge so mouse events pass through to windows underneath.
- `setHidden(false)` clears the mask, restoring full input.

### Theme (`src/theme.cpp`)
- Parses `~/.config/kdeglobals` (INI format) for colors and icon theme.
- Falls back to `QPalette` outside KDE.
- Live-reloads via `QFileSystemWatcher` with a 200ms debounce (file replacement safe).
- `theme.revision` increments on every reload; QML appends it to icon URLs to bust caches (`image://icon/name@rev`).
- **Icon-theme override (global)**: `QIcon::setThemeName` is process-wide, so the chosen icon set is **global** (not per-dock), persisted as `iconTheme` in the **shared** `kdock.conf`. `Theme` loads it in its ctor and `reload()` applies `override.isEmpty() ? kdeglobals Icons/Theme : override` — so it wins over the KDE theme. `Theme::setIconTheme(id)` persists + `reload()`s (re-applies + bumps `revision` ⇒ all `image://icon` refresh live). `Theme::availableIconThemes()` (static) lists selectable themes (scans `themeSearchPaths()` + XDG `icons` for `index.theme` with an `[Icon Theme]` group **and** a `Directories` key, excluding cursor themes, `Hidden=true`, and `hicolor`; returns `(displayName, id)` sorted). Chosen via the "Icon theme" combo in Settings → General (`SettingsDialog` gets a `Theme*`).
- **Widget icon set adapted to the dock color** (per-dock): standard widget icons (volume, network, session, settings…) are monochrome and painted in a color meant for *one* background, so a dark icon set over a light panel is unreadable. `image://icon` therefore accepts an optional third field — `name@rev@themeId` — and `IconProvider` resolves that one icon against `themeId` **by reading the theme directory**: `IconProvider::resolveInTheme(themeId, name, px)` (static, so a probe can call it without a QML engine) parses `index.theme` in every search path (`QIcon::themeSearchPaths()` + the XDG `icons` dirs, same criterion as `Theme::availableIconThemes()`), orders the `Directories`/`ScaledDirectories` entries by freedesktop's "directory matches size" rule, and walks the `Inherits` chain breadth-first with a visited set (the chains in the wild have cycles: `Gruvbox-Plus-Light` → `Gruvbox-Plus-Dark` → `FlatWoken` → …). Resolved paths are cached per `theme|name|size` and cleared when the `rev` in the URL changes, like `IconColorProvider`. An empty result — no theme in the chain carries the name (custom icons, uninstalled theme) — falls through to the dock's own icon set instead of becoming a missing-icon placeholder, which is what the old `QIcon::hasThemeIcon()` guard did. **It deliberately does not touch `QIcon::setThemeName()`**: that is process-global and swapping it around a single lookup leaks — see the trap in `CLAUDE.md` (bug 2026-08-02, app icons drawn with the widget icon set after a restart). `Dock.qml` picks the id in `widgetIconTheme` from `config.widgetIconThemeMode` (`0` follow icon theme / `1` match dock color / `2` always dark icons / `3` always light icons; default `1`) plus `config.widgetIconThemeLightBg` / `widgetIconThemeDarkBg` (defaults `breeze` / `breeze-dark`), reusing the same `dockBaseIsLight` luminance test as the clock text color, and appends it through `root.widgetIconSuffix` (the URL must change or QML serves the cached pixmap). Scope: the ~15 widget sections only — launchers, relanzadores, script runners, systray and popup/menu icons keep the global icon theme (popups paint on `theme.background`, not the panel color). Configured in Settings → General → "Widget icons" + the two icon-set combos, populated from `Theme::availableIconThemes()`.
- **Per-panel color override**: `config.panelColor` (QColor, **per-dock**) overrides the inherited theme background. Stored as `#RRGGBB` (empty = inherit); `panelColorSet` (bool) reports validity. `Dock.qml`'s `background.baseColor` uses `config.panelColorSet ? config.panelColor : theme.background`, still modulated by `config.opacity`. Chosen via a `QColorDialog` swatch button in Settings → **General → "Panel color"** (plus a "Reset to theme" button); no RGB text entry.
- **Quick color presets**: `config.panelPresetColors` (QStringList, **exactly `DockConfig::kPresetColorCount` = 8** hex strings — `normalizedPresetColors()` pads/truncates so the UI can index them safely; a short list is completed from `defaultPresetColors()`, not with a repeated color) + `resetPanelColor()`. Unlike `panelColor`, the palette is **process-global**: it persists to the **shared** `kdock.conf`, so every dock offers the same quick colors. `setPanelPresetColors()` writes there once and calls `reloadPresetColors()` on **every** live `DockConfig` (`s_instances`, which includes the caller) so all the submenus update at once; `load()` calls the same `reloadPresetColors()`, which falls back to the dock's own legacy `panelPresetColors=` key when the shared file has none yet — that's the migration from the old per-dock 4-color list, no explicit step needed. Editable from Settings → General → "Quick colors" (eight swatch buttons, each re-painted from `panelPresetColorsChanged` so a change made from another dock's dialog shows up live) and applied from `qml/BackgroundColorMenu.qml`, a reusable submenu (one swatch per preset with a checkmark on the active one + "Predeterminado (tema)") that is instantiated **twice** in `Dock.qml`: in the app-icon menu and in the widget-section menu. Same dual-use pattern as `IconLabelMenu.qml`. (`PreviewConfig` keeps its own, unrelated 4-color list for the preview strips.)
- **Text/icon color derived from the dock background**: `Dock.qml`'s root exposes `dockBaseColor` (= `panelColorSet ? panelColor : theme.background`), `dockBaseIsLight` (perceptual luminance 0.299/0.587/0.114 > 0.5) and `clockTextColor` (`#141414` / `#F2F2F2`). Both clock widgets use it, because they would otherwise inherit the KDE theme's gray foreground and wash out over a custom panel color. **Opacity deliberately plays no part** — a translucent dark panel still reads as dark (the dark presets at the default 85% opacity would otherwise get black text). `dockBaseIsLight` is the single "is the dock light or dark?" test, reused by the widget icon set below.
- **Per-panel background image (tiled)**: `config.panelImage` (absolute path, **per-dock**, empty = none) + read-only `panelImageUrl` (`QUrl::fromLocalFile`). `Dock.qml` draws an `Image` (`id: bgImage`) inside the `background` Rectangle, **over** the base color, `fillMode` = `TileHorizontally` (horizontal docks) / `TileVertically` (vertical docks) — scaled to the dock thickness, repeated along its length. Honors `config.opacity`. Rounded corners in non-panel mode via `layer.effect: OpacityMask` (needs `import Qt5Compat.GraphicalEffects` — **the only QML effects module used**; runtime dep `qml6-module-qt5compat-graphicaleffects`). Chosen via a `QFileDialog` button in Settings → **General → "Panel image"** (+ "Clear"). **Note**: the config stores only the *path*; the Backup `.zip` therefore does **not** bundle the image file.

### Volume Control (`src/volumecontrol.cpp`)
- Requires `wpctl` (WirePlumber) at runtime. If missing, the widget is hidden.
- Uses `pactl subscribe` for live change notifications; falls back to 3-second polling if `pactl` is absent.
- No linkage to `libpulse` or `libpipewire` — purely `QProcess`.
- **The subscriber reconnects on its own** (`launchSubscriber()`, both here and in `AudioControl`, 2026-08-08). `pactl subscribe` exits whenever the audio server drops it — re-plugging a USB/dock audio interface is enough — and nothing used to restart it, so from that moment the widget only showed what its own optimistic writes had guessed. On `QProcess::finished` it relaunches after a backoff (1 s, doubling to 30 s; reset to 1 s whenever the previous subscriber lived >10 s, so a real outage reconnects fast while a dead server never becomes a fork loop). The post-reconnect resync is gated on the subscriber still running 500 ms later: `refresh()` is half a dozen `pactl` calls and a down server would otherwise pay them on every retry (measured: 40 → 15 invocations over 45 s).
- **Both subscribers are tied to the dock's lifetime** with `kdock::tieToParent()` (`src/childprocess.h`): `PR_SET_PDEATHSIG` so the kernel kills them however we die. The dock's SIGTERM path ends in `::_exit(0)` on purpose, which never runs `QProcess`'s destructor, so every logout/kill used to leave two `pactl subscribe` behind, reparented to systemd and running forever — they piled up across restarts (44 of them, reported 2026-08-08). Verified: 0 orphans after both SIGTERM and SIGKILL.
- **Anti-stale-cache measures** (the cache can drift when Plasma changes the volume behind our back): `Q_INVOKABLE refresh()` re-reads the default-sink state asynchronously and is called from QML on hover over the volume widget; `toggleMute()` flips the flag optimistically for instant feedback and then reconciles via `scheduleRefresh()`; a one-shot 1 s `refresh()` after construction catches changes made between the initial read and `pactl subscribe` going live (e.g. Plasma restoring the volume at login).

### Audio mixer (`src/audiocontrol.cpp` + Settings → Audio)
- `AudioControl` is a **separate, fuller backend** from `VolumeControl` (which stays a tiny default-sink indicator for the dock widget). It enumerates output sinks, input sources and per-application streams through PipeWire's PulseAudio compat CLI, and drives per-device/per-app volume, mute and default-device selection.
- `pactl` is always invoked with `LC_ALL=C` so the parsed field labels (`Name:`, `Description:`, `Mute:`, `Volume:`) stay stable under a translated session. Live updates come from a long-running `pactl subscribe`.
- The tab (`SettingsDialog::createAudioTab()`) is only added when `AudioControl::available()`, and is rebuilt live from `AudioControl::changed()`. Right-clicking the dock's volume widget opens it directly (`DockWindow::openAudioSettings()` → `showAudioTab()`).
- The **only** preference the two backends share is the >100 % ceiling: `maxVolume` (checkbox "Raise maximum volume (up to 150%)"), persisted in the `audio` group of the shared settings file.

### Solapas coloreadas de Configuración (`src/coloredtabbar.cpp` + `SettingsDialog::tabPalette()`)
- El diálogo tiene ~10 solapas y todas se veían iguales. `ColoredTabBar` (subclase de `QTabBar`) pinta un fondo distinto por solapa; `ColoredTabWidget` existe solo porque `QTabWidget::setTabBar()` es protegido.
- `paintEvent()` rellena `tabRect(i)` con `mix(palette().window(), tint, f)` — `f` = 0.80 seleccionada / 0.45 hover / 0.20 resto — y después dibuja **solo** `CE_TabBarTabLabel`: pintar `CE_TabBarTabShape` taparía el relleno. El color del texto sale por luminancia del relleno ya mezclado, así que anda en esquema claro y oscuro sin tocar nada.
- Los colores **no** se eligen acá: `SettingsDialog::tabPalette()` recorre el `DockModel` del dock seleccionado (`DockManager::modelFor()`, accesor agregado para esto), toma el `IconNameRole` de cada fila y le pide el color a `IconColorProvider::dominant()` — la misma función que tiñe el fondo de las apps corriendo en `Dock.qml`. Se descartan los casi grises (`saturation() < 70`) y los demasiado parecidos a uno ya tomado (distancia Manhattan < 150), y el resto se normaliza a una banda S/V que sirva de fondo.
- **Por diseño no es determinista**: qué solapa recibe qué color depende de qué apps estén ancladas/abiertas. Decisión explícita del usuario (2026-07-31) para no inventar una paleta propia. El relleno de las solapas que sobran (sin dock vivo, pocas apps, o tema de íconos monocromo) sale de tonos por ángulo áureo sobre el índice.
- **La barra está en columna, a la izquierda** (`QTabWidget::West`, puesto en el ctor de `ColoredTabWidget`, 2026-08-04). Con la barra horizontal el ancho era el límite duro de cuántas solapas podía tener el diálogo: once títulos pedían 1086 px y el `QTabBar` caía **sin avisar** en modo flechas de scroll, escondiendo las últimas. En columna cada solapa cuesta ~30 px de alto en vez de ~90 de ancho: doce piden 142×480 (medido con la sonda), y entran ~28 antes de que aparezcan las flechas de Qt.
- **En modo vertical el label lo dibuja esta clase**, con `drawText()` horizontal. Qt rota la etiqueta 90° para las formas West/East, y entonces la columna volvería a pedir el mismo largo de antes, ahora en vertical — el problema no se arreglaría, solo giraría. `tabSizeHint()` devuelve un ancho **uniforme** (el título más largo + 28) para que la columna tenga borde recto, y la franja de acento de la solapa actual pasa del borde superior al izquierdo. La rama sin tinte también dibuja el texto a mano, para que no se cuele ninguna etiqueta rotada.

### Solapas Colores y Fuentes (`SettingsDialog::createColoresTab()` / `createFuentesTab()`, 2026-08-05)
- Dos **centros de look & feel**: la solapa **Colores** junta todas las opciones de temas/colorsets y colores del dock, y **Fuentes** las cuatro opciones de tipografía. **Nada se quitó de las solapas originales**: cada control existe dos veces (General/Widgets/DarkMode + Colores, Widgets + Fuentes) y es una **copia sincronizada** — escribe a la misma clave de `DockConfig` (o estática) y se re-lee cuando esa clave emite su `*Changed`. Quien edite en cualquier lado, el resto lo refleja al instante (cambiar de solapa no reconstruye el diálogo, así que sin estos sync los duplicados quedarían viejos).
- **Colores** agrupa por cajas: *Escritorio (KDE)* (`showIconThemes`/`showColorSchemes` copiadas de Widgets **más** los dos pickers que aplican al instante, "· Apply icon theme" / "· Apply color scheme" — desde 2026-08-05 **todos** los selectores de tema del diálogo son `ThemePickerButton`, no combos: ver *El selector de temas del diálogo*—, desde `AppearanceControl` con `refreshIfStale()` en el armado), *Iconset del dock* (el picker de `m_theme` de General), *Íconos de widgets* (modo + sets light/dark de General), *Color del panel* + *Quick colors* (General), *Apps en ejecución* (los tres indicadores de Widgets) y *Modo oscuro* (los dos colores + el grupo "Al cambiar de modo, cambiar también:" de DarkMode). **Fuentes**: `clockFontSize`, `iconLabelWidth`, `iconLabelFontSize`, `labelBold` (copiadas de Widgets), con el mismo gating por `iconLabelMode`/`widgetLabelMode` que la solapa Widgets y la nota de que el modo de etiqueta se elige allá. Desde el 2026-08-08 también `controlManagerFontSize` ("Control Manager text size:", el texto propio del widget de Control Manager — ver la sección *Control Manager*), espejo del spinbox del grupo de Widgets, con su propio gating (el widget visible **y** dibujando texto): es el único tamaño de texto del dock que no depende de ningún modo de etiqueta, porque el texto del CM existe aunque no haya nombres que dibujar.
- **Los nombres coinciden a propósito entre solapas pero configuran cosas distintas** — hay que leerlos con cuidado al tocar esto: "Icon theme picker" (Widgets/Colores: un widget del dock que aplica el iconset al escritorio, `showIconThemes`) ≠ "Iconset del dock" (General/Colores: el override global de kdock, `Theme::setIconTheme`) ≠ "El iconset del dock" (DarkMode/Colores, fila de los extras: qué iconset usa el dock **mientras el modo oscuro está activo**, `darkAppearanceValue(DockIconTheme)`). Lo mismo con "Color scheme picker" vs "El esquema de color del sistema", y "Panel color"/"Quick colors" (General) vs "Fondo del dock" (DarkMode).
- **Sync por señal, no por rebuild**: los setters solo emiten cuando el valor cambia de verdad, así que `setValue`/`setChecked`/`setCurrentIndex` desde el handler del config termina el loop solo (patrón ya usado en `menuPopupWidthChanged`). Casos especiales: el combo del iconset del dock se sincroniza con `Theme::changed` (relee `m_theme->iconTheme()` — se dispara en cada reload de kdeglobals, no solo al cambiar el override); los tres combos de "Íconos de widgets" se re-seleccionan juntos con `widgetIconThemeChanged` (la señal cubre los tres setters); los colores del modo oscuro y las filas extras se re-leen con `darkModeChanged` (las estáticas terminan en `notifyDarkModeChanged()`). El combo de tema usa `selectComboData()` (anónimo de `settingsdialog.cpp`): selecciona y, si el id configurado no está instalado, agrega una entrada "(not installed)" explícita — sin ella el combo mentiría mostrando el primer tema.
- **Helpers compartidos** para que Colores y DarkMode no dupliquen ~130 líneas: `makeColorButton()` (botón swatch + `QColorDialog`, devuelve el `refresh` para colgarlo de señales), `addDarkAppearanceExtrasRow()` (una fila check + combo oscuro + combo normal, con re-selección propia vía `darkModeChanged` y `QSignalBlocker` para no re-persistir al sincronizar) y `addDarkAppearanceExtras()` (las tres filas juntas, desde `AppearanceControl`/`Theme`). `createDarkModeTab()` usa los tres.
- **Trampa de los extras**: el seed de "normal" desde el valor vivo del sistema solo ocurre en el primer armado; la re-selección por `darkModeChanged` lee lo persistido y un valor vacío cae en "(no cambiar)". Es consistente con lo que el config dice y no toca el comportamiento del DarkMode original.
- Posición en el diálogo: `buildTabs()` las inserta inmediatamente después de General. La barra en columna no cambió de ancho (las 14 solapas entran de sobra).

### App Menu (`src/appmenu.cpp` + `qml/AppMenuPopup.qml`)
- Kickoff-like application menu, exposed to QML as `appMenu` context property.
- Groups installed `.desktop` apps by freedesktop main categories (Development, Games, Graphics, Internet, Multimedia, Office, Settings, System, Utilities, Other).
- Sidebar categories: **Favorites**, **All Applications**, then present categories.
- **Every sidebar row carries an icon.** The freedesktop categories used to be emitted with an
  empty icon and only the `.menu` submenus had one (out of their `.directory` files), which
  left the list looking half-finished. `AppMenu::sections()` now maps each canonical category
  to its standard `applications-*` name, with a fallback or two per entry and a
  `QIcon::hasThemeIcon()` check (`pickIcon()`), because those names are near-universal but not
  guaranteed. A submenu whose `.directory` has no `Icon=` gets `folder`. Favorites and All
  Applications get one too — the old comment said an icon would elide "All Applications", and
  the fix for that is `AppMenuPopup`'s sidebar at **196 px** instead of 172. One change, both
  menus: `kdock-tilemenu`'s sidebar reads the same `sections()`.
- Search filters by name, comment, and desktop file id.
- Editable favorites: star toggle per app, drag-to-reorder, persisted in `DockConfig::menuFavorites`.
- The menu **button** in the dock is a regular section (token `"menu"`) gated by `config.showMenuButton` (default off). Like the `apps`/`systray` blocks, it is a **drop-only anchor** (not draggable as a unit).
- **Keyboard interactivity**: When the popup opens, `dockWindow.setKeyboardInteractive(true)` sets `kdock.keyboardInteractivity` to **exclusive** (1) so the search field can receive text input. On close (via both `onAboutToHide` and `onClosed`) it reverts to none (0). `requestActivate()` is also called to ensure the compositor routes focus to the dock.
- `AppMenu` is instantiated in `DockWindow`'s constructor as a child of the `DockWindow`; `main.cpp` does not need to include it.

### Multi-instance / `DockManager` (`src/dockmanager.cpp`)
- `main.cpp` no longer builds a single dock. It creates the **system-wide singletons** (`Theme`, `DesktopEntryIndex`, `WindowMonitor`, `VolumeControl`, `BrightnessControl`, `OverviewControl`, `DesktopControl`, `MonitorControl`, `MaxMinControl`, `ActiveWindowControl`, `SystrayHost`, `RelanzadoresManager`, `ScriptRunnersManager`, `ClipboardHistory`, `DesktopWallpapers`), packs them into a `DockManager::Shared` struct, and constructs a `DockManager`. `DesktopWallpapers` viaja en `Shared` solo para que la solapa *Wallpapers* lo alcance con `m_manager->desktopWallpapers()`, igual que `audio()`: no hay dock que lo use, así que `dockwindow.{h,cpp}` no se toca.
- **Opt-in per (monitor, slot)**: the user enables a dock (Settings → Monitor + Dock selectors → "Show dock here"). The enabled set is persisted as `enabledScreens` (StringList of **dockIds**; key name kept for backward compat) in the shared `kdock.conf` via `DockConfig::enabledDocks()` / `DockConfig::setDockEnabled()`. `DockManager::setDockEnabled` also **seeds a new non-slot-0 dock with a free screen edge** (one not used by the monitor's other docks) so they don't overlap.
- **Per-dock config**: `DockConfig(const QString &dockId)` opens the dock's file. The constructor body is a shared `DockConfig::load()` used by both constructors. The dockId is retained (`DockConfig::dockId()`); `screenName` (the bound output, `screenOfDockId(dockId)`, shared by all slots on a monitor) is written into the file so `DockWindow::applyScreen()` binds the surface to the right `wl_output`.
- **First-run migration** (`DockManager::migrateFirstRun`): when `enabledScreens` is empty, copies the old shared `kdock.conf` into `kdock-<primary>.conf` (slot 0) and enables the primary monitor's slot 0 — the existing single-dock setup carries over unchanged.
- **Hotplug**: `sync()` runs on `screenAdded`/`screenRemoved`/`primaryScreenChanged` and on enable/disable. It creates a dock for every enabled dockId whose screen is connected and destroys the rest. A change of **primary** recreates the affected instances because only the primary dock hosts relanzadores.
- **Shared vs per-instance**: volume/brightness/overview/desktop/monitor/wallpaper/power/window-monitor/systray-host/relanzadores/scriptRunners/clipboardHistory are shared across all docks. Per-instance objects (created in `createInstance`): `DockConfig`, `DockModel`, `SystrayModel` (one per dock) and **both clocks** — `ClockWidget`/`ClockWidget2` are per-dock because their 12/24h/date/seconds format is a per-dock config setting. Configs are cached in `m_configs` (keyed by dockId, kept alive even when the dock is hidden) so `SettingsDialog` can edit any dock; `configFor(dockId)` returns the same object the live dock uses (edits apply live).
- **Systray: any dock, one at a time** (2026-08-04; before this it was the primary dock only). There is no technical obstacle to several — `SystrayHost` is a process-wide singleton and `SystrayModel` is just a filtered view over `host->items()` — so **every** dock gets its own model and the host pointer, and which one draws the tray is its own `config.showSystray`, gated in `Dock.qml`. Keeping the decision out of the instance role is deliberate: toggling the tray never tears down a dock window (no flicker), unlike a change of primary monitor. Exclusivity is a **product rule, not a limitation** (two docks drawing the same items would duplicate every icon and open two menus per click) and lives in the UI: `DockManager::systrayDockId()` names the current owner and Settings → Widgets disables the checkbox on every other dock, with a note saying where to turn it off. `DockManager::normalizeSystrayOwner()` runs once at startup and drops every claim but one (preferring `primaryDockId()`): configs written while the tray was primary-only may well have `showSystray=true` in several files, which would now show several trays.
- **Relanzadores are per-dock (opt-in)**: the shared `RelanzadoresManager` is passed to **every** dock; `createInstance` also calls `window->setPrimary(primary)` which exposes the `dockIsPrimary` context property. Visibility is filtered per dock via two `DockConfig` lists: the **primary** dock uses `relanzadoresHidden` (default empty ⇒ all shown, incl. newly created ones), every **other** dock uses `relanzadoresShown` (default empty ⇒ none). `RelanzadoresManager::visibleIds(primary, hidden, shown)` centralizes the rule; `Dock.qml`'s `root.visibleRelanzadorIds` drives both the section visibility and the `relanzadoresComp` Repeater (delegate uses `relanzadores.get(modelData)`). In Settings → **Relanzadores**, each row is a **checkbox** whose state reflects the currently-edited dock (primary → hidden semantics, else → shown); the relanzador **list/apps are still global** (one config), only visibility is per-dock. This preserves the old behaviour (relanzadores only on the primary dock) with no migration.
- `SettingsDialog` takes a `DockManager*`: a monitor selector + a **Dock (1..3)** slot selector + "Show dock here" checkbox at the top; changing either combo recomputes the dockId (`selectFromCombos` → `selectDock`) and calls `buildTabs()` to rebind every tab to that dock's config. The legacy per-dock "Screen (Automatic)" combo is hidden when a manager is present. It also takes a `Theme*` (for the Icon-theme combo). **Each tab is wrapped in a `QScrollArea`** (in `buildTabs()`) so the tall General/Widgets tabs scroll internally, and the dialog's initial height is clamped to the screen (`resize(500, min(560, avail-80))`) so the bottom Close/Quit buttons are always visible on small screens.
- **Alias de dock (`DockConfig::alias`)**: nombre amigable opcional por dock, persistido como `alias=` en el `.conf` del dock. Vacío = el nombre por defecto `"<screen> — Dock <n>"`. `SettingsDialog::dockLabel()` (static, lee el alias con un `QSettings` a mano para no crear `DockConfig`s) es la fuente única de nombres: desde el 2026-08-06 el alias se **suma** al nombre automático (`"<screen> — Dock <n>: <alias>"`) en vez de reemplazarlo — renombrar borraba el monitor de la fila, que es justo lo que distingue un dock de otro; como el monitor sale del `dockId`, los docks ya renombrados lo recuperan solos, sin migración: lo usan la lista de **Docks configurados** y la nota de la bandeja del sistema. Se edita con **doble clic en la fila** de la lista (vacío quita la clave). El alias **no** se expone a QML — el dock no dibuja su propio nombre, solo el diálogo y la nota del systray. `copySettingsTo()` (ver abajo) **omite** `alias` a propósito: un dock copiado a otro monitor empieza sin nombre y el original conserva el suyo.
- **Vista previa / copia entre monitores** (`previewDockOnScreen`): la copia NO es un `QFile::copy` del `.conf` — eso arrastraba el `screenName` del origen y, peor, un dock recién habilitado puede no haber volcado todas sus keys a disco (el archivo apenas tiene las que se escribieron explícitamente), así que el preview salía con defaults (bug 2026-08-04: "solo muestra un panel estándar en el borde de abajo"). Ahora `DockConfig::copySettingsTo(dst)` copia las **settings en memoria** (origen → destino) vía `QSettings::allKeys()`, rebinda `screenName` al monitor destino, omite `alias`, y devuelve false si no se pudo escribir (el preview entonces avisa error en vez de fallar en silencio). **El slot destino siempre está libre** (lo garantiza `firstFreeSlotWithPreviews`: ni live ni known ni preview pendiente), así que **cualquier archivo preexistente en ese slot es un remanente de un dock borrado/deshabilitado y se SOBRESCRIBE** — reutilizarlo era el bug 2026-08-04b ("solo funciona eDP-1→DP-5, el resto muestra el dock por default"): los slots libres de DP-7 y eDP-1 tenían stubs viejos (`kdock-DP-7.conf` 41 bytes, `kdock-eDP-1-2.conf` 27 bytes) que se mostraban en vez del panel fuente. El remanente se borra al cancelar el preview (siempre se marca como creado).
- **Borrar de la lista (solapa Docks)**: ahora borra de verdad. `DockManager::removeDock()` hace `destroyInstance` (el `DockWindow` apunta al `DockConfig` cacheado), descarta previews cuya fuente es ese dock, lo quita de `knownDocks`/`enabledDocks`, **borra el archivo `.conf`** y suelta el config cacheado. Re-habilitar el mismo monitor+slot arranca de cero, no "resucita" la config del dock borrado. La confirmación avisa que el archivo se elimina.
- **Identificar qué dock se está editando (2026-08-06)**: la barra de arriba del diálogo tiene una **segunda fila** — un combo *Escritorio:* y el *Nombre:* del dock (el `dockLabel()`, o sea el alias) en negrita. El combo lista **una entrada por conjunto de escritorios distinto entre los docks de ese monitor** (siempre incluye *Todos (dock base)* y el conjunto del dock actual), y elegir una **salta al primer dock de ese grupo** — no reasigna nada: las asignaciones se editan en la solapa *Docks*. Va por `activated` y no por `currentIndexChanged`, porque `reloadDockHeader()` reconstruye el combo en cada cambio de dock y eso volvería a cambiar de dock. Tres detalles: `selectDock()` ahora **sincroniza también los combos de monitor y slot** (antes solo los leía), agregando una entrada *(desconectado)* si el monitor del dock no está conectado; `showMonitorsTab(dockId)` **empezó a llamar a `selectDock(dockId)`**, así que el diálogo entero (no solo la fila resaltada) queda sobre el dock que el usuario señaló; y `reloadDocksList()` re-corre `reloadDockHeader()`, porque el alias y los escritorios se editan ahí y los dos se ven en la barra.
- **Filtros de "desconectado" en la solapa Docks (2026-08-06)**: un casillero bajo cada título — *Ocultar los docks de monitores desconectados* y *Ocultar monitores desconectados* — los dos **marcados por defecto**, persistidos en la sección `[ui]` del `kdock.conf` compartido (`SettingsDialog::hideOfflinePref()`; no van en `DockConfig` porque son del diálogo, no de un dock). Dos excepciones que hay que mantener si se toca el filtro: la lista de docks **nunca esconde el dock que se está editando** (ni el seleccionado en la solapa), y la de monitores **nunca esconde el monitor propio del dock**, cuya fila es el casillero "mostrar el dock acá".
- **Crear dock vacío** (`DockManager::createEmptyDock`, menú *Dock* y menú de sección del clic derecho): busca el primer slot libre del **mismo monitor** (`firstFreeSlotWithPreviews`, o sea contando también los previews pendientes), **borra el `.conf` que haya quedado en ese slot** —"vacío" es los defaults, no la config del dock que se borró antes— y llama a `setDockEnabled(id, true)`, que es lo que le siembra un borde libre y lo pone en pantalla. **Y lo ata al escritorio virtual actual, pero solo si hace falta** (2026-08-06): un dock nuevo es un dock *base*, y en un monitor cuyo escritorio actual ya tiene docks propios eso significa **invisible** (`wantedDocks`: ganan los del escritorio), o sea que el comando parece haber fallado; si en cambio el monitor no tiene ningún dock atado a este escritorio, el base ya se ve y atarlo **escondería** los docks base del monitor, así que se deja base. `DockWindow::createEmptyDock()` después abre el diálogo con `showMonitorsTab(nuevo)`: el dock nace sin nombre y en un borde cualquiera, así que lo primero que hay que hacer es nombrarlo/moverlo.
- **Mover Sig. Monitor** (`DockManager::moveDockToNextMonitor`, menú *Dock* → *Mover Sig. Monitor* del clic derecho, en ambos menús): mueve el dock al **siguiente monitor conectado**, dando la vuelta al final de la lista (`connectedScreens()` + `(idx+1) % size`). Reusa la misma maquinaria de la solapa Docks: `copySettingsTo()` clona la config a un slot libre del destino (`firstFreeSlot`), luego el dock viejo se deshabilita, sale de `knownDocks` y su `.conf` se **renombra a `.conf.tmp`** — no se borra, por si el usuario lo quiere recuperar a mano. **La limpieza de esos `.tmp` es diferida a propósito**: cada llamada a `moveDockToNextMonitor()` primero barre el directorio de configs (`QFileInfo(settingsFilePath()).absoluteDir()` + `entryList("kdock-*.conf.tmp")`) y borra todo lo que encuentre, así el primer move crea un `.tmp` y el siguiente move desde cualquier dock lo limpia antes de crear el suyo — la solapa Docks nunca acumula entries sin uso, y la limpieza no cuesta nada de código de UI. Se dispara diferido desde `DockWindow::moveToNextMonitor()` con `QTimer::singleShot(0)` (igual que `deleteDock()`: `sync()` destruye la instancia del dock que está en medio del handler de clic). La señal nueva `DockManager::dockListChanged()` avisa a la solapa Docks (`reloadDocksList()` + `reloadMonitorsForSelectedDock()`) y el diálogo abierto se para sobre el dock nuevo vía `showMonitorsTab(nuevo)`. Sin estado por dock ni config nueva: solo la señal.

### Docks por escritorio virtual (`VirtualDesktops` + `DockManager::wantedDocks`, 2026-08-05)
- **La regla vive en un solo lugar**: `DockManager::wantedDocks(desktop)`. Cada dock lleva `DockConfig::dockDesktops()` — una lista de posiciones **1-based** persistida como `desktops=` en su `.conf`, **vacía/ausente = dock base**. Para el escritorio actual, y **monitor por monitor**: si ese monitor tiene docks propios de ese escritorio se muestran **esos y solo esos**; si no tiene ninguno, se muestran sus docks base. O sea que configurar un escritorio en un monitor **no pela los demás monitores**, y una config sin ninguna clave `desktops=` se comporta exactamente como antes (no hay migración). Cambiarla a un override global es tocar el bucle final de esa función y nada más.
- **Tope fijo**: `DockConfig::kMaxDesktops = 5` (decisión de producto, no técnica). Y `kMaxDocksPerScreen` subió de 3 a **6**, porque un monitor ahora puede tener sus docks base más uno por escritorio.
- **Diferido y persistente**: un dock que el escritorio actual no quiere **no se destruye, se oculta** (`Instance::onScreen` + `DockWindow::setDeskVisible`), y **no se construye hasta la primera vez que se entra a su escritorio**. Medido en la sesión real con dos docks en eDP-1: arranque 240 MB con solo el base construido; primer salto al Escritorio 2 → +57 MB y se crea el segundo; a partir de ahí ir y venir no mueve el RSS ni un MB, y nunca se destruye una instancia. Al reiniciar el proceso se vuelve a arrancar solo con el juego del escritorio actual.
- **`DockWindow::setDeskVisible(false)` es `hide()`**, que en QtWayland destruye la superficie de layer-shell (no el `QQmlEngine` ni el modelo). Volver hace `applyScreen()` **antes** de `show()` —mientras estuvo oculta, `applyScreen()` se saltea la recreación por su chequeo de `wasVisible`, así que el `wl_output` puede haber cambiado— y después re-aplica la máscara de autohide con `applyHiddenMask()`, porque la máscara vive en la ventana de plataforma, que es nueva. `setHidden()` corta temprano cuando el flag no cambió, de ahí que la máscara esté factorizada aparte; también se re-aplica en `widthChanged`/`heightChanged`, que de paso arregla la máscara vieja tras un cambio de tamaño.
- **`src/virtualdesktops.{h,cpp}`**: singleton del proceso (viaja en `Shared`) que lee `org.kde.KWin /VirtualDesktopManager` y emite `currentChanged(int)` / `countChanged()`; `DockManager` los conecta a `sync()`, igual que el hotplug de monitores. Expone posiciones **1-based** y `0` cuando KWin no contesta (X11, wlroots, el arnés de Xvfb) — con `0` la regla devuelve el juego base. `ActiveWindowControl` dejó de tener su propia copia del demarshalling y le pide a este objeto los uuids.
- **La bandeja del sistema pasó a ser exclusiva por grupo, no global**: dos docks chocan solo si **pueden estar en pantalla a la vez**, o sea si sus conjuntos de escritorios se intersecan (vacío = todos) — `DockManager::canCoexist()`. `normalizeSystrayOwner()` normaliza dentro de cada grupo y el diálogo usa `systrayDockIdFor(dockId)` en vez de `systrayDockId()`, así un escritorio con docks propios puede tener su propia bandeja. Los **relanzadores** no se tocaron: `primaryDockId()` sigue siendo uno solo y un dock de otro escritorio los muestra con la lista por dock `relanzadoresShown` que ya existía.
- **UI**: la solapa **Docks** (se llamaba *Monitores* hasta el 2026-08-06) ganó abajo una tercera lista, *Escritorios virtuales*, colgada de la misma selección `m_selectedTabDockId`, más el botón **Duplicar para el escritorio…** (`duplicateDockForDesktop`: `firstFreeSlot` en el **mismo** monitor → `copySettingsTo` → `setDockDesktops({n})` → habilitar **sin sembrar borde**, para que la copia caiga exactamente donde está el original). Las tres listas tienen `setMaximumHeight`: sin eso las dos primeras se comían el alto y la nueva quedaba abajo del pliegue, invisible.
- **El área de maximizado de KWin NO es por escritorio** (`kwin/src/workspace.cpp`, `rearrange()`: `m_screenAreas` está indexado solo por output). Cuando el dock entrante y el saliente coexisten brevemente durante `DockManager::sync()` —el entrante se muestra antes de ocultar el saliente para no dejar un frame sin dock— ambos reservan zona exclusiva sobre el mismo output, KWin achica el work-area y las ventanas maximizadas **no se recuperan** solas al soltarla el saliente. La corrección está en `src/desktopmaximize.{h,cpp}`: un `DesktopMaximize` enganchado a `VirtualDesktops::currentChanged` que mantiene un `QSet` de ventanas previamente maximizadas (alimentado por `AbstractWindow::changed`) y, tras cada cambio, re-ejecuta `setMaximized(false); setMaximized(true)` —el ciclo toggle— con reintentos escalonados (200/700/1500 ms) sobre las ventanas del escritorio nuevo que eran maximizables y no están minimizadas ni son `skipTaskbar`. La feature se apaga con el casillero de Configuración → General o con `KDOCK_NO_WINDOW_ACTIONS=1`.
- **Widget paginador (`pager`)**: los números de los escritorios de KWin, clic para cambiar. Diseño mínimo a propósito — cajas cuadradas con el número, la actual rellena con `theme.highlight`; nada de miniaturas de ventanas. Es un **bloque** (`isBlock()`), porque cada número es su propio blanco de clic: eso lo excluye del `Binding` de `hovered` (no declara la property) y del arrastre por sección, igual que la bandeja. El color del número sobre el relleno sale del mismo test de luminancia que `root.dockBaseIsLight`. Se corta en `kMaxDesktops`, y `VirtualDesktops::switchTo()` escribe la propiedad `current` por D-Bus sin asumir nada: el estado se actualiza cuando KWin contesta con `currentChanged`.
- **Submenú *Escritorio* de los íconos de apps**: cinco ítems fijos (uno por `kMaxDesktops`), cada uno se oculta solo cuando ese escritorio no existe; el tope fijo es lo que permite escribirlos a mano en vez de un `Repeater` (un `Menu` con hijos dinámicos es la parte delicada de QtQuick.Controls).
- **Submenú *Escritorio* en el clic derecho de un ícono de app**: *Ventana aquí* (trae las ventanas al escritorio actual) más *Enviar a <nombre>* por cada escritorio. Mueve **todas** las ventanas del ícono, como *Cerrar todas*, y **nunca cambia de escritorio**: se queda donde está el usuario. Va por `DockModel::sendToDesktop(row, position)` → `AbstractWindow::moveToOnlyDesktop(uuid)`, que entra al destino y **deja todos los demás** — el protocolo informa la pertenencia de a un escritorio por vez, así que `PlasmaWindow` ahora escucha `virtual_desktop_entered`/`left` y mantiene `AbstractWindow::desktops`. Ojo con la convención: **lista vacía significa "en todos los escritorios"**, no "en ninguno", y por eso una ventana pineada a todos solo recibe el *enter* (KWin la reduce al destino). Los tres ítems *Enviar a* están escritos a mano en vez de un `Repeater`: el tope es fijo en tres y un `Menu` con hijos insertados dinámicamente es la parte delicada de QtQuick.Controls.
- **Estado**: instalado y en uso desde el 2026-08-05 (el paginador y el submenú *Escritorio*, desde el 2026-08-06; *Crear dock vacío*, el selector de escritorio de la barra, los filtros de "desconectado" y el alias no destructivo, probados en la sesión real el 2026-08-06), probado en la sesión real sin bugs conocidos. Lo único que quedó sin ejercitar es el cruce **entre monitores** (que atar un dock a un escritorio en un monitor no toque a los otros): la lógica está separada por pantalla y las sondas cubren el agrupamiento, pero al momento de escribir esto había un solo output conectado y Xvfb no da una segunda pantalla.

### KWin-shortcut widgets (overview / move-to-desktop / move-to-monitor / maxmin)
- **`src/kwinshortcut.{h,cpp}`** centraliza las dos únicas cosas que estos widgets comparten:
  `KWinShortcut::sessionIsKde()` (chequeo de `XDG_CURRENT_DESKTOP`/`XDG_SESSION_DESKTOP`,
  cacheado en un static) y `KWinShortcut::invoke(name)` (la llamada D-Bus). `MonitorControl` y
  `MaxMinControl` lo usan; `OverviewControl`/`DesktopControl` todavía traen su copia propia del
  bloque (no se tocaron para no ampliar el diff — migrarlas es trivial si se las edita).
- Cuatro widgets KDE-only son `QObject`s finos que disparan un **atajo global** de KWin por D-Bus (`org.kde.kglobalaccel /component/kwin invokeShortcut <name>`), en vez de reimplementar protocolos del compositor:
  - `OverviewControl::toggle()` → `"Overview"`. **Note**: `org.kde.kwin.Effects.toggleEffect "overview"` does *not* work in Plasma 6 (it loads/unloads the plugin, not the screen); the shortcut is the correct path. `active` is tracked optimistically per click.
  - `DesktopControl::moveToNextDesktop()` → `"Window to Next Desktop"` (token `movetodesktop`, icon `go-next`).
  - `MonitorControl::moveToNextScreen()` → `"Window to Next Screen"` (token `movetoscreen`, icon `video-display`). Its `available` is just the KDE-session check (like overview/move-to-desktop), so the button shows whenever the user enables it. (It previously also required `QGuiApplication::screens().size() > 1`, which hid the button on single-monitor states even when enabled — removed.) El **clic derecho** llama a `moveToPreviousScreen()` → `"Window to Previous Screen"`; con dos monitores las dos direcciones caen en la misma pantalla, recién se distinguen desde tres.
  - `MaxMinControl` (token `maxmin`, ícono `window-maximize`, etiqueta por defecto *MaxMin*): `maximize()` → `"Window Maximize"` en el clic izquierdo, `minimize()` → `"Window Minimize"` en el derecho. Actúa sobre la ventana **activa**, que sigue siendo la del usuario porque la superficie del dock pide `keyboard_interactivity: none` y un clic no le roba el foco. El ícono es **estático a propósito**: `"Window Maximize"` es un toggle del lado de KWin (también restaura) y el `WindowMonitor` del dock reporta `activated`/`minimized` pero **no** `maximized`, así que no hay estado que espejar sin agregar protocolo.
- Availability of overview/move-to-desktop is a static KDE-session check (`XDG_CURRENT_DESKTOP`/`XDG_SESSION_DESKTOP`). Each has a matching `show*` flag in `DockConfig` and a checkbox in Settings → Widgets.

### Widget "Close window" (`closewindow`, `ActiveWindowControl`)
- **Clic izquierdo**: cierra la ventana activa. Va por `AbstractWindow::requestClose()`, o sea
  el backend de ventanas que ya usa la barra de tareas — anda igual en KWin y en wlroots.
- **Clic derecho**: manda la ventana activa al **escritorio virtual siguiente** dejando al
  usuario donde está. **Ningún atajo de KWin sirve para esto**: probado en vivo el 2026-08-01,
  `"Window One Desktop to the Right"` (igual que `"Window to Next Desktop"`, el que usa el
  widget `movetodesktop`) mueve la ventana **y arrastra la vista** al escritorio nuevo.
- Por eso el movimiento va por **protocolo**, no por atajo: `request_enter_virtual_desktop(next)`
  seguido de `request_leave_virtual_desktop(current)` sobre el `org_kde_plasma_window`
  (`AbstractWindow::moveToDesktop()`, implementado solo en `PlasmaWindow`; `canChangeDesktop()`
  lo declara). El **orden importa**: una ventana que no pertenece a *ningún* escritorio es como
  el protocolo dice "en todos", así que primero se entra y después se sale.
- Los **ids de escritorio** (uuids) salen de KWin por D-Bus, `org.kde.KWin /VirtualDesktopManager`,
  propiedades `desktops` (a(iss) = posición, uuid, nombre; se ordena por posición) y `current`.
  Se leen en el momento del clic, sin caché ni señales: son dos llamadas baratas y así no hay
  estado que se desincronice. Las dos trampas de `QDBusArgument` que esto trae están en
  `CLAUDE.md` (una de ellas **abortaba el proceso**).
- La ventana que se mueve es la activa, así que está en el escritorio actual por definición:
  ese es el que se abandona. Una ventana fijada "en todos los escritorios" solo pierde el
  actual, que es lo mismo que hace el gestor de tareas de KDE.

### Negritas (`config.labelBold`)
- Un solo booleano por dock que dibuja en negrita **todos** los nombres que el dock muestra: los de las apps (`labelText`), los de las secciones y la **fecha del reloj** (los `Text` de fecha de `clockComp`/`clock2Comp` llevan `font.bold: config.labelBold` desde 2026-08-05). La **hora** de los relojes ya era negrita fija y sigue igual: la opción agrega peso, nunca lo saca.
- El nombre de la app activa sigue en negrita aunque la opción esté apagada (`config.labelBold || delegateRoot.active`): la negrita del foco es una señal distinta y no se pierde.
- **La medición del ancho tiene que seguir la opción, y son CUATRO mediciones, no dos.** Las dos sondas de la raíz de `Dock.qml` (`appNameProbe`/`secNameProbe`) solo alimentan el **grosor** en C++; el **ancho de la caja de cada nombre** lo mide un `TextMetrics` dentro de cada delegate: `labelMetrics` (apps) y `secLabelMetrics` (widgets).
  - `secNameProbe` copia `config.labelBold`, y el bloque `Connections { target: config }` agrega `onLabelBoldChanged` para re-medir. Medido bajo Xvfb: un dock vertical pasa de 95 a 101 px de grosor al prender la opción, o sea la realimentación QML→C++ funciona.
  - `appNameProbe` y `labelMetrics` miden **siempre** en negrita (el peso de la app activa, ver *App-icon labels*), así que los nombres de apps nunca se vieron afectados.
  - `secLabelMetrics` **se quedó sin la negrita hasta el 2026-08-08** (bug reportado por el usuario con una captura): el nombre de un widget se medía en redonda y se dibujaba en negrita, así que la caja salía unos px corta y con `labelLines=2` la última letra se iba al segundo renglón ("Blade" dibujado "Blad/e", "Config" → "Confi/g"); con un renglón se elidía. **Este bug no mueve el grosor del dock**, así que la verificación de arriba sale idéntica — se ve solo en una captura. Si agregás otra opción de peso o tamaño de fuente, tocá las cuatro.
- UI: **Configuración → General → "· Bold text:"** (y su espejo en **Fuentes**), junto al ancho y al tamaño de fuente del nombre. El ancho y el tamaño se deshabilitan cuando no hay ninguna etiqueta de app/widget encendida, pero **el casillero de negritas queda activo siempre**: la fecha del reloj es texto fijo, así que la opción vale aunque no haya nombres que dibujar.

### Modo Dark (`config.darkModeActive`, solapa DarkMode, token `darkmode`)
- **La idea central: es un override de lectura, no una reescritura.** El modo oscuro **nunca** escribe sobre `panelColor`, `iconRunningBackground` ni ningún otro color que el usuario haya configurado; los tres bindings de `Dock.qml` (`dockBaseColor`, `dockTextColor` y los colores que salían de `iconColors`) eligen el valor oscuro **en el momento de dibujar**. Por eso volver a Normal restituye el esquema exacto sin snapshot ni restore, y por eso no hay nada que migrar. Verificado: tras una corrida en modo oscuro el `.conf` conserva su `panelColor` intacto.
- **Qué cambia mientras está activo**: el fondo del dock pasa a `darkBackground` (`config.opacity` se sigue aplicando igual, así que la transparencia configurada no se toca); los nombres pasan a `darkAccent`; el resaltado de las apps que corren deja de usar el color dominante de cada ícono y usa ese mismo acento; y `root.widgetIconTheme` fuerza `widgetIconThemeDarkBg` sin mirar `widgetIconThemeMode` (con el panel oscuro por definición, el set claro sería un ícono negro sobre negro).
  - Los puntos y la línea de borde son la excepción de una sola línea: sobre el relleno de acento (`iconRunningBackground` prendido) se pintan con `darkBackground`, porque el acento sobre el acento no se ve. Es la misma lógica que ya tenía `iconColors.contrasting()`.
- **Dónde vive el estado.** Por dock, en su `.conf`: `darkMode` (el on/off cuando el alcance es por dock) y `showDarkMode` (el widget). App-wide, en el `kdock.conf` compartido: `darkModeAllDocks` (**alcance**), `darkModeOn` (**on/off** cuando el alcance es app-wide), `darkModeExceptions` (lista de dockIds), `darkAccent` y `darkBackground` (los dos colores son **únicos para toda la app**, no por dock).
- **Alcance y estado son dos claves distintas, y esa separación es el diseño.** `darkModeAllDocks` es una *preferencia* ("prender/apagar desde cualquier lado vale para todos") que sobrevive a que el modo se apague; `darkModeOn` es el interruptor. Fundirlas en una sola clave fue el bug 2026-08-02: apagar el modo bajaba el alcance con él, así que volver a prenderlo solo alcanzaba al dock desde el que se hizo clic. `darkModeOn` **defaultea a `darkModeAllDocks()`**, que es la migración de las configs escritas antes de que la clave existiera (donde "el alcance es app-wide" quería decir "y está prendido").
- **La resolución es una sola función**, `DockConfig::darkModeActive()`: con alcance app-wide, `darkModeGlobal() && !darkModeExceptions().contains(m_dockId)`; si no, el flag propio del dock. Resolver en vez de propagar es lo que hace que una excepción **no necesite ninguna escritura** en el `.conf` del dock exceptuado — incluidos los docks que no están corriendo.
- **Cambiar el alcance no cambia lo que se ve.** `setDarkModeAllDocks()` lee primero lo que cada dock está dibujando y siembra con eso el almacén que el alcance nuevo va a leer: al ampliar, `darkModeOn` = "alguno estaba en oscuro"; al angostar, el flag propio de cada `DockConfig` vivo. Un ajuste de alcance que apaga media pantalla es un ajuste roto.
- **Una sola señal para todo el grupo**, `darkModeChanged()`. Los setters estáticos terminan en `notifyDarkModeChanged()`, que la emite sobre **cada** `DockConfig` de `s_instances`: el interruptor global, las excepciones y los dos colores son de proceso, así que un cambio tiene que repintar los 15 docks, no solo el que se está editando.
- **`setDarkModeActive(bool)`** (`Q_INVOKABLE`; lo llaman el submenú, el widget y el checkbox de la solapa) mira el alcance: app-wide → escribe `darkModeOn` y **no toca ni el alcance ni las excepciones**; por dock → escribe el flag del dock. Única salvedad: pedir *Dark* desde un dock que está en la lista de excepciones lo saca de ella, porque dejarlo en normal sería ignorar el clic.
  - Las **excepciones son una configuración de la solapa**, no un subproducto de conmutar el modo: se tildan a mano y sobreviven a los ciclos de apagado/encendido. (Una versión intermedia las limpiaba en cada cambio de modo — era el parche a haber fundido alcance y estado.)
- **Los tres controles son un solo interruptor con tres caras.** El checkbox de la solapa lee `darkModeActive()` (el valor efectivo, **no** el flag propio del dock: leer el flag equivocado hacía que dijera "activo" con el dock dibujando normal) y escribe con `setDarkModeActive()`, igual que el submenú y el widget; su etiqueta sigue el alcance ("Activado en este dock" / "Activado en todos los docks"). La solapa se redibuja entera desde la config —no desde los otros controles— en un único `syncAll()` colgado de `darkModeChanged`, con `QSignalBlocker` sobre los tres widgets porque la lista de excepciones es justamente lo que `setDarkModeExceptions()` cambia.
- **Efectos opcionales sobre el escritorio** (`DarkModeAppearance`, `src/darkmodeappearance.cpp`): el modo puede además cambiar el **esquema de color de KDE**, el **iconset de KDE** y el **iconset propio del dock** (`Theme::setIconTheme`). Cada uno se prende por separado en la solapa.
  - **Estos no son overrides, son estado global**, y esa es la diferencia que manda todo el diseño: los colores del dock se eligen al dibujar y volver atrás sale gratis, pero un esquema de KDE hay que escribirlo y *desescribirlo*. Por eso cada ítem guarda el valor de **los dos modos** (`darkModeXDark` / `darkModeXNormal`).
  - **El vacío significa cosas distintas en cada lado, y por eso son dos etiquetas.** En el selector de modo oscuro, id vacío = **"(no cambiar)"**: `applyColorScheme()`/`applyIconTheme()` no hacen nada y el escritorio se queda como está. En el de modo normal es **"(volver al anterior)"**: `DarkModeAppearance::apply()` guarda el valor vivo del sistema **al entrar** en oscuro (`DockConfig::setDarkAppearancePrevious()`, claves `darkModeColorSchemePrev` / `darkModeIconThemePrev`) y lo restaura al salir. Ese snapshot está **persistido**, por lo mismo que `darkAppearanceApplied`: la restauración tiene que andar aunque el dock se reinicie con el modo puesto. Un valor explícito le gana al snapshot. En la fila del iconset **del dock** no hay ninguna de las dos: ahí el id vacío ya significa otra cosa ("sin override, seguir a KDE"), y por eso ese ítem no tiene clave `Prev`.
    - **Antes esto se hacía sembrando el combo** desde el sistema al construir la solapa, y estuvo roto todo ese tiempo: la siembra solo corría si el usuario abría la solapa con el modo apagado, y encima guardaba vacío porque `currentColorScheme()` leía la clave mal (ver `CLAUDE.md`). Resultado: al salir de oscuro el escritorio se quedaba con el esquema oscuro. Arreglado 2026-08-05 capturando al entrar, que además no depende de que nadie abra ningún diálogo.
    - Igual hace falta la entrada explícita al frente de la lista: sin ella el selector se queda en el primero por orden alfabético y conmutar el modo le aplica *ese* al usuario.
  - **Se disparan por "algún dock está en oscuro"** (`DockConfig::anyDarkModeActive()`), no por dock: un esquema de KDE no es por monitor. Con dos docks, pasar uno a normal no restaura nada hasta que el otro también lo haga.
  - **`darkAppearanceApplied` está persistido**, no es un miembro: describe el estado del *sistema*, que sobrevive al proceso. Sin él, arrancar con el modo ya prendido re-aplicaría, y arrancar con el modo apagado **saltearía el restore** y dejaría el escritorio oscuro para siempre.
  - **Una señal aparte para esto**: `DockConfig::darkModeNotifier()` (clase `DarkModeNotifier`) emite **una vez** por cambio, mientras que `darkModeChanged()` se emite por instancia. Aplicar un esquema de KDE quince veces son quince procesos `plasma-apply-colorscheme`.
  - `main.cpp` construye `DarkModeAppearance` **después** del `DockManager` y llama a `sync()` ahí: antes no existe ningún `DockConfig`, así que `anyDarkModeActive()` diría "no" y "restauraría" una sesión que arranca oscura.
- **Colores de Breeze Dark hardcodeados a propósito** (`DockConfig::kDarkAccentDefault` `#3daee9`, `kDarkBackgroundDefault` `#232629`): el esquema oscuro no se lee de `kdeglobals` en runtime, para que no se mueva bajo el dock cuando el usuario cambia su esquema de KDE. `Theme` no interviene.
- **UI**: solapa **DarkMode** (`SettingsDialog::createDarkModeTab()`) con el interruptor de este dock, el de todos los docks, los dos selectores de color con su botón "Breeze Dark", y la lista de excepciones (`QListWidget` con checkboxes sobre `DockConfig::knownDocks()`, habilitada solo cuando el alcance es "todos los docks"). Más el submenú **`qml/ModeMenu.qml`** (Normal / Dark), instanciado junto a `BackgroundColorMenu` en los dos menús contextuales, y el widget `darkmode` (clic izquierdo = Normal, clic derecho = Dark; ícono `weather-clear` / `weather-clear-night`, que es también el indicador de en qué modo está).
- **Un widget sin backend**: `darkmode` solo toca `DockConfig`, así que de los siete archivos de la convención de `CLAUDE.md` no lleva `src/<x>control.{h,cpp}`, ni `main.cpp`, ni `dockmanager.*`, ni `dockwindow.*` — quedan `DockConfig` (con su línea en **`knownWidgetTokens()`**, la que se olvida y no da error), `Dock.qml` y el checkbox de Configuración.
- **La solapa nueva desbordó la barra de solapas coloreadas**: con once solapas los títulos pedían 1086 px y el `QTabBar` caía sin avisar en modo flechas de scroll, escondiendo las últimas. El diálogo pasó de 1000 a 1120 px de ancho. Desde 2026-08-04 el problema no se puede repetir: la barra está en columna (ver *Solapas coloreadas*) y el ancho del diálogo ya no lo dicta ella.

### Clic derecho de un widget: acción vs. menú de sección
- El clic derecho sobre un widget abre por defecto el **menú de sección** (agregar separador, color de fondo, etiquetas, renombrar, Configuración). Algunos widgets se lo gastan en una segunda acción, y eso se declara en **un solo lugar** de `Dock.qml`: `sectionHasAltClick(token)` dice cuáles, y `sectionAltClick(token)` hace qué. Hoy: `volume` → mezclador (solapa Audio), `movetoscreen` → monitor anterior, `maxmin` → minimizar, `closewindow` → mandar la ventana al escritorio siguiente sin seguirla, `darkmode` → modo oscuro (el clic izquierdo pone el modo normal).
- **`Shift`+clic derecho siempre abre el menú de sección**, en todos los widgets (`secMouse.onClicked`). Es la única vía al menú en esos tres — antes el widget de volumen simplemente se quedaba sin menú.
- Un widget nuevo con acción de clic derecho se agrega tocando **solo esas dos funciones**; no hay que tocar el `MouseArea`.

### Next-wallpaper widget (`nextwallpaper` token, `WallpaperControl`)
- Advances the KDE **slideshow** wallpaper to the next image on the **monitor the dock lives on** (`config.showNextWallpaper`, `Dock.qml` calls `wallpaperControl.nextWallpaper(config.screenName)`).
- **Why not the global shortcut**: `"Slideshow Wallpaper Next Image"` (kglobalaccel) only ever advances the **primary** screen — every screen's slideshow registers the same-named shortcut and only one fires. Verified live on a 3-monitor session (only screen 0 changed).
- **Per-monitor mechanism** (`src/wallpapercontrol.cpp`): drive Plasma's scripting API `org.kde.PlasmaShell.evaluateScript`. Map `screenName`→Plasma screen index by matching `QScreen::geometry()` top-left against `screenGeometry(i)` (scanning only `0..screenCount-1`, so a disconnected containment with `screen == -1` never matches). Then, on the containment with that `.screen`: read the slideshow's `Image` + `SlidePaths` (config group `["Wallpaper","org.kde.slideshow","General"]`), compute the **next** image in C++ (enumerate the folders recursively for common image extensions, sort case-insensitively, pick the one after the current, wrapping), and write it back with `writeConfig("Image", url)` + `reloadConfig()`. Two chained async `evaluateScript` calls (read → write).
  - `d.reloadConfig()` / re-setting the plugin do **not** advance the slideshow (verified); only writing a concrete `Image` does. `evaluateScript` returns whatever the script `print()`s (not the last expression).
  - Non-slideshow wallpapers (`org.kde.image`) and empty `screenName` are no-ops / fall back to the global shortcut.
- Availability is the same static KDE-session check; `showNextWallpaper` flag + Settings → Widgets checkbox.
- **CLI / `next-wall.sh`**: the same `WallpaperControl` is reachable one-shot via `kdock --next-wallpaper [--screen NAME]` (`runNextWallpaperCli` in `main.cpp`, dispatched before any dock is built). `next-wall.sh` is a thin wrapper. Two ways it learns the current monitor: (B) `ScriptRunnersManager::run(id, screenName)` exports the launching dock's connector as `KDOCK_SCREEN` (passed from `ScriptRunnerWidget`/`Dock.qml`'s `config.screenName`) → `--screen "$KDOCK_SCREEN"`, exact and no window; (A) fallback when the var is absent (global shortcut / terminal) — the CLI opens a 1×1 probe `QWidget` (`Qt::Tool`, frameless) that the compositor maps onto the active output, then reads `windowHandle()->screen()->name()`. The one-shot process keeps its loop alive ~1.6–2 s for the chained async D-Bus calls, then quits. **Re-`ninja install` after changing this** — the wrapper's `command -v kdock` resolves the *installed* binary first.

#### `next-wall.sh` is now a standalone `qdbus6` script (does NOT use the C++ `WallpaperControl`)
- **The script itself is not in this repo**: it is a per-user Script Runner script that lives outside the tree (it hardcodes the user's monitor connectors and wallpaper directories). The notes below document the protocol it exercises, which is what matters for the C++ side.
- **What changed (2026-07-16)**: after the C++ engine proved unreliable on this KDE 6 setup, `next-wall.sh` was rewritten as a self-contained `qdbus6` + Plasma scripting script. The dock's `nextwallpaper` **widget** still uses the C++ `WallpaperControl` (reverted baseline); the **Script Runner icon** runs `next-wall.sh`, which is the path the user actually uses.
- **KEY finding (verified live, final)**: on this Plasma 6, switching a containment's `wallpaperPlugin` to `org.kde.image` and then (in a **separate** `evaluateScript` call) **back** to `org.kde.slideshow` + `reloadConfig()` makes KDE **advance the slideshow to its next image and repaint** — exactly like the desktop's right-click "Next Wallpaper". No `Image` is written by us. (Doing the toggle in one call does **not** repaint; writing the slideshow `Image` + `reloadConfig()` alone does **not** repaint either.) So the final script is just: (1) list target screens (connected, currently `org.kde.slideshow`, geometry-matched if `KDOCK_SCREEN`); (2) set them to `org.kde.image` **without** `reloadConfig()`; (3) `sleep 0`; (4) set them back to `org.kde.slideshow` + `reloadConfig()`. **Two subtleties that fixed a double-change flash**: (a) the image switch must NOT `reloadConfig()` — reloading there repaints to whatever stale image is stored in the `org.kde.image` group first (a visible extra change); (b) `sleep 0` is enough (the two separate D-Bus calls already serialize). Result: a single visible advance, slideshow mode preserved. The slideshow config (folder/interval/order) is never touched. **This replaced an earlier version that read `SlidePaths` and computed the next image in bash — no longer needed.**
- **Targeting**: `KDOCK_SCREEN` set (dock icon) → only that monitor, mapping connector→Plasma screen index by geometry via `kscreen-doctor --json` (pos) matched against `screenGeometry(i)`. No var (console) → every connected screen currently in `org.kde.slideshow`.
- **Gotchas**: use `qdbus6` (there is no `qdbus`); run it with `bash`/`./` not `sh` (Ubuntu `sh` is dash → the bash arrays / `<(...)` / `<<<` fail). `ScriptRunnersManager::run()` executes an **executable** script directly (honoring its shebang), else falls back to `sh <path>`.

### Wallpapers por escritorio virtual (`src/desktopwallpapers.{h,cpp}`, 2026-08-06)

- **Por qué hace falta**: en Plasma el *containment* del escritorio es **por pantalla, no por
  escritorio virtual** (verificado en vivo: con 2 monitores hay 2 containments con `screen>=0`,
  más los huérfanos con `screen=-1` de monitores viejos). O sea que todos los escritorios
  comparten un fondo y la única salida es **reescribirlo en el momento del cambio**.
- **El reparto es asimétrico a propósito**:
  - **El Escritorio 1 es de KDE.** Lo que el usuario haya configurado ahí (acá, un slideshow
    por monitor) se *fotografía* antes de tocar nada y se restaura cada vez que vuelve, al
    salir del proceso, y al arrancar si una corrida anterior murió con nuestro fondo puesto.
    Desde kdock **no se edita**: la solapa lo muestra en modo lectura.
  - **Los escritorios 2..`DockConfig::kMaxDesktops`** tienen un **modo** cada uno, elegido en
    la solapa con dos checkboxes excluyentes (Slideshow / Estático):
    - **Estático** (el comportamiento original): una imagen por monitor, con clave el nombre
      del conector (`eDP-1`, `DP-1`…), aplicada como `org.kde.image` con el `FillMode`
      global. Un monitor **sin imagen configurada no se toca**.
    - **Slideshow**: cada monitor con carpeta configurada corre el plugin `org.kde.slideshow`
      de KDE enraizado en esa carpeta (clave `slideshow_<conector>`), avanzando cada
      `slideshowInterval` segundos (clave `slideshowInterval`, por escritorio, default 300 =
      5 min). Un monitor **sin carpeta no se toca**. El `FillMode` global no aplica acá.
  - Escritorios más allá del tope tampoco se tocan: el usuario nunca dijo qué poner ahí.
- **Apagado por defecto** (`enabled` arranca en false). No es cosmético: `VirtualDesktops` sale
  de D-Bus, así que **un proceso bajo Xvfb ve los escritorios reales** y reaccionaría a los
  cambios del usuario — la reja es que la config vive en el `kdock.conf` compartido y el arnés
  siempre usa un `XDG_DATA_HOME` descartable.
- **Snapshot genérico y persistido**: la captura sale de un `evaluateScript` que recorre los
  containments y lee **`d.configKeys`** del grupo `["Wallpaper", <plugin>, "General"]`, o sea
  que no hay ninguna clave hardcodeada y cualquier plugin (slideshow, `org.kde.image`, otro)
  hace round-trip. Se guarda como JSON en una sola clave (`[Wallpapers] snapshot=`) del
  `kdock.conf` compartido: una clave sola evita pelearse con el escapado de `QSettings` para
  rutas y nombres de clave arbitrarios, y de paso entra sola en el Backup. **Se fusiona, nunca
  se reemplaza**: la entrada de un monitor desenchufado hoy tiene que seguir estando cuando
  vuelva.
- **Dos rejas para no fotografiar nuestro propio fondo** (que sería perder la config del
  usuario para siempre): solo se captura con `applied == false`, y se descarta la captura de un
  monitor cuyo plugin sea `org.kde.image` **y** cuya `Image` coincida con alguna de las
  imágenes configuradas en `[Wallpapers2]`..`[Wallpapers<kMaxDesktops>]`, o cuyo plugin sea
  `org.kde.slideshow` **y** cuyo `SlidePaths` coincida con alguna de las carpetas de slideshow
  de los escritorios 2..`kMaxDesktops` (el guard del slideshow se agregó junto con el modo).
- **Repintar exige DOS `evaluateScript` separados**, uno que escribe y otro que hace
  `reloadConfig()` — la misma regla del hallazgo de `next-wall.sh` de más arriba. **Verificado
  en vivo el 2026-08-06 incluyendo el caso que faltaba**: de escritorio 2 a 3 el plugin
  **no cambia** (`org.kde.image` → `org.kde.image`) y aun así repinta. La única forma de *ver*
  el fondo es desde un escritorio virtual vacío: en el escritorio de trabajo las ventanas lo
  tapan, y el containment **no es un toplevel**, así que `kdock-previews --dump-captures` no lo
  enumera.
- **Al restaurar un slideshow, la imagen concreta avanza una** (el plugin, al recargar, pasa a
  la siguiente). La *configuración* —plugin, carpetas, intervalo— vuelve idéntica, que es lo que
  importa; un slideshow avanza solo de todos modos.
- **Un slideshow de escritorio N re-aplicado sobre un containment que ya corre
  `org.kde.slideshow` REPITE la imagen** (bug 2026-08-06): el plugin conserva su clave `Image`,
  así que escribir el mismo `SlidePaths` + `reloadConfig()` vuelve a mostrar el mismo cuadro —
  exactamente lo que pasa de escritorio 2 a 3 cuando ambos usan la misma carpeta. La salida es
  la del hallazgo de `next-wall.sh`: `apply()` de un escritorio slideshow hace **TRES** llamadas
  separadas — (1) `toggleToImageScript()` cambia el plugin de los monitores configurados a
  `org.kde.image` **sin reload** (el switch solo no repinta, así que no hay flash), (2) `applyScript()`
  lo vuelve a `org.kde.slideshow` escribiendo `SlidePaths`/`SlideInterval`, (3) `reloadConfig()`.
  El cambio de plugin fuerza el avance. La llamada (1) es la que hace que (2) se registre como
  *cambio de plugin* y no como re-aplicación. `toggleToImageScript()` toca los mismos monitores
  que `applyScript()` (los que tienen carpeta para ese escritorio); el resto no se toca.
- **Config** (`kdock.conf` compartido, estáticos al estilo `DockConfig::favoritesShared()`):
  `[Wallpapers] enabled/fillMode/snapshot/applied` y **una sección por escritorio**,
  `[Wallpapers2]`..`[Wallpapers5]`, con el conector como clave y las tres claves del modo
  slideshow (`slideshowMode`, `slideshowInterval`, `slideshow_<conector>`). Una sección por
  escritorio y no `desktop2/eDP-1` porque `QSettings` solo mapea el **primer** `/` a la sección.
- **`SIGTERM` no dispara `aboutToQuit`** — ver la trampa en `CLAUDE.md`. `main.cpp` instala un
  handler (socketpair + `QSocketNotifier`) que restaura y sale con `::_exit(0)` **sin
  desenrollar**: un `quit()` limpio destruye los `QQmlEngine` con los bindings vivos y escupe
  ~1800 *"property of null"* al journal en cada cierre de sesión.
- **Solapa *Wallpapers*** (`SettingsDialog::createWallpapersTab()`): no es por dock (como Redes
  y Audio). Interruptor + combo de `FillMode` (nota: "(imágenes estáticas)" — el slideshow
  tiene su propio render), el grupo de solo lectura del Escritorio 1 con *Volver a capturar
  ahora* (habilitado **solo desde el Escritorio 1**) y *Restaurar ahora*, y un `QGroupBox` por
  escritorio (2..`kMaxDesktops`) con **dos checkboxes excluyentes**: *Slideshow* (con un
  `QSpinBox` de intervalo en segundos y una fila por monitor para elegir carpeta — el picker
  es `getExistingDirectory()`, la miniatura un ícono `folder-pictures`) y *Estático* (las
  filas de imagen de siempre). **Exactamente un modo está activo por escritorio**: tildar uno
  destilda el otro (`refreshModes()`), y el cuerpo del modo inactivo se oculta para no alargar
  el scroll — los dos checkboxes quedan siempre visibles para poder cambiar de modo. El
  `intervalSpin` solo se ve en modo slideshow. Fila de monitor compacta a propósito (con el
  tope de 5 escritorios × 5 monitores el scroll se vuelve profundo): miniatura 64×36 en vez
  de 96×54 y `QToolButton`s con solo ícono (`document-open`/`edit-clear`) en vez de botones
  con texto. Y *Aplicar ahora*. El *Elegir…* de Estático usa `QFileDialog::getOpenFileName()`
  **nativo**: en una sesión Plasma el `QPlatformTheme` de *plasma-integration* sirve el
  diálogo de KDE, que **previsualiza imágenes**, sin que kdock dependa de KDE Frameworks. La
  miniatura se decodifica con `QImageReader::setScaledSize()`, no cargando el archivo entero.

### Clock2 (`src/clockwidget2.cpp` + `clock2Comp` in `Dock.qml`)
- A second clock widget (token `clock2`, flag `config.showClock2`) that shows the time like `clock` but with a **larger custom tooltip**: gray `#404040` rounded background, time in yellow `#FFD700` 16px bold, date below in white. Its on-dock font sizes are ~30% larger than `clock` (`iconSize*0.455` / `*0.26`). Bound to the same per-monitor clock format settings as `clock`.
- **Font size (both clocks)**: `config.clockFontSize` (px, `0` = automatic → the historic factors `iconSize*0.35` for `clock` and `*0.455` for `clock2`; the date line derives from it with ratio `0.57`). Set in Settings → Widgets → "Clock font size" (`setSpecialValueText("Automatic")`). The widget's `Item` grows with the font via `Math.max(widgetIconSize, column.implicitWidth)`, so a large font does not get clipped.
- **Text color**: `root.clockTextColor`, derived from the dock background's luminance — see the `dockBaseIsLight` bullet in the **Theme** section.
- **Clic = comando configurable y alternable** (`config.clock2Command`, Settings → Widgets → "Clock click app"): el clic izquierdo llama a `ClockWidget2::launch()`, que acepta **cuatro formas** — un id de `.desktop` (`kdock-calendar`), un comando en `PATH`, un path completo a binario, o un path completo a un archivo `.desktop` (`/usr/share/applications/orage.desktop`, parseado con `DesktopEntryIndex::fromFile`). Resolución en ese orden (ver `launch()`): `.desktop` por ruta → binario por ruta → `QStandardPaths::findExecutable()` → `DesktopEntryIndex::byId()` (el index se alimenta en el arranque, así que un `.desktop` instalado después exige reiniciar el dock) → nombre pelado como último recurso. El index le llega por `ClockWidget2::setApps()` desde `DockManager::buildInstance` (`m_shared.apps`).
- **Segundo clic = cerrar.** `launch()` es un **toggle**: el primer clic arranca la app como proceso hijo rastreado (`m_proc`, un `QProcess` por widget, vía `startTracked()`/`startTrackedExec()`), y si esa instancia sigue viva el segundo clic la cierra con `terminate()` (SIGTERM; `kill()` de respaldo a los 2,5 s para las que lo ignoren) — **sin minimizar ni ocultar**. Es lo que permite abrir/cerrar `kdock-calendar` desde el reloj. Como la app se lanza como hijo (no `startDetached`), si la cerraste a mano el estado vuelve a `NotRunning` y el clic la vuelve a abrir.
- **Botones de la fila**: **"Apps…"** abre el picker de aplicaciones instaladas del menu editor (`QInputDialog::getItem` sobre `m_apps->all()`) y llena el box con el **id** del `.desktop` elegido; **"File…"** abre un `QFileDialog` con filtros `Desktop entries (*.desktop)` y `Executables (*)` y llena el box con el **path completo**. `setClock2Command()` hace `m_settings.sync()` inmediato (2026-08-05): QSettings difiere la escritura a disco y un reinicio justo después de guardar podía volver a leer el comando viejo.

### Clipboard history (`src/clipboardhistory.cpp` + `qml/ClipboardPopup.qml`)
- Text-only clipboard manager, exposed to QML as `clipboardHistory` context property. Token `"clipboard"`, gated by `config.showClipboard` (default **off**, opt-in). A **drop-only anchor** block (owns its own mouse handling, popup and context menu), like `menu`/`apps`. Icon `edit-paste`.
- **Shared singleton**: `ClipboardHistory` is a system-wide singleton created in `main.cpp`, packed into `DockManager::Shared`, and passed to every `DockWindow` → one global history shared by all docks/monitors.
- **Capture (`ext-data-control-v1`)**: `WaylandClipboard` (`src/waylandclipboard.{h,cpp}`, built from `protocols/ext-data-control-v1.xml` through `qt_generate_wayland_protocol_client_sources`) binds `ext_data_control_manager_v1` and gets a device for the seat (`QNativeInterface::QWaylandApplication::seat()`). That protocol is **focus-independent** — it is what clipboard managers use — so the history now fills while the dock has no keyboard focus at all, which is the whole point (see the caveat entry below, now resolved). It is **not** on KWin's restricted-interface list (`kwin/src/wayland_server.cpp`), so nothing is declared in `kdock.desktop` and ksycoca is not involved. `QClipboard::dataChanged` stays wired as the fallback for X11/Xvfb and is skipped whenever the Wayland backend is active. Text and images are kept, deduplicated (an existing copy moves to the top), capped at `kMaxEntries` (50) plus `kMaxImages` (20) and 8 MB per image. An offer that advertises `x-kde-passwordManagerHint` (Klipper/KWallet's "this is a password") is ignored.
- **Transfers are asynchronous, in both directions**: reading is `pipe2` + `offer->receive()` + `wl_display_flush()` + a `QSocketNotifier(Read)` until EOF with a 5 s watchdog; serving a paste is `O_NONBLOCK` + `QSocketNotifier(Write)` in chunks. A blocking `read()`/`write()` on the GUI thread would freeze the dock whenever the app on the other side is slow (a multi-MB screenshot does not fit in the 64 KB pipe buffer). `SIGPIPE` is ignored process-wide because the reader can vanish mid-transfer. While kdock owns the selection its own `DataControlSource` is alive, and the selection event the compositor echoes back is ignored — reading it would mean reading from our own process.
- **`setClipboard(text)` / `setClipboardImage(file)`** (used when the user clicks a row) take the selection through the same protocol, so pasting a history entry works without the dock ever taking focus; on the fallback path they go through `QClipboard` and `m_ownText` skips the echo.
- **Persistence** (survives full shutdowns): the storage is a JSON index at `~/.local/share/kdock/clipboard-history.json` (ordered `{type:"text",text}` / `{type:"image",file,w,h}`) plus one PNG per image entry under `~/.local/share/kdock/clipboard-images/`, named `img-<sha1>.png` — the hash is the dedup key, and a PNG arriving as PNG is stored verbatim (only other formats are transcoded, and only the header is decoded to get the size). `clipboard-history.txt` is still written on every save as a **derived, human-readable export** (`=== kdock clipboard entry ===` delimiters, images as `[Imagen 1920 × 1080 — clipboard-images/…]`), which is what *Ver historial actual* and *Guardar historial* use. A history from before the index existed is migrated from that `.txt` on first load; PNGs no entry refers to are swept on load and on `clearHistory()`.
- **Left click** = `ClipboardPopup` (modeled on `AppMenuPopup`, same modal `Popup.Window` + `dockWindow.setKeyboardInteractive` + `focusRetry` keyboard dance): a search field on top and a scrollable list (~10 rows visible). Search filters case-insensitively (`entries(query)`). Clicking a row copies it to the active clipboard and closes. Size is `config.clipboardPopupWidth`/`clipboardPopupHeight` (Settings → Widgets).
- **Right click** = `Menu` with three `IconMenuItem`s: **Borrar Historial** (`clearHistory()`), **Ver historial actual** (`openInEditor()` → `QDesktopServices::openUrl` the plain-text export in the default editor), **Guardar historial** (`saveHistoryDialog()` → `QFileDialog::getSaveFileName` then export), plus a checkable **Guardar imágenes** bound to `clipboardHistory.captureImages` (stored in `QSettings` under `clipboard/captureImages`, like `audio/maxVolume`: `ClipboardHistory` is a shared singleton, so it does not belong in the per-dock `DockConfig`).
- **Wayland caveat — resolved (2026-08-04)**: an app may only read `wl_data_device` while it holds keyboard focus, and the dock's layer surface is keyboard-inert, so background capture through `QClipboard` was dead (the history only filled while a popup held the grab). Fixed with data-control, above. Note the protocol is **`ext-data-control-v1`, not `wlr-data-control-unstable-v1`**: KWin renamed its implementation (`kwin/src/wayland/datacontroldevicemanager_v1.cpp` implements `QtWaylandServer::ext_data_control_manager_v1`) and no longer advertises the `zwlr_` global at all, and KDE's own KSystemClipboard moved with it. Verified live: text and a 1.8 MB screenshot captured with the probe having no window and no focus, and `qdbus6 org.kde.klipper /klipper getClipboardContents` returning what kdock put on the selection.

### Dock Preview — binario accesorio `kdock-previews` (`previews/`)

Tira de **vistas previas de ventanas** en un borde de pantalla (referencia: el Stage
Manager de macOS): una tarjeta por ventana con una captura real de su
contenido, clic para activarla. **No es parte de kdock**: es un segundo binario, con su
propio árbol (`previews/`), su propia config y su propio multimonitor, que corre *junto* al
dock. kdock solo lo prende, lo apaga y le abre su panel.

- **Clic en una tarjeta** = toggle de foco como la barra de tareas
  (`PreviewModel::activate`): si la ventana ya está en primer plano (y no minimizada), el
  clic la **minimiza**; si está minimizada o detrás, la restaura y enfoca
  (`KWinWindow::activate()` ya desminimiza primero). Clic medio = cerrar la ventana.

- **Target aparte** (`previews/CMakeLists.txt`, incluido con `add_subdirectory(previews)`).
  Reusa sin modificar `src/layershell.{cpp,h}`, `src/desktopentry.{cpp,h}`,
  `src/iconprovider.{cpp,h}`, `src/theme.{cpp,h}` y `src/dockconfig.{cpp,h}`, listándolos en
  el target nuevo (se compilan dos veces; el target `kdock` **no cambia en nada**).
  `dockconfig.cpp` está ahí solo porque `theme.cpp` le pide
  `DockConfig::settingsFilePath()` para leer el override de iconset — así los dos docks
  comparten archivo y tema sin duplicar una línea. `xdg-shell.xml` **sí** hace falta al
  generar los protocolos: el código C de layer-shell referencia `xdg_popup_interface`.
- **Autorización, lo que hace o rompe todo**: `previews/kdock-previews.desktop.in` declara
  **dos** claves y KWin las resuelve contra `/proc/<pid>/exe` → `Exec=` (vía
  KApplicationTrader/ksycoca):
  `X-KDE-Wayland-Interfaces=org_kde_plasma_window_management` (lista de ventanas) y
  `X-KDE-DBUS-Restricted-Interfaces=org.kde.KWin.ScreenShot2` (capturas). Sin la segunda,
  cada captura vuelve como `org.kde.KWin.ScreenShot2.Error.NoAuthorized` y las tarjetas caen
  a ícono + título. **Para desarrollar** hace falta un `.desktop` temporal en
  `~/.local/share/applications/` con `Exec=` a la ruta absoluta del binario del build y las
  dos claves, más el refresco de ksycoca (ver `CLAUDE.md` → *Build*: en esta máquina hay que
  invocarlo con ruta y `XDG_DATA_DIRS` explícitos, `kbuildsycoca6` pelado rompe los "abrir
  con" de las apps de Ubuntu). Ojo: la ruta del repo tiene un `#`, que KConfig no parsea
  bien en `Exec=` — copiar el binario a un directorio sin `#` (p. ej. `/tmp/…`) y apuntar
  ahí. Borrar el `.desktop` al terminar (deja privilegio de captura a lo que haya en esa
  ruta).
- **Pipeline de miniaturas** (`thumbnailsource.h` → `screenshotsource.cpp` →
  `thumbnailcache.cpp` → `thumbnailimageprovider.cpp`): `CaptureWindow(handle, opciones,
  pipe)` donde **`handle` es el uuid de `window_with_uuid`** (verificado en KWin 6.7.4). KWin
  contesta con `width/height/stride/format` y escribe los píxeles en el pipe; el pipe son 64
  KB y una ventana 4K son ~33 MB, así que **KWin se bloquea escribiendo**: la lectura va en
  un `QSocketNotifier` (sin hilo, sin módulo extra, sin carrera con el reply) y la imagen se
  arma cuando llegaron las dos mitades. Se escala **al llegar** y se guarda solo la copia
  escalada. `image://thumb/<thumbId>@<rev>` sirve la caché con el mismo truco de revisión que
  `image://systray/…`; la caché tiene mutex porque `requestImage` corre en el hilo de
  imágenes de QtQuick.
- **El uuid no se puede poner en una URL** (bug 2026-07-30, dos rondas de diagnóstico): KWin
  lo manda con llaves (`{461eb7ac-…}`) y **`QUrl` las percent-codifica**, así que el provider
  recibía `%7B461eb7ac-…%7D`, la búsqueda en la caché fallaba y **la tarjeta quedaba vacía
  aunque la captura hubiera salido perfecta**. El síntoma engaña: el badge del ícono sí se
  dibuja (depende de `thumbRevision > 0`, que estaba bien), así que parece un problema de
  render o de permisos de captura cuando es una clave que no coincide. Tres defensas: el rol
  **`thumbId`** (uuid sin llaves) es lo único que QML pone en la URL,
  `ThumbnailCache::normalizeKey()` percent-decodifica y saca las llaves en los dos extremos, y
  un *miss* del provider avisa por stderr una vez por proceso en vez de degradar en silencio.
- **Una captura en vuelo para todo el proceso** (KWin re-renderiza la ventana en cada una).
  Cuándo se captura lo decide `config.captureMode` (`PreviewConfig::CaptureMode`), en
  `PreviewModel`:
    - **`OnceOnFocus` (0, default)**: **una sola captura por ventana**, la primera vez que
      pasa a primer plano con la tira corriendo, y nunca más. Sin timer, sin refresco, y el
      hover no captura (`refreshNow()` es no-op): una ventana que *no* está al frente es
      justamente la que no se puede capturar con confianza. La ventana que ya estaba activa
      cuando arranca la tira se captura al insertarse la fila; las demás muestran el ícono de
      la app hasta que el usuario las trae al frente. La captura sale **400 ms después** de
      la activación: KWin marca la ventana activa *antes* de que termine de repintar, y en el
      mismo tick se agarra el frame viejo. Si falla, la revisión queda en 0 y se reintenta en
      el próximo foco — acotado, a diferencia de un reintento por timer.
    - **`Periodic` (1)**: el round-robin **solo sobre las filas visibles**
      (`setVisibleRange` desde el `ListView`), la activa cada `activeRefreshInterval`
      (1500 ms) y el resto cada `refreshInterval` (4000 ms), captura inmediata al aparecer una
      tarjeta / activarse la ventana / pasar el mouse, y **pausa total** mientras la tira está
      oculta por autohide. Es el camino que queda para el trabajo de preview en vivo; está
      **apagado por defecto**. En el panel, los dos spinbox de intervalo se deshabilitan
      cuando no es este modo.
  En los dos casos el *timestamp* se marca al **pedir**, no al recibir, así una ventana que no
  se puede capturar no entra en loop. Una captura fallida se avisa por stderr **una vez por
  ventana** (`m_captureFailWarned`): una tarjeta que se queda con el ícono es indistinguible
  de un bug de render si no lo dice.
- **Ventanas minimizadas**: se capturan igual, con contenido completo (verificado
  2026-07-30); KWin las re-renderiza aunque no estén visibles.
- **Scroll rápido**: el `ListView` de la tira usa `flickDeceleration: 800` y
  `maximumFlickVelocity: 6000` (más inercia y velocidad al soltar un arrastre). **No** se
  toca el `mouseWheelVelocity` del `Flickable`: no existe en el Qt del sistema (6.10.2;
  llegó más tarde) y asignarlo hacía fallar la carga del QML (bug 2026-07-31).
- **Rueda del mouse en tiras horizontales** (bug 2026-07-31, cerrado): Qt 6 **no cruza ejes**
  en `QQuickFlickable::wheelEvent` — el `angleDelta().y` del wheel normal solo mueve si
  `yflick()` (contenido vertical desbordante) y el `.x` solo si `xflick()`. Una tira
  horizontal (edge 0/1) no desborda verticalmente (`contentHeight == height`), así que el
  wheel vertical se ignoraba y no había forma de avanzar con el mouse (el arrastre sí
  funcionaba). **Un `WheelHandler` hermano del `ListView` no lo arregla**: la entrega recorre
  los items de arriba hacia abajo en el orden de apilado, y el `ListView` —que está *dentro*
  de `slider`, o sea más arriba— se queda el evento antes de que se consulten los handlers
  del padre. Verificado con logging: el handler se habilitaba (`enabled=true`) y `onWheel`
  no disparaba nunca.
  La solución es un **`MouseArea` por encima del `ListView`** (último hijo de `slider`, así
  queda topmost y ve el wheel primero) con `acceptedButtons: Qt.NoButton`, que deja pasar los
  press —clic en tarjeta, arrastre para flickear, menú de clic derecho— y no toca el hover del
  autohide (`hoverEnabled` queda en false). En las tiras verticales pone `wheel.accepted = false`
  y el evento sigue al `ListView`, que scrollea nativo con su propio timeline. En las
  horizontales escribe `contentX` con un `NumberAnimation` de 150 ms; como escribir `contentX`
  a mano **saltea el `boundsBehavior`**, el clamp a `[0, contentWidth - width]` es explícito, y
  el destino se **acumula** entre muescas (`scrollTarget`) para que un giro rápido no se coma
  a sí mismo. Wheel arriba == tarjetas anteriores, igual que la vertical.
  - **Trampa (mordió 2026-07-31)**: un `WheelHandler` es un **PointerHandler, no un
    `Item`**: **no tiene `anchors`**. Asignarle `anchors.fill:` falla con *"Cannot assign to
    non-existent property anchors"* y **rompe la carga de todo el QML** — la tira queda
    vacía (solo el espacio reservado, el escritorio de atrás recibe los clics).
  - **Trampa de diagnóstico**: el `kdock-previews` que corre en la sesión **no manda su stderr
    al journal** (lo lanza `previewslauncher` / el D-Bus, no una unidad); mirá
    `/proc/<pid>/fd/2` para saber a dónde va (en esta máquina, `/tmp/previews-err.log`). Se
    perdió una ronda entera buscando los `console.log` en `journalctl`.
- **Barra de desplazamiento fina opcional** (`config.showScrollBar`, default **off**):
  un indicador de 3 px en el **borde interior** de la tira (opuesto al borde de pantalla:
  edge 0→arriba, 1→abajo, 2→derecha, 3→izquierda), para no pisar el hover del auto-hide.
  Solo aparece cuando el contenido desborda (`contentWidth > width` en horizontal,
  `contentHeight > height` en vertical); el handle sale de `list.visibleArea.{x,y}Position/
  {width,height}Ratio` y es arrastrable (MouseArea → `contentX`/`contentY`). No se usa el
  `ScrollBar` attached: `QQuickFlickableScrollBar` lo clava al borde inferior/derecho del
  viewport, donde taparía las tarjetas (que llenan el viewport). La barra vive en la banda
  de padding y no estorba el wheel: los eventos sin aceptar sobre su MouseArea propagan al
  `slider` y los agarra el `WheelHandler` de arriba.
- **Auto-fit de tarjetas** (`config.autoFitCards`, default on, + `config.fitMinCardWidth`,
  default 96 px): con muchas ventanas las tarjetas se achican para entrar en el largo de la
  tira en vez de desbordar a scroll, y vuelven al tamaño configurado al quedar pocas. El
  **grosor de la tira y la zona exclusiva acompañan a las tarjetas** (bug 2026-07-31): con el
  grosor fijo y las tarjetas encogidas la tira mostraba una banda muerta a cada lado; ahora
  `PreviewModel::effectiveThicknessPx` = `round(cardWidthPx * cardScale) + 2*pad` lo alimenta
  todo — el QML (grosor del root, que re-dirige el tamaño de la superficie) y
  `PreviewWindow::thickness()` (zona exclusiva, reaplicada vía
  `effectiveThicknessChanged` → `applyLayerProperties`). A escala completa coincide con
  `stripThicknessPx`. Sin bucle: el largo útil (eje principal) no depende del grosor, y la
  realimentación vía geometría de ventanas maximizadas es contractiva (factor 1/n, n ≥ 2).
  - La escala la calcula **`PreviewModel::cardScale`** (Q_PROPERTY qreal, 1.0 = tamaño
    configurado), no el QML: el modelo conoce las filas y sus `aspect`. El largo útil lo
    reporta el QML con `previews.setAvailableLength(px)` (el width/height del `ListView`,
    ya sin padding) desde `onWidthChanged`/`onHeightChanged`/`onOrientationChanged`. Sin
    bucle: el largo disponible no depende del tamaño de tarjeta.
  - `recomputeCardScale()` ajusta `cross` por iteraciones (≤10, factor `avail/need × 0.99`
    como el `fitScale` del dock) hasta que `mainAxisNeeded(cross) ≤ available`, acotado a
    `[fitMinCardWidth, cardWidthPx]`. `mainAxisNeeded()` replica **exactamente** la
    geometría de `PreviewCard.qml` (pisos de 32/24 px, `aspect` real, título 16 px,
    `cardSpacing`), para que el fit sea exacto y no aproximado.
  - Se recalcula en `sync()` (filas/geometría) y ante cambios de `edge`/`stripThickness`/
    `cardSpacing`/`showTitles`/`autoFitCards`/`fitMinCardWidth`. `captureTarget()` usa el
    tamaño escalado ⇒ con muchas ventanas las miniaturas cacheadas son más chicas (menos RAM).
  - En el panel: checkbox "Ajustar el tamaño de las tarjetas al contenido" + spinbox
    "Tamaño mínimo de tarjeta" (habilitado solo con el checkbox), en el grupo Miniaturas.
- **El delegate es un wrapper que reenvía los roles** (bug 2026-07-31): ListView inyecta los
  roles del modelo solo en el *root* del delegate, así que el `Item` que envuelve a
  `PreviewCard` (para centrar la tarjeta encogida) debe declarar los `required property` y
  reasignárselos a la tarjeta. Sin el reenvío, cada fila falla con
  `Required property uuid was not initialized` y la tira queda vacía (parece un problema de
  render o autorización, y es el delegate).

- **Fuente de ventanas propia** (`previews/src/kwinwindows.cpp`), KWin-only, en vez de
  generalizar `WindowMonitor`/`PlasmaWindow`: necesita uuid + `geometry` + escritorios
  virtuales, y agregar el evento `geometry` al `PlasmaWindow` compartido haría que
  `emit changed()` se dispare en cada movimiento de ventana → `DockModel` reconstruyendo de
  más. Las señales son granulares a propósito (`metadataChanged` / `stateChanged` /
  `geometryChanged` / `desktopsChanged`).
- **Filtros** (por tira): monitor (el `QScreen` que contiene el centro de la geometría),
  escritorio virtual actual (`VirtualDesktops` lee `current` de
  `org.kde.KWin /VirtualDesktopManager`; si KWin no contesta **no** se filtra, para no vaciar
  la tira) y minimizadas sí/no. Las minimizadas se dibujan atenuadas.
- **Ventana y config**: `PreviewWindow` espeja la plomería de `DockWindow` (anchors por
  borde, `applyScreen()`/`scheduleApplyScreen()` con el `wl_output` resuelto vía
  `QWaylandScreen`, máscara de input de 3 px para autohide). **Grosor: una sola fórmula**,
  `PreviewConfig::stripThicknessPx()` (px, no %, justamente para no depender del tamaño de
  pantalla) — la lee el QML *y* la zona exclusiva. Rango
  `PreviewConfig::kMinThickness`..`kMaxThickness` (**48..800 px**), las mismas constantes que
  usa el spinbox del panel, así el clamp y el widget no pueden discrepar. El piso de 48 es
  geométrico: menos los `2*pad` deja la tarjeta en su mínimo de 32 px. En el panel el spinbox
  se etiqueta **según el borde** — *"Alto del panel"* en Abajo/Arriba, *"Ancho del panel"* en
  Izquierda/Derecha (`updateThicknessLabel()`, atada al combo de borde, no al config, para que
  cambie en el acto): con el nombre fijo anterior el control existía pero en horizontal nadie
  lo encontraba. `reserveSpace` y `autohide` son independientes, pero una tira oculta nunca
  reserva. Config en `~/.local/share/kdock/previews.conf` (compartida: `enabled`,
  `enabledScreens`, `knownScreens`) + `previews-<screen>.conf` por tira; `PreviewManager`
  replica el `sync()` + `migrateFirstRun()` de `DockManager` (adopta el monitor primario en el
  primer arranque).
- **Alineación, dos caminos según el largo** (bug 2026-07-31): con `stripLength > 0` la ubica
  el compositor —`applyLayerProperties()` ancla el borde más una esquina (Inicio/Fin) o solo
  el borde (Centro)— y eso siempre anduvo. Con `stripLength = 0` la superficie está anclada a
  **los dos** lados opuestos, el compositor la estira de punta a punta y no hay nada que
  alinear: las tarjetas quedaban pegadas al inicio y el combo parecía roto. Ahora el QML
  alinea el **contenido** (`PreviewStrip.qml` → `root.alignOffset`, aplicado como
  `leftMargin`/`topMargin` del `ListView`), el mismo reparto que hace `Dock.qml` en modo panel.
  Se usa el margen del `Flickable` y no la geometría del `ListView` a propósito: el margen no
  toca `contentWidth`/`contentHeight` ni el `width` que el auto-fit reporta con
  `setAvailableLength()`, así que **no hay bucle** entre alinear y encoger las tarjetas. Si el
  contenido desborda, `alignOffset` es 0 (hay scroll: no hay nada que alinear).
  Verificado con capturas reales en los dos ejes y los tres valores. Con el switch maestro
  `enabled=false` no se muestra ninguna tira, así
  `--settings` abre el panel sin poner nada en pantalla.
- **Integración desde kdock** (todo el acoplamiento, y es chico): `src/previewslauncher.cpp`
  (encuentra el binario, lee/escribe `enabled`, arranca/para vía `org.kdock.Previews`),
  la solapa **Settings → Previews** (`createPreviewsTab()`, solo si el binario está
  instalado) y tres líneas en `main.cpp` (`startIfEnabled()`). El proceso se controla por
  D-Bus: `org.kdock.Previews` en `/Previews` con `showSettings()`, `reload()`, `quit()` —
  y ser dueño del nombre del bus **es** el candado de instancia única.
- **Diagnóstico**: `kdock-previews --dump-captures <dir>` enumera las ventanas que reporta el
  compositor (uuid, app_id, flags, geometría, título), pide una captura de cada una, las
  escribe como PNG y sale. Es la forma más corta de probar el camino ScreenShot2 completo
  (autorización incluida) sin UI de por medio: sin archivos + `NoAuthorized` = falta el
  `.desktop`.

### Menú de mosaicos — binario accesorio `kdock-tilemenu` (`tilemenu/`)

Menú de aplicaciones **a pantalla completa** con los íconos en una matriz que el usuario
reacomoda arrastrando (referencia: el applet `plasma-applet-tiledmenu-prime`). **No es parte
de kdock**: es un tercer binario, con su propio árbol, su propia config y su propio panel de
ajustes, que el widget `tilemenu` prende y apaga. Mismo reparto que `kdock-previews`.

- **La ventana es un toplevel normal *maximizado*, no una superficie layer-shell.** Es la
  decisión que define el binario: KWin ya resta los *struts* de las superficies layer-shell al
  calcular el área de maximización, así que "maximizado" **es** "todo el escritorio menos los
  docks y paneles visibles", para cualquier borde, cualquier cantidad de docks y también los
  paneles de Plasma — sin una línea de geometría nuestra. Un dock en **autohide** no reserva
  nada, así que el menú cubre toda la pantalla y el dock (capa `top`, por encima de las
  ventanas normales) se dibuja encima al revelarse: tampoco hay caso especial. Y el foco de
  teclado es el de una ventana común, sin el baile de `keyboard_interactivity` double-buffered
  que necesita `AppMenuPopup`.
  - Por eso este target **no compila `src/layershell.cpp`** ni setea
    `QT_WAYLAND_SHELL_INTEGRATION` (lo *desetea* si lo hereda con el valor de kdock, que apunta
    a un plugin estático que solo existe dentro del binario de kdock).
  - **No pide ningún privilegio de KWin** (no usa `org_kde_plasma_window_management` ni
    `ScreenShot2`), así que se corre directo desde `build/` y su `.desktop` no necesita
    refrescar ksycoca. Ese `.desktop` existe solo para darle nombre e ícono en el gestor de
    tareas, donde aparece cuando está puesto *Mantener abierto*.
  - **En Wayland el cliente no elige el monitor de un toplevel.** `TileWindow::showOn()` aplica
    `setScreen()` con la ventana abajo, pero es una *pista*: en la práctica KWin la coloca en
    la pantalla activa, que es donde está el puntero que acaba de hacer clic en el dock. El
    `--screen` del D-Bus existe para eso.
  - `setGeometry(availableGeometry())` acompaña al `showMaximized()`. En Wayland manda el
    `configure` del compositor y se ignora; es lo que hace que la ventana llene la pantalla
    **bajo un X pelado sin gestor de ventanas** (el arnés de Xvfb), donde `showMaximized()` no
    tiene quién lo respete y la vista saldría de 160x160.

- **El motor de disposición vive entero en C++** (`tilemenu/src/tilelayout.cpp`). El QML solo
  dibuja y arrastra: cada soltada pregunta y después obedece. Es lo que hace la feature
  testeable — `--dump-layout` y una sonda de consola ejercitan auto-placement, colisiones,
  swaps, resizes y grupos sin ninguna ventana en pantalla.
  - **Una disposición por sección**, con las claves de `AppMenu::sections()`
    (`cat:__favorites__`, `cat:Internet`, `menu:Web/Apps`…). Una sección que nadie tocó no
    tiene ningún registro y se acomoda sola, en el orden que devuelve `AppMenu`; el primer
    arrastre la **materializa** (se anota tal cual está en pantalla) y a partir de ahí es del
    usuario. `isCustomized()` decide si se muestran las cabeceras de grupo y el índice A-Z
    ("saltar a la K" no significa nada con los mosaicos puestos a mano).
  - **Cómo se le agregan y se le sacan miembros a un grupo**, que es lo que no se descubre
    solo: **arrastrando el mosaico sobre la solapa destino** (la solapa se resalta y el
    fantasma de celda se apaga), o desde el menú contextual del mosaico, **Mover a grupo ▸**,
    que lista los grupos de la sección con un tilde en el actual y termina en *Grupo nuevo…*
    (crea el grupo y le manda ese mosaico de una). **No existe "sacar de un grupo"**: un
    mosaico siempre pertenece a alguno, así que sacarlo es mandarlo a otro. Quitar un grupo
    manda sus mosaicos al vecino. `moveTileToGroup()` lo deja en el primer hueco libre del
    destino. Y desde el panel propio hay un **editor de Grupos** (`createGroupsGroup()`): un
    combo de sección + la lista de sus grupos, con agregar, renombrar, quitar, subir y bajar.
    Mueve grupos, no mosaicos — para eso están el arrastre y el menú del mosaico, y el panel
    lo dice.
  - **El objetivo del arrastre sale del puntero, no del centro del mosaico** (mordió
    2026-08-03): el mosaico cuelga de donde lo agarraste, así que su centro puede seguir bien
    adentro del lienzo con el puntero ya sobre una solapa. `TileMenuTile` reporta la posición
    del puntero en coordenadas de la raíz (`root.dragPointer`) y `dragOverTab` resuelve con
    eso. De paso resuelve que el mosaico quede recortado por el lienzo al salirse: lo que
    importa es dónde apunta el usuario.
  - **Los grupos son solapas**: el lienzo dibuja **uno solo por vez**, el que dice
    `TileModel::currentGroup`. Cada grupo arranca su propia matriz en la fila 0, así que un
    mosaico se posiciona con su `(col, fila)` y no hay eje compartido que mantener en sincro.
    La barra (`qml/TileGroupTabs.qml`) va en el borde que diga `config.groupTabs`
    (0 arriba / 1 abajo / 2 izquierda / 3 derecha) y **aparece solo cuando la sección tiene
    más de un grupo** — una sola solapa no informa nada y come espacio. La barra y el lienzo
    viven dentro de un `Item` contenedor (`canvasCol`), que es lo que deja los anclajes en
    un borde contra otro en vez de un condicional a tres bandas entre el lado de la barra
    lateral y el riel A-Z.
    - Antes eran **bandas apiladas** en una única matriz, con un `absRow` que sumaba las filas
      de las bandas anteriores y cabeceras intercaladas. Se cambió a solapas a pedido
      (2026-08-03); con eso desaparecieron `absRow`, `startRow`, el alto de cabecera y **el
      plegado**, que con solapas no significa nada (la clave `c` del JSON se ignora al leer).
  - **`placement()` es *la disposición*, no *la vista*, y el filtro de qué se dibuja va en el
    modelo.** Devuelve todos los mosaicos de la sección, de todos los grupos, porque eso es lo
    que necesitan las funciones de edición y el contador de mosaicos por solapa;
    `TileModel::refresh()` se queda solo con los de `currentGroup`. La distinción se pagó
    cara con la versión de bandas: ahí el modelo dibujaba también las de una banda plegada,
    a la que `groupRows()` ya le daba 0 filas, y salían dos síntomas de un solo bug —plegar
    no hacía nada **y** la banda siguiente encimaba sus íconos sobre la plegada (reportado
    con captura, 2026-08-03).
  - `TileModel::refresh()` **acota `currentGroup` antes de usarlo**: una solapa puede
    desaparecer bajo los pies (borrada desde el panel, o cambio de sección), y pedir los
    mosaicos de un grupo que ya no existe dibujaría un lienzo vacío sin forma de volver.
  - **Colisión deliberadamente manual, sin auto-compactar**: libre → coloca; **un** mosaico del
    **mismo tamaño** → **swap**; cualquier otro solapamiento → **rechazo** (`moveTile()`
    devuelve false y el QML lo devuelve a su lugar). Una disposición hecha a mano no se
    reacomoda sola porque se movió un vecino. `dropKind()` responde lo mismo *antes* de soltar,
    así el fantasma no miente (azul libre / verde swap / rojo rechazo).
  - **Agrandar sí reubica**, nunca falla: `resizeTile()` prueba en el lugar y, si no entra,
    manda el mosaico al primer hueco de su grupo donde sí entre.
  - **Los registros de apps desinstaladas se conservan** (`Section::orphans`) para que una
    reinstalación recupere su lugar, pero se mantienen **fuera** de `tiles`: un mosaico que no
    se dibuja no puede participar de una colisión.
  - **El auto-placement es lineal, no cúbico** (importa: "Todas las aplicaciones" son ~450
    mosaicos). Los huecos salen de un `QSet` de celdas ocupadas y de un cursor que solo avanza
    — todos los mosaicos nuevos son 1x1 y el barrido es row-major, así que una celda que quedó
    atrás no puede liberarse. Probar cada celda candidata contra la lista de mosaicos ya
    puestos era O(n³).
  - **`liveIds()` está cacheado por sección** e invalidado por `AppMenu::changed` /
    `favoritesChanged`: `placement()` corre en cada celda que cruza el puntero durante un
    arrastre, y cada corrida le preguntaba a `AppMenu` por las apps de la sección.

- **Persistencia**: un único JSON compacto en `~/.local/share/kdock/tilemenu.conf`
  (`[General] layout=`). Un blob y no una clave por sección porque las claves de sección llevan
  `/` y `:`, que QSettings lee como jerarquía de grupos. Exportar/importar escribe el mismo
  JSON indentado a un archivo.
- **La disposición es una sola para toda la sesión**: hay un solo proceso, así que todos los
  docks abren el mismo menú y ven lo mismo. No hay variante por monitor ni interruptor de
  compartir.

- **`TileModel` es un `QAbstractListModel`, no una `QVariantList`**, para que una soltada sea
  un `dataChanged()` de una fila en vez de reconstruir el lienzo: el mosaico se anima a su
  celda nueva con `Behavior on x/y` y el resto no parpadea. `refresh()` compara la secuencia de
  ids y solo hace `beginResetModel()` cuando cambió el conjunto.
- **El menú contextual y el tooltip son compartidos, uno para todo el lienzo** (medido
  2026-08-03): declarados dentro del delegate, "Todas las aplicaciones" instanciaba ~450 `Menu`
  (con su `Instantiator` de colores) y ~450 `ToolTip` — **783 MB de RSS y 3,5 s** para abrir la
  sección, contra **245 MB y 0,9 s** una vez compartidos. El menú vive en `TileMenu.qml` y se
  parametriza con `root.ctxTile`; el tooltip usa la API **adjunta**
  (`ToolTip.text`/`ToolTip.visible`), que ya comparte una sola instancia por ventana.
- **Geometría del lienzo**: `columns` fijas por config (default 10; `0` = ajustar al ancho) y
  la **celda** es lo que se estira para llenar el espacio, acotada por `cellMin`/`cellMax`. Las
  columnas fijas son lo que hace que una posición guardada valga igual en cualquier monitor: lo
  que se adapta es el tamaño de la celda, no la matriz.
- **Cierre**: Esc, la ✕ de la esquina, perder el foco y lanzar una app — los cuatro anulados
  por el casillero **Mantener abierto** de la esquina (persistido en `keepOpen`), con el que la
  ventana queda como una ventana más y se puede mandar al fondo y volver por el alt-tab. El
  cierre por foco tiene dos guardias: una ventana muerta de 400 ms tras `show()` (el compositor
  todavía está entregando el foco) y un contador de diálogos propios arriba, o abrir la
  configuración cerraría el menú que la abrió.
- **Ciclo de vida**: el primer clic del widget lo lanza (`--toggle`) y queda residente; los
  clics siguientes son un toggle por D-Bus (`org.kdock.TileMenu` en `/TileMenu`, y ser dueño
  del nombre del bus **es** el candado de instancia única). Opción *Precargar* para levantarlo
  con kdock. La ventana se `hide()`, nunca se destruye.
- **Lo que se toca en kdock, y es poco**: `src/tilemenulauncher.{h,cpp}`, dos claves en
  `DockConfig` (`showTileMenu`, `tileMenuIcon`) con su línea en `knownWidgetTokens()`, el
  `Component` de `Dock.qml` (bloque, como `menu`), una context property en `DockWindow`, el
  grupo de la solapa **Menu** y una línea en `main.cpp` para el precargado.
- **UI**: Configuración → **Menu** → grupo *"Menú de mosaicos (pantalla completa)"* (casillero
  del widget, ícono, precargar, estado del proceso y botón *Configurar…*). Deliberadamente
  **no** es una solapa nueva: con once títulos la barra ya pide 1086 px y la duodécima la manda
  a modo flechas de scroll sin avisar. Todo lo demás vive en el panel propio del binario
  (`TileSettingsDialog`: Grilla, Apariencia, Barra lateral, Comportamiento, **Grupos**,
  Disposición).
- **Backup**: `ConfigArchive` archiva ahora las **tres** familias (`kdock*.conf`,
  `previews*.conf`, `tilemenu*.conf`), así la disposición entra al `.zip`. Al importar solo se
  borran las familias de las que el archivo trae al menos una entrada, para que un `.zip` viejo
  no se lleve puesta la disposición que el usuario armó después.
- **Diagnóstico**: `kdock-tilemenu --dump-layout [sección]` imprime la disposición resuelta
  como arte ASCII (grilla + leyenda) y sale. No abre ninguna ventana y solo lee, así que sirve
  con el menú corriendo. Es a este binario lo que `--dump-captures` es a `kdock-previews`.

### Calendario — binario accesorio `kdock-calendar` (`calendar/`)

Calendario de mes **standalone** estilo KDE, pensado para lanzarse desde el widget del reloj
(o un Script Runner): el widget **no se toca** — es un toplevel normal aparte, así que
engancharlo nunca implica editar el QML del dock. Es un **cuarto binario**, con su propio árbol
(`calendar/`) y **sin configuración propia**: todo el look sale de la paleta del sistema.

- **Qt Widgets + pintado a mano, no QML.** Todo vive en `calendarwidget.cpp` (`paintEvent`):
  la grilla 7×6, los **números grandes** que escalan con la celda (`min(celda) * 0.40`), el
  encabezado con mes/año en negrita y las flechas ‹ ›, la fila de días de la semana y el pie.
  Sin child widgets, sin QQuick, sin KDE Frameworks: solo `Qt6::Widgets`. Como los colores
  salen de `QPalette`, sigue el tema claro/oscuro y el acento KDE automáticamente.
- **Features KDE pero sin configurabilidad regional**: semana **lunes-primero**, fecha local
  (`QLocale()`), **día actual** en píldora rellena con el color de acento, día seleccionado con
  anillo, y días de otros meses / fines de semana atenuados. Extras simples: botón **Hoy**,
  rueda / `PageUp`/`PageDown` para cambiar de mes (la selección salta al mismo día del mes),
  flechas para mover la selección, `Esc` cierra, y el pie muestra la fecha larga del día
  seleccionado.
- **`--month AAAA-MM`**: arranca en otro mes (por defecto, el actual). Es la única CLI; el
  widget del reloj lo lanza pelado y muestra el mes en curso.
- **Sin privilegios y sin arnés especial**: como `kdock-tilemenu`, es un toplevel normal (ni
  layer-shell ni interfaces KWin), se corre directo desde `build/calendar/kdock-calendar` y su
  `.desktop` (solo para nombre e ícono en el gestor de tareas) no necesita refrescar ksycoca.
  Bajo Xvfb se captura igual que el menú de mosaicos.
- **Lo que se toca en kdock, y es nada**: solo `add_subdirectory(calendar)` en el
  `CMakeLists.txt` raíz. No comparte ningún archivo de `src/`.

### Control Manager — binario accesorio `kdock-controlmanager` (`controlmanager/`, 2026-08-08)

Panel de control con **solapas horizontales**: audio, brillo por monitor, perfil de energía,
modo oscuro, calendario, reproducción (MPRIS), red, fondo de escritorio y sistema. La primera
solapa (*Principal*) es una **grilla de tarjetas** que el usuario reacomoda y redimensiona
arrastrando, y cada sección se muestra ahí como tarjeta a elección. **No maneja íconos de
aplicaciones**: eso es del dock y del menú de mosaicos. Quinto binario, con su árbol, su
config y su panel de ajustes, que el widget `controlmanager` prende y apaga.

- **La ventana es una superficie layer-shell anclada, no un toplevel.** Es la decisión que lo
  distingue de `kdock-tilemenu`: el panel tiene tamaño propio (`panelWidth`/`panelHeight`, o
  un porcentaje de la pantalla) y tiene que colgar del borde que diga el usuario, y en Wayland
  un cliente **no puede ubicar un toplevel** — KWin lo pondría al medio. Así que compila
  `src/layershell.cpp` y los dos protocolos, igual que `previews/`.
  - **Sin privilegios igual**: `zwlr_layer_shell_v1` se le anuncia a un cliente sin autorizar
    (verificado con `wayland-info`), así que su `.desktop` no necesita refrescar ksycoca —
    como el del menú de mosaicos, y a diferencia de `kdock-previews`.
  - **Zona exclusiva siempre 0**: es un panel que se abre y se cierra, no un strut. Reservar
    espacio le movería las ventanas maximizadas al usuario en cada apertura.
  - **El teclado se pide y se devuelve**: `kdock.keyboardInteractivity` a 1 al mostrar y a 0
    al ocultar. Es lo único que le da Esc y, sobre todo, `activeChanged` — o sea "el usuario
    hizo clic en otro lado", que es la única señal de pérdida de foco que tiene una superficie
    layer-shell. Las dos guardias son las de `TileWindow`: 400 ms de gracia tras `show()` y un
    contador de diálogos propios arriba.
  - **Cierre**: Esc, la ✕, perder el foco y (opcional, apagado) salir el puntero — los cuatro
    anulados por **Ventana permanente** (`keepOpen`), que es el pedido original de "puede
    convertirse en ventana permanente con un checkbox".
- **Las secciones son una tabla estática** (`controlmanager/src/cmsections.cpp`): id, ícono,
  tamaño inicial en celdas, tamaño mínimo y si tiene solapa propia. Es lo que leen la barra de
  solapas, el motor de la grilla, el panel de ajustes y `--dump-sections`. **Agregar una
  sección es una fila ahí, un `.qml` en `qml/cards/` y un `case` en `CmSectionView.qml`.**
  Las etiquetas **no** están en la tabla: salen de `CmSections::label()` con `tr()` en el
  momento, para que un cambio de idioma sea una relectura y no una reconstrucción.
- **Una sola componente por sección, en dos tamaños.** `CmSectionView.qml` mapea id → archivo
  y le pasa `compact`: la tarjeta de Principal y la solapa completa son **el mismo** `.qml`.
  Por eso cada `cards/*.qml` tiene dos bloques (`visible: card.compact` / `visible:
  !card.compact`) en vez de dos archivos que se desincronizarían.
- **El motor de la grilla es `TileLayout` sin secciones ni grupos** (`cmlayout.cpp`): hay una
  sola grilla y a lo sumo una docena de tarjetas, así que la tarjeta se identifica por el id de
  su sección. Se conservó lo que costó caro allá: colisión manual (libre → coloca; **una** del
  **mismo tamaño** → swap; cualquier otro solapamiento → rechazo), `dropKind()` read-only para
  que el fantasma no mienta, agrandar que **reubica** en vez de fallar, y los registros de las
  tarjetas que ahora no están en Principal (vuelven a su lugar al reactivarlas, y mientras
  tanto no participan de ninguna colisión). El auto-placement es un `firstFreeSlot()` por
  tarjeta —a diferencia del cursor del menú de mosaicos— porque acá las tarjetas tienen
  **tamaños distintos** y son pocas.
- **Solo la cabecera arrastra.** El cuerpo de una tarjeta está lleno de sliders y botones; un
  `drag.target` sobre toda la tarjeta se los comería. La cabecera es el asa (y el clic derecho
  abre el menú, el doble clic va a la solapa completa).
- **La barra de solapas se adapta midiendo, no adivinando.** Con ocho secciones los títulos
  se pasaban del ancho y la última solapa simplemente no estaba. Ahora las solapas sueltan su
  etiqueta y se quedan con el ícono (menos la actual, que siempre se lee) cuando no entran.
  La decisión **no puede leer la fila visible** —su ancho es lo que decide, o sea un binding
  loop—: la lee de una fila oculta que siempre dibuja las etiquetas completas. Un umbral
  adivinado se probó primero y falló en las dos direcciones (a 96 px seguía desbordando, a 120
  plegaba etiquetas que entraban).
- **Los íconos van por `win.iconSuffix`, nunca por `theme.revision` a mano.** Media docena de
  íconos de breeze son line-art oscuro y **desaparecen** sobre un panel oscuro (el de brillo se
  perdió entero en la primera captura). El sufijo resuelve el set (`widgetIconThemeDarkBg` /
  `widgetIconThemeLightBg` del `kdock.conf` compartido, o sea la misma elección que el dock)
  según la luminancia del fondo **del panel**, y lleva el `theme.revision` que invalida la
  caché de pixmaps de QML.
- **Backends**: reusa `AudioControl`, `BatteryControl`, `BrightnessControl`, `NetworkControl`,
  `WallpaperControl`, `PowerControl` y `DesktopEntryIndex` tal cual, y agrega tres propios:
  - **`ScreenBrightness`** (`org.kde.ScreenBrightness`, PowerDevil): brillo **por monitor**, que
    es lo que `brightnessctl` no puede (maneja el backlight interno y nada más). Un objeto por
    display con `Label`, `Brightness`, `MaxBrightness` e `IsInternal`. **Los nombres de esos
    objetos son volátiles**: se renumeran cuando un monitor duerme y vuelve (visto en vivo:
    `display11`/`display12` pasaron a `display13` entre dos corridas), así que nada los cachea
    y `DisplayAdded`/`DisplayRemoved` reescanean. Si el servicio no está, la sección cae al
    `BrightnessControl` de siempre con un solo slider.
  - **`MprisControl`** (`org.mpris.MediaPlayer2.*`): el reproductor "actual" es el que eligió el
    usuario, si no el que está `Playing`, si no el primero. La posición **se sondea**, y solo
    mientras la tarjeta está en pantalla (`setMonitoring`), porque es un round trip por tick.
  - **`DockLink`**: cliente de `org.kdock.Dock` (abajo).
- **`org.kdock.Dock` — el servicio nuevo de kdock** (`src/dockservice.{h,cpp}`). Existe porque
  tres cosas no se pueden hacer desde afuera del proceso del dock: **el modo oscuro** (escribir
  `darkModeOn` en el `.conf` no repinta nada — no hay watcher de archivos, y el modo se
  resuelve al dibujar), **el diálogo de Configuración** (se abre solo desde el menú del dock) y
  **el reinicio**. Métodos: `openSettings(dockId)`, `restart()`, `darkMode()`/`setDarkMode(b)`/
  `toggleDarkMode()`, `dockIds()`/`dockScreens()`/`primaryDockId()`, señal `darkModeChanged(b)`.
  - **Tres `as` paralelas en vez de un `a(ssb)`**: `as` no necesita `qDBusRegisterMetaType` de
    ningún lado (ver la trampa de los structs de D-Bus en `CLAUDE.md`).
  - **No tiene `quit()` a propósito**: dejar al usuario sin dock y sin forma de volver no es
    algo que una llamada D-Bus perdida deba poder hacer.
  - La señal sale del `DarkModeNotifier` **de proceso**, no del `darkModeChanged()` por
    instancia: con quince docks ese se emite quince veces por conmutación, y esto es una señal
    de bus.
- **Atajo global propio** (`src/globalshortcut.{h,cpp}`): kdock publica la acción
  `toggle-dark-mode` bajo el componente `kdock` de kglobalaccel, por D-Bus crudo (sin KDE
  Frameworks). `KWinShortcut` solo **dispara** atajos ajenos; esto **registra** uno nuestro.
  Va **sin combinación por defecto**: aparece en *Preferencias del sistema → Atajos* para que
  el usuario le asigne una, en vez de robarle una tecla. Se usa `setShortcut` (`asaiu`) y no
  `setShortcutKeys` (`asa(ai)u`): la firma vieja toma un array de ints, y una `QList<int>`
  vacía se marshalea sola — la nueva necesitaría un metatipo registrado para decir "ninguna".
- **Persistencia**: `~/.local/share/kdock/controlmanager.conf`, una sola para toda la sesión
  (hay un solo proceso). Tres notificadores en vez del único de `TileConfig`, porque cuestan
  distinto: `settingsChanged()` repinta, `windowChanged()` re-commitea la superficie
  layer-shell y `sectionsChanged()` reconstruye la barra y la grilla. La disposición es un JSON
  compacto en la clave `layout`.
- **Lo que se toca en kdock**: `src/controlmanagerlauncher.{h,cpp}`, seis claves en
  `DockConfig` (`showControlManager`, `controlManagerIcon`, `controlManagerDisplay`,
  `controlManagerText`, `controlManagerFormat`, `controlManagerFontSize`) con su línea en
  **`knownWidgetTokens()`**, el
  `Component` de `Dock.qml` (bloque, como `tilemenu`), una context property en `DockWindow`, el
  grupo de la solapa **Widgets**, una línea en `main.cpp` para el precargado, el glob
  `controlmanager*.conf` en `ConfigArchive`, y —esto sí es nuevo— `DockService` +
  `GlobalShortcuts` en `main.cpp` y `DockManager::windowFor()`.
- **El widget dibuja texto, que es lo que ningún otro widget hace**: `controlManagerDisplay`
  elige ícono / ícono+texto / solo texto, y el texto es `controlManagerText` (una cadena corta
  del usuario, "Máquina de Pruebas") o —si está vacía— **el reloj**, formateado con
  `controlManagerFormat`. El `Timer` solo corre cuando el texto *es* el reloj. Crece por
  `implicitWidth`, igual que `clock2`.
- **El texto tiene fuente propia** (`config.controlManagerFontSize`, 2026-08-08): es el único
  texto del dock que no cuelga de ningún modo de etiqueta — existe aunque el dock no dibuje ni
  nombres de apps ni nombres de secciones, que es justo el caso de un dock con solo este
  widget. 0 = automático: sigue la fuente del reloj (`clockFontSize`, la que usaba el texto
  del CM antes del ajuste dedicado) y, si tampoco, una fracción del tamaño del ícono. Se edita
  en **Configuración → Fuentes → "Control Manager text size:"** y en el grupo de la solapa
  Widgets ("Tamaño del texto:"), dos copias sincronizadas; el spinbox se deshabilita cuando el
  widget está apagado o no dibuja texto (`controlManagerDisplay == 0`).
- **Botones de las solapas redimensionables** (`cmConfig.buttonWidth`/`buttonHeight`, en el
  panel de ajustes → **Apariencia → "Ancho/Alto mínimo de los botones:"**, 2026-08-08): los
  `CmButton` de las solapas (y los de las tarjetas de Principal, que son la misma componente)
  reciben un tamaño mínimo de `implicitWidth`/`implicitHeight`; 0 = tamaño natural (el texto
  largo sigue pudiendo empujar el ancho). Un `Flow` respeta el `implicitWidth` de cada hijo,
  así que el cambio re-arma la fila al vuelo.
- **Fuente única para todo el panel** (`cmConfig.fontSize`, 2026-08-08): un spinbox en
  **Apariencia** ("Tamaño de fuente:", 0 = Predeterminado, 12 px efectivos) del que cuelga el
  `fontScale` read-only (`fontSize/12`, 1.0 con 0). **Cada** `font.pixelSize` del QML del
  panel —solapas, botones, tarjetas, sliders y secciones, 42 puntos— se multiplica por el
  factor, y los contenedores que recortarían (alto de solapas, cabecera de tarjeta, fila de
  slider, esquinas) lo siguen. Ojo al tocar fuentes acá: la fila oculta de medición de
  `CmTabs` (`measureRow`) escala igual, o el colapso de etiquetas decidiría con el tamaño
  viejo.
- **Iconset propio del panel** (`cmConfig.iconTheme`, 2026-08-08): un `ThemePickerButton` en
  modo `PickValue` en **Apariencia** ("Iconset del panel:", entrada especial vacía = seguir el
  del sistema). Con id puesto, `CmWindow::iconSuffix()` lo antepone y **gana** sobre el par
  por luminancia (`widgetIconThemeDarkBg`/`LightBg` del `kdock.conf` compartido); vacío = el
  comportamiento histórico. El picker lista los mismos temas y favoritos que el del dock:
  `CmWindow` construye un `AppearanceControl` propio (se compila `appearancecontrol` y
  `themepicker` en este target, reutilizados sin modificar).
- **Botones de sesión con etiquetas** (2026-08-08): la fila *Sesión* de `SystemCard.qml`
  (bloquear/suspender/cerrar sesión/reiniciar/apagar) pasó de ícono pelado a ícono + texto,
  con `compact: card.compact` — en la solapa completa se leen solos, en la tarjeta chica
  quedan compactos y el `Flow` envuelve.
- **UI**: Configuración → **Widgets** → grupo *"Control Manager (panel de control)"* (casillero
  del widget, ícono, qué muestra, texto, formato, precargar, estado y botón *Configurar…*).
  Todo lo demás vive en el panel propio del binario (`CmSettingsDialog`: Ventana, Apariencia,
  Grilla, Secciones, Disposición y scripts).
- **El editor de secciones es una grilla de filas, no un `QListWidget`**: dos casilleros por
  fila (*Solapa* y *Principal*) más subir/bajar. Un `setItemWidget()` se come el manejo de
  mouse de la lista, y el arreglo obvio para eso (`WA_TransparentForMouseEvents`) mata los
  casilleros de adentro — las dos trampas están documentadas en `CLAUDE.md`.
- **Diagnóstico**: `kdock-controlmanager --dump-sections` imprime la grilla resuelta en ASCII,
  la tabla de secciones con su estado, los monitores con brillo, los reproductores MPRIS y lo
  que contesta `org.kdock.Dock`, y sale. No abre ninguna ventana y **solo lee**, así que es la
  única forma de ver los backends sin tocar la máquina (los sliders del panel escriben al
  brillo y al mezclador de verdad). Es a este binario lo que `--dump-layout` es al menú de
  mosaicos.

### Capa de traducciones (`src/translations.{h,cpp}`, `translations/*.md`, 2026-08-07)

Los textos escritos en el código son la **capa nativa "capabase"** (ni inglés ni español: lo
que haya en el `.cpp`/`.qml`). Encima se carga **una traducción**, un `.md` de texto plano en
`~/.local/share/kdock/translations/` que el usuario edita con su editor. Se entregan tres:
`capabase.md`, `english.md` y `spanish.md`.

- **Todo pasa por un `QTranslator` propio.** `KdockTranslator::translate()` busca la cadena
  fuente en un `QHash` y devuelve un `QString` **nulo** cuando no la tiene, que es como se le
  dice a Qt "usá el texto fuente" ⇒ el fallback a capabase sale gratis. Como `tr()` y `qsTr()`
  ya pasan por `QCoreApplication::translate()`, las ~690 cadenas del proyecto quedan cubiertas
  **sin tocar un solo call site**.
- **Cuatro secciones**, dos formas de indexar:
  `## Configuracion` y `## UIdock` van por **cadena fuente** (el texto capabase) y se cargan en
  **un solo mapa** — la división es para que el archivo se lea, no para la búsqueda: indexar
  por el *context* de Qt rompería una traducción cada vez que una cadena cambia de clase.
  `## Widgets` va por **token** de sección (`clock`, `pager`…) y `## Apps` por **id de
  `.desktop`**.
- **Precedencia de un nombre de widget**: renombre manual del usuario → traducción → capabase
  (`DockConfig::widgetName()` → `translatedWidgetLabel()`). El renombre es una decisión
  explícita y sobrevive al cambio de idioma. Por eso `defaultWidgetLabel()` **no** usa `tr()`:
  es la tabla capabase contra la que se escriben las traducciones.
- **Nombre de app**: `appNameFor(entry)` (en `translations.cpp`) es el único lugar que decide;
  lo llaman `DockModel::Item::displayName()` y `AppMenu::entryToMap()`. Sin entrada en `Apps`,
  el `Name=` del `.desktop`. `AppMenu::search()` busca contra los dos nombres.
- **El idioma es global**: clave `language` del `kdock.conf` compartido (ojo: `value("language")`,
  no `"General/language"` — QSettings mapea `[General]` a la raíz).
- **Repintado en vivo** al cambiar de idioma o al guardar el archivo desde el editor (hay un
  `QFileSystemWatcher` sobre el directorio, con rebote de 250 ms): `Translations::changed` →
  `DockConfig::retranslate()` (bump de `widgetNamesRevision` en cada config viva) +
  `DockWindow::retranslate()` por dock, que llama `engine()->retranslate()` (re-evalúa todos
  los `qsTr()`) y **recrea el diálogo** — Qt Widgets no tiene `retranslateUi` acá. `DockModel`
  emite `dataChanged(NameRole)` por su cuenta.
- **UI**: solapa **Traducciones**, la última del diálogo. Lista los `.md` presentes (el activo
  en negrita y con tilde) y ofrece *Editar* (`QDesktopServices::openUrl` → editor
  predeterminado), *Usar este idioma*, *Actualizar apps* (agrega al archivo los `id = Name` de
  todas las apps instaladas que falten, idempotente), *Recargar* y *Nueva…* (duplica la
  seleccionada).
- **Capas ALT** (2026-08-07): nueve archivos, `{english,spanish,zh-CN}-ALT-{startrek,hacker,starwars}`,
  cada uno es su base (`english.md` / `spanish.md` / `zh-CN.md`) con la sección `Widgets` cambiada por
  apodos (naves de Star Trek / apodos de hacker / hardware de Star Wars) y una sección `Apps`
  compartida de 82 entradas — IA de novelas de ciencia ficción y herramientas de hackeo ficticias — armada
  contra los `.desktop` realmente instalados. Además de ser un chiste, son el **único ejemplo
  con la sección `Apps` llena** que se entrega, que es la que no se puede generar desde el
  código. El **reparto** (qué apodo le toca a cada token y a cada id) es uno solo para las
  nueve; en inglés y en español los apodos van tal cual, y para el chino hay una tabla más,
  `ZH_NAMES`, que traduce el apodo — o sea que el chino es una capa de traducción sobre los
  apodos, no otro reparto. Se regeneran con
  `python3 tools/gen-alt-layers.py` (las tablas de apodos están
  adentro; con `APPS_CHECK=<volcado de ids>` avisa de los ids que no tengan `.desktop`); si
  cambian las cadenas de `english.md`, `tools/sync-translations.py` las mantiene al día como
  a cualquier otro idioma.
- **Mantenimiento**: `python3 tools/gen-capabase.py` reescribe `capabase.md` desde el código
  (usa `lupdate`), y `python3 tools/sync-translations.py` re-sincroniza los demás `.md`
  conservando lo ya traducido y agregando las claves nuevas con el texto capabase. Los tres
  archivos van en el qrc y se copian al home en el primer arranque (`seedFiles()`, nunca pisa
  uno existente).
- **Los accesorios también** (2026-08-07): `kdock-previews` y `kdock-tilemenu` instancian
  `Translations(Translations::BaseOnly)` en su `main.cpp`. Tres consecuencias, y las tres son
  la feature:
  - **Un solo catálogo para los tres binarios**: `tools/gen-capabase.py` escanea `src/`,
    `qml/`, `previews/{src,qml}` y `tilemenu/{src,qml}`, así que las cadenas de los accesorios
    viven en los mismos `.md`. No hay archivos por binario.
  - **El idioma no se elige aparte**: sale de la misma clave `language` del `kdock.conf`
    compartido. Como los accesorios no la escriben, `Translations` en modo `BaseOnly` **vigila
    ese archivo** además del directorio, y un cambio de idioma en el dock les llega en vivo
    (`PreviewManager::retranslate()` / `TileWindow::retranslate()`: `engine()->retranslate()`
    + rebuild del panel de Qt Widgets).
  - **Una capa ALT cae a su base**: `Translations::baseLanguage()` corta en `-ALT-`, así que
    con `english-ALT-hacker` puesto en el dock, el menú de mosaicos y las tiras salen en
    `english` y con los nombres **reales** de las apps. Es lo correcto: los apodos renombran
    los widgets y los lanzadores *del dock*, que es donde está el chiste; en el menú de
    mosaicos serían nombres que no se pueden encontrar buscando.
  Los dos binarios embeben en su qrc **solo las cuatro capas base** (nunca las ALT, que no
  pueden cargar), para poder sembrarlas si corren sin kdock instalado.
  `kdock-calendar` sigue afuera: no tiene una sola cadena traducible propia.
- **Los nombres de categoría del menú son datos, no texto**: `kCategories` en `src/appmenu.cpp`
  los usa como *clave* de sección (`cat:Development`), así que la tabla queda en inglés y lo
  que se traduce es solo la etiqueta, en `categoryLabel()`. Las doce están declaradas con
  `QT_TRANSLATE_NOOP` al lado para que `lupdate` las encuentre; el `tr()` de verdad es
  dinámico. Así la sección elegida sobrevive a un cambio de idioma.

## Code Conventions

- **C++17**, Qt 6 (≥ 6.5).
- Header guards use `#pragma once`.
- Qt string literals: `QStringLiteral("...")`.
- QLatin1String/QLatin1Char for comparisons where possible.
- Signals/slots: prefer modern `connect(sender, &Class::signal, ...)` syntax.
- QML: `import QtQuick` and `import QtQuick.Controls.Basic` (no styles depending on KDE).
- `Q_INVOKABLE` for C++ methods called from QML.
- Dynamic properties prefixed with `kdock.` for layer-shell communication.

## Build & Run

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja -C build
./build/kdock
```

Deploy: `sudo ninja -C build install` (prefijo `/usr/local`; actualiza los binarios y los
`.desktop`). **Ojo**: el `sudo` reescribe `build/.ninja_deps` como root, y el siguiente
`ninja -C build` sin sudo falla con *"Error writing to deps log: Permission denied"* —
recuperar con `sudo chown <user>:<user> build/.ninja_deps`.

Dependencies: `qt6-base-dev`, `qt6-base-private-dev` (for the private `QZip*` headers used by `configarchive.cpp`), `qt6-declarative-dev`, `qt6-wayland-dev`, `qt6-wayland-private-dev`, `libqt6dbus6-dev` (or `qt6-tools-dev` depending on distro), `cmake`, `ninja-build`. CMake links `Qt6::CorePrivate` + `Qt6::WaylandClientPrivate` (private Qt modules).

## Dormant / UI-unreachable code

Some widgets had their **Settings checkboxes removed to save space** but the rest
of their code is left **intact but dormant** (existing configs that enabled them
still render and work; they just can't be toggled from the UI anymore).

- **Next-wallpaper widget (`nextwallpaper` token)** — checkbox removed 2026-07-15.
  - Intact: `src/wallpapercontrol.{h,cpp}` (`WallpaperControl`, ctx prop `wallpaperControl`, wired through `main.cpp`/`DockManager`/`DockWindow`); `config.showNextWallpaper`; the `nextwallpaper` token in `knownWidgetTokens()`; `nextWallpaperComp` + its cases in `Dock.qml`; `sectionLabel("nextwallpaper")`.
- **Clock widget (`clock` token)** — "Show clock" checkbox removed 2026-07-16.
  - Intact: `ClockWidget` (`src/clockwidget.{h,cpp}`, ctx prop `clock`); `config.showClock`; the `clock` token + `clockComp` in `Dock.qml`. **Note**: the clock **format** checkboxes (24h / date / seconds) stay in Settings and still apply to **both** clocks; `clock2` (enhanced-tooltip clock) keeps its own "Show clock (enhanced tooltip)" checkbox and is fully usable.
- **Layout tab hides `clock` and `nextwallpaper`** — `SettingsDialog::reloadLayoutList()` filters them out (`kHiddenFromLayout`) so they can't be reordered there. Because the list is filtered, the Layout reorder/add/remove handlers map a list row to its `widgetOrder` index through `orderIndexOfRow()`, which reads the index **stored on each item** (`Qt::UserRole + 1`). It used to resolve the row by its *token*, which silently broke with **two or more springs**: `widgetOrder.indexOf("spring")` always returns the first one, so Up/Down/Remove acted on the wrong separator and dragged a widget along with it (bug 2026-07-29). The list also **numbers the separators**, each kind on its own count ("Dynamic separator 1/2", "Static separator 1/2") — identical rows make that class of bug invisible.
- **To re-enable** any of the above: re-add the checkbox in `SettingsDialog::createWidgetsTab()` and (for clock/nextwallpaper) remove the token from `kHiddenFromLayout`. **To fully remove**: delete the backend + token + `*Comp` + config flag.

## Important Considerations for Modifications

1. **Layer-shell private API** — When modifying `layershell.cpp`, be aware that it uses QtWaylandClient private headers (`QWaylandShellIntegrationTemplate`, `QWaylandShellSurface`, etc.). These are not ABI-stable across Qt major versions.
2. **Protocol objects** — The generated Wayland protocol classes (`QtWayland::zwlr_*`, `QtWayland::org_kde_*`) are created by `qt_generate_wayland_protocol_client_sources`. Do not hand-edit the generated files in `build/`; they are regenerated from `protocols/*.xml`.
3. **Autohide + input mask** — Dock thickness now has a **single source of truth in C++**: `DockConfig::dockThickness()` (= `qMax(iconSize, appCellThickness()) + padding`). `DockWindow::thickness()` returns it (layer-shell exclusive zone) and `Dock.qml`’s `thickness` binds to `config.dockThickness`, so the two can no longer drift. Any new setting that changes the dock’s cross-axis size must be folded into `appCellThickness()` and must emit `dockThicknessChanged()` (`DockWindow` re-applies its layer properties on that signal). One input of that formula is *measured in QML*, not configured — `config.effectiveLabelWidth`, fed by `setMeasuredLabelWidth()` (see the widest-name bullet under **App-icon labels**). That is the only QML→C++ feedback into the thickness, and it is safe because a label's natural width does not depend on the thickness; do not add a remeasure hook on `dockThicknessChanged`.
4. **Window geometry in panel mode** — When `panelMode` is true and `dockLength` is 0, the compositor dictates the stretched dimension (layer-shell size = 0 in that dimension). `Dock.qml` uses `config.panelMode` to decide whether to follow `Window.width/height` or content size. When `dockLength` > 0, the dock has a fixed length (percentage of the screen edge) regardless of `panelMode`; the layer-shell anchors only one side corner (not both) so the compositor respects the explicit `set_size`.
5. **KWin privilege** — If the window list stops working on KWin during development, verify the `.desktop` file is installed with the correct `X-KDE-Wayland-Interfaces` and `Exec=` path.
6. **QML context properties** — `DockWindow` (`dockwindow.cpp`) injects `config`, `theme`, `dockModel`, `dockWindow`, `volume`, `clock`, `clock2`, `brightness`, `overview`, `desktopControl`, `monitorControl`, `maxmin`, `activeWindow`, `wallpaperControl`, `power`, `systray`, `relanzadores`, `scriptRunners`, `clipboardHistory`, `battery`, `disks`, `network`, `dockIsPrimary`, `apps`, `showdesktop`, `appMenu`, `iconColors` as context properties. Any new C++ object exposed to QML should be added there. `systray` is injected in every dock (the guard `systray && …` in QML is kept for the legacy single-dock path); `relanzadores` is passed to every dock but filtered per-dock (see the relanzadores note under DockManager) with `dockIsPrimary` selecting the default. Note: inside `RelanzadorWidget`/`RelanzadorPopup`/`ScriptRunnerWidget`/`AppMenuPopup`, alias context properties (`_config`, `_theme`, …) before passing them to same-named component properties to avoid self-shadowing.
7. **Multi-screen** — Changing screens recreates the platform window (`hide(); destroy(); setScreen(); show()`) because `wl_output` is fixed at layer-surface creation. The `wl_output` is resolved from `QWindow::screen()->handle()` (cast to `QWaylandScreen*`) rather than `window->waylandScreen()` because the latter may not have picked up the target screen yet after the destroy/set/show cycle. Falls back to `waylandScreen()` if the cast fails.
8. **Window detection on startup** — At construction time, Wayland protocol events from the compositor (e.g. `window_with_uuid` from KWin) are still queued in the socket buffer. `DockModel::rebuild()` sees an empty `monitor->windows` list. A deferred `syncWindows()` call (200 ms via `QTimer::singleShot` in `DockWindow` constructor) iterates all tracked windows after the event loop starts and places any that were missed.
8. **Drag & drop** — `DockModel::moveItem()` enforces that dragged items cannot cross the pinned/unpinned boundary or move separators.
9. **Static separators** — Two different things share the name, both sized by `DockConfig::separatorSize` (default 16 px, lateral space in horizontal mode / vertical space in vertical mode) and both configured in the **Layout tab** (see #16 — they used to be three spinboxes in General):
    - **Inside the apps block**: `DockModel` inserts up to 2 fixed-size separator items among the app icons, at the indices `DockConfig::separator1`/`separator2` (`-1` = off). The index counts into the *merged* list (pinned launchers first, then running windows), so an index equal to the number of pinned apps is what splits launchers from running windows.
      Either slot can be **transparent** (`DockConfig::separator1Transparent`/`separator2Transparent`, default false, 2026-08-08): it keeps its `separatorSize` px of room but draws no line. The flag travels to the delegate as the `separatorTransparent` role of `DockModel` and does exactly one thing there — turns off the line `Rectangle`. The cell size comes from the delegate itself, so nothing moves. Note this is **not** the `gap` token: the panel background stays painted behind it and it still eats clicks; no `setGapRects` involved. `DockModel` rebuilds on the two new signals (without that the checkbox looks dead until something else forces a rebuild).
    - **Between sections**: the `sep` token of `widgetOrder` (see #13), a fixed gap anywhere in the section order.
    Both render as the same thin line in QML and are non-interactive. Distinct from the **dynamic separator** and from the transparent `gap` section (see #13).
9b. **Submenu icons (`SubMenuDelegate.qml`, 2026-08-08)** — Every leaf of the dock's menus is an `IconMenuItem`, but the row that *opens* a submenu is not declared by us: Qt builds it from the parent menu's `delegate`, which by default is the stock `MenuItem` (no icon), so those rows were the only bare ones. Both menus that have submenus (`sectionMenu` and `contextMenu` in `Dock.qml`) now set `delegate: SubMenuDelegate {}`, and each submenu carries its icon name in a `property string menuIcon` that the delegate reads through its own `subMenu` property: `virtual-desktops` (Escritorio), `transform-move` (Ubicación), `application-menu` (Dock), `color-management` (`BackgroundColorMenu`), `contrast` (`ModeMenu`), `view-list-details` (`IconLabelMenu`), `view-list-tree` (`WidgetLabelMenu`). Three things that bite:
    - **A delegate-built row cannot resolve `theme` from inside `IconMenuItem.qml`**, so letting it build the URL there (which `iconName` does, appending `theme.revision`) throws *"Cannot read property 'revision' of undefined"* **once per row** — 112 lines of stderr on a normal config, with the icons still drawn. `SubMenuDelegate` therefore assembles the URL itself and passes it as `iconSource` (which takes precedence), leaving `iconName` empty so `IconMenuItem`'s own expression short-circuits.
    - **The first evaluation happens while the row is detached**, with every context property reading back undefined (even `console`), hence the `theme ?` guard and the `_ready` flag: once it flips, the binding re-runs with `theme` live, so a theme change still busts the icon cache.
    - **Reserve the arrow's room** (`rightPadding`): `IconMenuItem`'s `Row` does not know about the submenu arrow the way the stock `IconLabel` does, so the widest label runs underneath it.
    Related fix in the same pass: `IconMenuItem` now also reserves the **check indicator's** column with a spacer. `MenuItem` draws the tick at `leftPadding`, i.e. right on top of our icon, so every checked entry of `ModeMenu`/`IconLabelMenu`/`WidgetLabelMenu`/`BackgroundColorMenu` read as a smudge. (The color swatches of `BackgroundColorMenu` have their own `contentItem` and are still drawn under the tick.)
10. **Brightness widget** — Uses `brightnessctl` (via QProcess, no extra library linkage). The widget is hidden if `brightnessctl` is not in `$PATH`.
11. **Autohide toggle** — A dock widget button that toggles `config.autohide`. Uses `window-pin`/`window-unpin` icons.
12. **Systray DBus (SNI)** — `SystrayHost` implements the StatusNotifierItem protocol using the **`org.kde.*`** bus names, which is what the ecosystem actually implements (KDE, libappindicator, Qt/GTK trays); the `org.freedesktop.*` names are only registered as extra aliases when kdock has to *become* the watcher (bare wlroots, no existing watcher). Key points (see `src/systray.cpp`):
    - **Watcher discovery**: prefer an existing `org.kde.StatusNotifierWatcher`, else `org.freedesktop.StatusNotifierWatcher`, else register both ourselves. The chosen name is `m_watcherService` (used for host registration, the item list and the `StatusNotifierItemRegistered/Unregistered` signal subscriptions).
    - **`RegisteredStatusNotifierItems` is a D-Bus *property*** of the real watcher (not a method), read via `org.freedesktop.DBus.Properties.Get`. (Our own scriptable method form only applied when kdock was the watcher.)
    - **Item id parsing** (`splitItemId`): the watcher returns items as `service+path` concatenated (e.g. `:1.67/org/blueman/sni`); split at the first `/` (no `/` ⇒ path `/StatusNotifierItem`).
    - **Item properties/signals** use `org.kde.StatusNotifierItem`. Live updates come from the SNI signals `NewIcon`/`NewOverlayIcon`/`NewAttentionIcon`/`NewToolTip`/`NewTitle`/`NewStatus` (items do **not** emit `PropertiesChanged`); each re-runs `readProperties()`. `readProperties()` is a **private slot** so the string-based `SLOT()` connection resolves.
    - **Pixmap fallback**: items without a themed `IconName` expose only `IconPixmap` (`a(iiay)` = array of `(w, h, ARGB32-big-endian bytes)`). **These wire types MUST be registered with `qDBusRegisterMetaType` (KDbusImageStruct/KDbusImageVector, done in the SystrayHost ctor)** — otherwise `QDBusInterface::property()` returns empty and manual `QDBusArgument` demarshalling desyncs and **libdbus aborts** (`type struct not a basic type`). `SystrayItem::readPixmapProperty()` reads the property into `KDbusImageVector`, picks the largest frame and converts big-endian ARGB32 → native (`qFromBigEndian`). `SystrayImageProvider` (`image://systray/<service>`) serves it; on no icon it returns a transparent 1×1 (renders nothing, no QML warning). `SystrayModel` adds an `iconSerial` role (bumped every `readProperties()`) so the QML `Image` busts its cache on change. `Dock.qml` uses `image://icon/<name>` when `iconName` is set, else `image://systray/<service>@<iconSerial>`. Registered per `DockWindow` engine when `systrayHost` is non-null (i.e. in every dock).
    - **Item menus are drawn by the dock, over DBusMenu** (`src/dbusmenu.cpp`, `qml/SystrayMenu.qml`; 2026-07-29). SNI does not carry the menu: the item exposes a `Menu` object path speaking `com.canonical.dbusmenu` and the **host** is expected to fetch the tree and render it. Asking the item to draw its own (`ContextMenu(x,y)`) **cannot work here** — on Wayland an app with no visible window has no surface to parent an `xdg_popup` to, and plenty of items do not even export the method (blueman); it is kept only as the last resort for items with no `Menu` path. Verified on this host: the five live items all expose a real menu (`/MenuBar` for Qt/KDE, `/org/blueman/sni/menu`).
        - **Wire type**: `GetLayout` returns `u, (ia{sv}av)` where the children are an `av` of variants each wrapping the same struct, so `DBusMenuLayoutItem`'s `operator>>` **recurses** through the inner `QDBusArgument`. As with the SNI pixmaps, `qDBusRegisterMetaType` is mandatory before any call or demarshalling desyncs and libdbus aborts.
        - **Everything is async** (`QDBusPendingCallWatcher`, 2 s timeout): a blocking call against a hung item would freeze the dock for the D-Bus default of 25 s. So a click only *asks* (`requestMenu`) and QML opens the menu on `menuReady`. A layout already fetched is served immediately — `LayoutUpdated`/`ItemsPropertiesUpdated` keep it fresh and reach QML as `menuInvalidated`, which refreshes an open menu.
        - **Click routing** (`systrayComp` in `Dock.qml`): left = `Activate`, except that `ItemIsMenu` items get the menu (per spec, their Activate does nothing), and `Activate` is an **async call whose reply is inspected** — an error emits `SystrayItem::activateFailed`, which the model turns into a `requestMenu`, so blueman (no `Activate` at all) opens its menu instead of doing nothing. Right = the menu (`ContextMenu` only without one). Middle = `SecondaryActivate`. No wheel/`Scroll` support (deliberate).
        - **`SystrayMenu.qml` builds itself imperatively**, not with an `Instantiator`: the three kinds of entry are three different types (`MenuSeparator`/`IconMenuItem`/`Menu`) and one delegate can only produce one of them. Two traps: submenus make the component instantiate **itself**, which QML rejects declaratively (*"SystrayMenu is instantiated recursively"* — the check is static), so they go through `Qt.createComponent("SystrayMenu.qml")` at runtime; and items must be created with **`control.contentItem`** as parent, or every entry logs *"Created graphical object was not placed in the graphics scene"* (a `Menu` is a `Popup`, not an `Item`, so it is no visual parent). Teardown tracks what it created and uses `removeMenu` for submenus, `removeItem` for the rest.
        - **Labels and icons**: mnemonic markers (`_`/`&`) are stripped — QtQuick Controls interprets neither, so they would show up literally. Entries carry `icon-name` and/or `icon-data` (a raw PNG blob); the **theme name wins** (follows the icon theme, scales) and the blob is the fallback, served by extending the existing provider with an `image://systray/menu:<service>:<itemId>@<serial>` id. `toggle-type` renders as a check either way: QQC's radio indicator needs an exclusive group, which DBusMenu does not describe.
    - **Settings**: a "Mostrar la bandeja del sistema" checkbox in Settings → Widgets toggles `config.showSystray` (per-dock). **Any** dock can host the tray, but only one at a time: while another dock holds it the checkbox is disabled and a note names the owner (`DockManager::systrayDockId()` + `SettingsDialog::dockLabel()`) — see the systray bullet under DockManager. A "Systray icon scale" spinbox (20–100%) sets `config.systrayIconScale`, an **independent** scale (mirrors `widgetIconScale`): the derived read-only `config.systrayIconSize` = `qMax(16, iconSize * systrayIconScale%)` drives the tray icon cell/pixmap in `Dock.qml`'s `systrayComp` (so tray icons resize separately from other widgets, and `setIconSize` re-emits `systrayIconSizeChanged` for live rescale).
12b. **Dock sin íconos de apps (`config.showAppIcons`, 2026-08-04)** — Una casilla en Settings → General ("Mostrar los íconos de aplicaciones", default on, **por dock**) apaga la sección `apps` entera: lanzadores anclados y botones de ventanas. Sirve para usar un dock como barra de solo widgets/systray junto a otro que sí muestre las apps. Tres puntas, y las tres importan:
    - `Dock.qml` lo gatea en `sectionVisible("apps")` **y** devuelve `null` en `componentFor("apps")`: el `Loader` de cada sección instancia su componente aunque la sección esté invisible, así que sin lo segundo el `Repeater` de todas las apps seguiría vivo y actualizándose detrás de un dock que no lo dibuja.
    - `root.labelVisible` suma `&& config.showAppIcons`, que es lo que hace que `measureLabels()` deje de mandar los nombres de apps a `config.setMeasuredLabelWidth()`. Sin eso el dock sigue reservando el ancho del nombre más largo que ya no dibuja.
    - `DockConfig::dockThickness()` saltea `appCellThickness()` cuando está en false (el piso `iconSize` se mantiene: los widgets se siguen dibujando a ese tamaño), y el setter emite `dockThicknessChanged()` para que la *exclusive zone* de layer-shell siga al dock. Medido bajo Xvfb con `iconLabelMode=1`: 87 px con apps, 68 px sin ellas.
    - El token `apps` **no** se saca de `widgetOrder`: sigue en su lugar en la solapa Diseño, marcado "(oculto)", así que volver a prenderlo lo devuelve a la misma posición.
13. **Section layout & separators (`config.widgetOrder`)** — The dock content is an **ordered list of sections** stored in `DockConfig::widgetOrder` (a persisted `QStringList` of tokens). Known widget tokens: `menu`, `apps`, `clipboard`, `disks`, `network`, `volume`, `brightness`, `battery`, `clock`, `clock2`, `overview`, `movetodesktop`, `movetoscreen`, `maxmin`, `closewindow`, `nextwallpaper`, `darkmode`, `pager`, `autohide`, `showdesktop`, `systray`, `relanzadores`, `scriptrunners`, `session`, `settings`. On top of those come the two **repeatable** tokens (`DockConfig::isRepeatableToken()`): `spring`, the **dynamic separator**, and `sep`, the **static separator** (a fixed gap of `separatorSize` px, drawn by `staticSepComp`, cross-axis size = one widget icon so it never adds thickness). `Dock.qml` renders them with a single-line `GridLayout` (`flow`/`rows`/`columns` switch by orientation) fed by a `Repeater` over `config.widgetOrder`, mapping each token to a `Component` via `componentFor()`.
    - **Reconciliation**: `DockConfig::reconcileWidgetOrder()` (run on load and in `setWidgetOrder`) guarantees `apps` + every known widget token appear exactly once (missing ones appended), keeps the repeatable tokens as they are, and drops unknown tokens. This makes upgrades safe (new widgets appear automatically) and prevents a widget from ever disappearing — **per-widget visibility is still governed by the existing `show*` flags**, not by presence in `widgetOrder`. A repeatable token that is not listed in `isRepeatableToken()` gets dropped on the next load, silently.
    - **Dynamic separator (spring)** expands via `Layout.fillWidth`/`Layout.fillHeight` to push sections toward an extreme. It only has room to expand when the dock spans the whole edge (`panelMode` && `dockLength == 0`); otherwise it collapses to size 0 (**no-op**). `root.fillMain = panelMode && hasSpring` gates whether the layout fills the main axis; when **true the `alignment` (start/center/end) is ignored** (the spring dictates positioning); when false the existing `alignment` logic applies (unchanged behavior when no spring exists). Because a no-op alignment combo confuses users, `SettingsDialog` **disables the alignment combo** and shows an explanatory note whenever the dock spans the whole edge AND a spring is present (updated live on `panelModeChanged`/`widgetOrderChanged`/`dockLengthChanged`); removing the spring, disabling panel mode, or setting a fixed `dockLength` re-enables it.
    - **Widget/section drag & drop**: single-icon widgets (`volume`/`brightness`/`clock`/`autohide`) and `spring` are draggable to reorder (`DockConfig::moveSection(from,to)`), mirroring the app-icon `Drag`+`DropArea`+reparent pattern. Their click/wheel actions are dispatched from the section-level `MouseArea` (`root.sectionClick`/`sectionWheel`). **Multi-icon blocks** (`apps`, `systray`, `relanzadores`) are **drop-only anchors** (not draggable as a unit) so their inner per-icon mouse handling stays intact; reposition them by dragging widgets/springs around them.
    - **Add/remove separators**: right-click any widget → "Add dynamic separator" (`DockConfig::insertSpring(at)`) or "Add static separator" (`insertSeparator(at)`); right-click a separator → "Remove …" (`removeSectionAt(at)`, which only removes repeatable tokens). Neither kind is labelled or hover-highlighted: the delegate's `hovered` `Binding` is gated on `!isSpring && !isStaticSep` because their components declare no such property (it warns on every load otherwise).
    - `root.dragCount` tracks in-progress drags (app-icon and section) so autohide does not hide the dock mid-drag.
14. **Running-app colored background (`config.iconRunningBackground`)** — Optional per-app running indicator: instead of the dots, a rounded rectangle behind the icon filled with the icon's **dominant color** at ~0.85 opacity, shown only while the app has ≥1 window. Toggled from Settings → Widgets ("Colored background for running apps"), default off.
    - Dominant color is computed in C++ by `IconColorProvider::dominant(iconName, revision)` (`src/iconcolorprovider.cpp`): resolves the icon like `IconProvider`, samples a 32×32 pixmap, and picks the most frequent **vivid** color bucket (saturation/value gated), falling back to the average for monochrome icons. Cached per icon name; the `revision` arg (`theme.revision`) invalidates the cache and forces the QML binding to re-run on theme changes (same mechanism as `image://icon/name@rev`). Exposed as the `iconColors` context property (created in `dockwindow.cpp`).
    - In `Dock.qml` (apps delegate) the background grows **beyond** the icon cell via negative anchor margins — generously across the dock thickness (into the `thickness - iconSize` padding) and up to half of `config.spacing` along the dock — so the color is visible **without shrinking the icon**. Larger "Icon spacing" ⇒ larger highlight. When enabled, the running **dots** are hidden (`visible: !config.iconRunningBackground && …`).
14b. **The other two running-app indicators** — Three indicators coexist, each with its own flag in Settings → Widgets, all anchored to the icon **cell** (so app labels never displace them):
    - **Dots** (`config.iconRunningDots`, default on) — one dot per window on the screen-edge side.
    - **Edge line** (`config.iconRunningLine`, default off) — a single bar beside the icon, always on the screen-edge side (`config.edge` picks the anchor); the active window gets a longer (0.7 vs 0.45 × `iconSize`) and fully opaque bar.
    - **Colored background** (point 14).
    - Dots and line take their color from `IconColorProvider::contrasting(iconName, revision)` when the colored background is on, and from `dominant()` otherwise — over a fill of the dominant color, the dominant color itself would be invisible. `contrasting()` is the RGB inversion of the dominant, except for near-gray dominants (max−min < 40), which invert to another near-gray and are therefore snapped to black or white by luminance.

15. **Show-desktop widget (`showdesktop` token)** — Optional widget that toggles "show desktop" (peek). Gated by `config.showDesktopButton` (Settings → Widgets). The backend lives on `WindowMonitor` (exposed to QML as the `showdesktop` context property): `showDesktopSupported`, `showDesktopActive`, `toggleShowDesktop()`. `PlasmaBackend` uses the native `org_kde_plasma_window_management.show_desktop` request and tracks state via the `show_desktop_changed` event; `WlrBackend` emulates it by minimizing every window and restoring the ones it minimized (`AbstractWindow::unminimize()`). Null on X11 (widget hidden).
16. **Layout settings tab** — `SettingsDialog::createLayoutTab()` (`src/settingsdialog.cpp`) is where **every** separator is configured. It holds two lists plus the shared `separatorSize` spinbox:
    - **Section list** (`m_layoutList`) — `config.widgetOrder`: reorder (up/down → `moveSection`), rename a widget, add a static separator (`insertSeparator`) or a dynamic one (`insertSpring`), remove either (`removeSectionAt`, enabled only on a repeatable-token row). The manual alternative to in-dock drag & drop / right-click. Each kind of separator is numbered on its own count ("Static separator 1", "Dynamic separator 1").
    - **Apps-block list** (`m_appSepList`) — the pinned launchers with `separator1`/`separator2` drawn where they land, so those indices are set by moving a row instead of typing a number (they were `-1..99` spinboxes in General until 2026-08-02). Add inserts before the selected launcher (or after the last one), Up/Down shift by one launcher and **jump over the other separator** so the two never share an index, and both ends clamp to `[0, pinned.size()]`. Indices past the pinned count (they land among running windows the dialog cannot enumerate) are listed at the bottom rather than dropped.
      A checkbox under the buttons (*"Transparent separator (no line)"*) flips the selected slot's `separator<N>Transparent`; it is greyed out on a launcher row, like Remove/Up/Down, and its `setChecked()` runs inside a `QSignalBlocker` — without that, selecting the other separator would write the previous one's state onto it.
      **Selection is tracked by separator, not by row** (`m_appSepSelected`): every edit rebuilds the list and the row a separator sits on changes as it moves, so reading the current row would leave Up/Down acting on nothing after the first click. `reloadAppSeparatorList()` snapshots the member first, because both `clear()` and `setCurrentItem()` emit `currentRowChanged` and overwrite it mid-rebuild.
17. **AppMenu (Kickoff-like launcher)** — `src/appmenu.cpp` + `qml/AppMenuPopup.qml`. The menu button (`"menu"` token in `widgetOrder`) is gated by `config.showMenuButton` (Settings → Widgets → "Show application menu button"). The popup opens toward the opposite side of the dock edge. Keyboard interactivity is set to **exclusive** (1) on the dock's layer surface while open so the compositor grants keyboard focus; `requestActivate()` is called alongside, and both `onAboutToHide` + `onClosed` revert to none (0) as a safety net. The popup itself uses `modal: true` + `popupType: Popup.Window` so the compositor grants a **keyboard grab** to the xdg_popup (this is what actually lets the search `TextField` receive key events — the layer-surface interactivity alone is not enough). A 50 ms `Timer` retries `forceActiveFocus()` after the commit settles. Favorites are persisted in `config.menuFavorites`. The menu button block is drop-only (like `apps`/`systray`).
    - **XDG `.menu` submenus** (`src/xdgmenutree.{h,cpp}`, 2026-07-31) — grouping by the `Categories` key alone **cannot see** the submenus a user builds with KDE's menu editor nor the ones browsers create for their web apps: those are declared in `.menu` XML files and list their members **by filename**, and web-app `.desktop` files carry no `Categories` at all (so they all piled into `Other`). `loadXdgMenuTree()` reads `${XDG_MENU_PREFIX}applications.menu` through the config dirs and returns the submenus that list files explicitly; `AppMenu` appends them to the sidebar under the categories, nested ones indented. Deliberately a **subset** of the menu spec: `<Category>`/`<And>`/`<Or>`/`<Not>`/`<OnlyUnallocated>`, `<Layout>` and `<Move>` are ignored, since the category path already covers that ground. The tree is read once at construction (no `QFileSystemWatcher`).
    - **Two traps, both found by the probe**: (1) a relative `<MergeFile>` must be searched **through all the menu dirs, user first** — the system file ends with `<MergeFile>applications-kmenuedit.menu</MergeFile>` while the editor writes that file into `~/.config/menus/`, so resolving it next to the declaring file finds nothing and every hand-made submenu vanishes; (2) `<DefaultMergeDirs/>` must also merge plain `menus/applications-merged/`, not only the prefixed name, because that is where `xdg-desktop-menu` writes regardless of the session prefix. Installers also nest pointless single-child menus (NoMachine's `.menu` nests `Internet` five deep), so empty single-child chains are collapsed.
    - **Section keys** are prefixed and untranslated (`cat:__favorites__`, `cat:Development`, `menu:chrome-apps/Brave-Apps`) so a submenu can never collide with a category of the same name and the selection survives a language change.
    - **Edit the menus** — the widget is a **block**, so the section-level `MouseArea` that gives every draggable widget its context menu is disabled for it (`secMouse`, `enabled: sec.draggable`): `menuComp` carries its own `Menu` and its `menuMouse` had to start accepting `Qt.RightButton`. It offers "Edit menu…" (→ `appMenu.launchMenuEditor()`) and "Dock settings…", and mirrors `sectionMenu`'s `root.menuOpen` handling so the dock does not auto-hide while it is open. `AppMenu::launchMenuEditor()` resolves `config.menuEditorApp` (default `org.kde.kmenuedit`) through `DesktopEntryIndex::forAppId()` — so a bare `kmenuedit` also finds `org.kde.kmenuedit` — and falls back to running the value as a plain command, which keeps a hand-written value with arguments working. The app is picked in Settings → Menu with the same `QInputDialog::getItem` over the installed entries that `addPinnedApp()` uses.
    - **Icon size and grid density** — `config.menuColumns` goes up to **8**. The count is honoured *exactly* (the `GridView` is `columns * cellWidth` wide), so at 8 columns in a 540 px popup the cells fall to ~43 px and the names collapse to one letter: the lever is `menuPopupWidth`, and the spinbox tooltip says so. `config.menuAppIconSize` (px) drives the icon in **both** layouts of the list: the single-column rows (icon, then name and comment) and the multi-column cells (icon over name). `config.menuGridSpacing` (px) is the padding around a grid cell. Grid cells are sized **from the icon**, not from the available width — otherwise a handful of apps at 4 columns spread across the whole popup — and the `GridView` is then only `columns * cellWidth` wide so `menuColumns` still caps the row and the leftover space stays empty. `cellWidth` keeps a floor of ~10 characters (`FontMetrics.averageCharacterWidth`): sized purely from the icon, every name elides after four characters.
    - **Configurable icon**: the button icon is `config.menuIcon` (per-dock, default `applications-all`), chosen via `IconPickerDialog` in Settings → Widgets ("Menu icon").
    - **Configurable size**: the popup `width`/`height` bind to `config.menuPopupWidth`/`menuPopupHeight` (per-dock, default 540×460), set via two spin boxes in Settings → Widgets ("Menu width"/"Menu height"). (Border drag-resize was attempted but abandoned — an `xdg_popup` on Wayland can't be reliably user-resized by dragging; the config spinboxes are the robust approach.)
    - **Toggle close**: because the popup is `modal` + `Popup.Window`, the press that lands on the icon while it is open *dismisses* the popup before the `MouseArea.onClicked` fires — a naive `visible ? close() : open()` would reopen it. Fix: `AppMenuPopup` records `closedAt = Date.now()` in `onVisibleChanged` when hidden; the icon's `onClicked` suppresses reopening if `Date.now() - closedAt < 300` (so a second click on the icon toggles it closed).

18. **Script Runner widget (`scriptrunners` token)** — A dock button that runs a shell script. Mirrors the relanzadores architecture: `ScriptRunnersManager` (shared singleton, exposed to QML as `scriptRunners`) holds a **global collection** of `ScriptRunnerConfig` (`{id,title,iconName,script}`) persisted under `[scriptrunners]` in the shared `kdock.conf`. `run(id)` executes `QProcess::startDetached("sh", {"-c", script})` (fire-and-forget). **Per-dock visibility** reuses the exact relanzadores scheme: `DockConfig::scriptRunnersHidden` (primary dock, default all shown) / `scriptRunnersShown` (other docks, default none), filtered by `ScriptRunnersManager::visibleIds(primary,hidden,shown)` and `Dock.qml`'s `root.visibleScriptRunnerIds`. `qml/ScriptRunnerWidget.qml` is the on-dock icon (click → `scriptRunners.run(id)`, no popup). Settings → **Script Runner** tab: checkbox per runner (per-dock visibility, same logic as Relanzadores) + editor (title / icon / multiline script). The icon is chosen via `IconPickerDialog` (`src/iconpickerdialog.{h,cpp}`) — a **self-contained Qt Widgets** picker (no KDE Frameworks dep) that lists the current icon theme's icons (scans `themeSearchPaths()` for the active theme + `Inherits=` + `hicolor`, cached; searchable IconMode grid, results capped for responsiveness). Reusable for other icon fields. The `scriptrunners` block is a drop-only anchor like `relanzadores`.

19. **Config Import/Export (Settings → Backup tab)** — `ConfigArchive` (`src/configarchive.{h,cpp}`) exports/imports the **entire** configuration as a `.zip`: every `kdock*.conf` in the config dir (`ConfigArchive::configDir()` = dir of `DockConfig::settingsFilePath()`), i.e. the shared `kdock.conf` (enabledScreens, relanzadores, **script runners incl. their scripts**, favorites) + each per-dock `kdock-<dock>.conf`. Uses **QtCore's private `QZipWriter`/`QZipReader`** (`#include <private/qzipwriter_p.h>`; links `Qt6::CorePrivate`) — no external `zip`/`unzip` runtime dependency. Import: validates the archive contains `kdock.conf`, only accepts entries matching `^kdock[\w.#-]*\.conf$` (**anti zip-slip**, no path separators), **backs up** the current config to `backup-<timestamp>/`, does a **clean replace** (removes current `kdock*.conf` then writes the archive's), and the Settings dialog **relaunches kdock** (`QProcess::startDetached(applicationFilePath())` + `quit()`) so all docks/managers reload. The `Backup` tab is always present (config is global, not per-dock).

19a. **Favorites export/import (Settings → Application menu)** — Separate from the full `.zip` backup: `ConfigArchive::exportFavorites()` / `importFavorites()` write and read a small JSON file `{ "app": "kdock-favorites", "version": 1, "favorites": [<.desktop ids>] }`. The import validates the `app` marker and that `favorites` is an array of strings before touching the config.

19b. **Removable disks widget (`disks` token)** — Native reimplementation of Plasma's device-notifier tray applet (the chosen "Ruta C" over embedding plasmoids). `DisksControl` (`src/diskscontrol.{h,cpp}`, shared singleton, exposed to QML as `disks`) speaks raw **UDisks2** over the **system** D-Bus (`org.freedesktop.UDisks2`, no extra library): `GetManagedObjects` (demarshalled into `QMap<QDBusObjectPath, QMap<QString, QVariantMap>>`), keeping Block objects that have a `Filesystem` interface, `IdUsage=="filesystem"`, not `HintIgnore`, and whose backing `Drive` is **removable/usb** (this drops `loop*` snap mounts, whose Drive is `/`). `volumes()` returns maps `{path,drive,label,device,mountPoint,mounted,ejectable,size}`; `mount`/`unmount` call `Filesystem.Mount`/`Unmount`, `eject` calls `Drive.Eject` (empty `a{sv}` options), `openMount` runs `xdg-open`. Live updates: connects the ObjectManager `InterfacesAdded`/`InterfacesRemoved` **and** `PropertiesChanged` (empty path = any object, catches mount/unmount) to a 300 ms-debounced `rescan()`. UI: `disksComp` block in `Dock.qml` (icon `drive-removable-media-usb`, left-click opens `qml/DisksPopup.qml` — a **non-modal** popup, no keyboard grab needed, listing volumes with mount/unmount + eject buttons; click a mounted row → open in file manager). Opt-in via `config.showDisks` (Settings → Widgets → "Disks"); drop-only block like `clipboard`. Threaded through `main.cpp` (singleton) → `DockManager::Shared::disks` → `DockWindow` ctor → context property.

19c. **Network widget (`network` token) + the Redes tab** — The other native Ruta C widget. The low-level D-Bus plumbing shared by widget and editor is `src/nmdbus.{h,cpp}`: service/path/interface constants, `getAll()`/`getProp()`, `objectPaths()`, `connectionSettings()`/`connectionSecrets()`, SSID `ay`↔`QString`, the AP security decoding (`apSecurityLabel()`/`apKeyMgmt()` over `Flags`/`WpaFlags`/`RsnFlags`), and **`registerTypes()`** — `qDBusRegisterMetaType<NMSettingsMap>()` (`a{sa{sv}}`) and `QList<QVariantMap>` (`aa{sv}`), which reading does not need but **writing does**.

`NetworkControl` (`src/networkcontrol.{h,cpp}`, shared singleton, exposed to QML as `network`) keeps the dock-facing half. `rescan()` (debounced 500 ms, driven by any `PropertiesChanged` under the NM service) reads NM's `GetAll` (`State`, `WirelessEnabled`, `NetworkingEnabled`, `PrimaryConnectionType`, `PrimaryConnection` → active `Id`), locates the Wi-Fi device, and derives the themed `iconName`. On top of `connections()` / `activate()` / `deactivate()` / `setWifiEnabled()` it now has:
  - `accessPoints()` — `Device.Wireless.AccessPoints` + one `GetAll` per AP (`Ssid` is `ay`, `Strength` is a **byte**), deduplicated by SSID keeping the strongest, each flagged `saved` (one `ListConnections` pass for the whole list, not one per SSID) and `active`, with a `band` and a security label. Sorted active → saved → **strength bucketed by 20** → SSID: sorting by the raw strength reorders the list on every rescan and rows jump out from under the pointer mid-click.
  - `setApTrackingEnabled(bool)` — the popup turns it on while open. Without it every rescan would do ~40 blocking round trips with nobody looking.
  - `requestScan()` + the `scanning` property (derived from `Device.Wireless.LastScan`).
  - `connectToAccessPoint(ssid, password, apPath)` — **joining a new network needs no secret agent**: an already-saved SSID is just activated, otherwise `AddAndActivateConnection` carries the settings *and* the secret inline with `psk-flags=0`, so NM stores it itself (what `nmcli device wifi connect` does). The key management comes from the AP's own flags, so a WPA3-only network gets `sae` and a WEP one gets `wep-key0` + `wep-key-type=2` instead of a psk.
  - `forgetConnection(path)` and an `errorOccurred(QString)` signal — every call goes through `QDBusPendingCallWatcher` and a rejection (bad password, no authorization) is shown in the popup instead of vanishing.

UI: `networkComp` block in `Dock.qml`. **Left click** opens `qml/NetworkPopup.qml`: header (primary connection + Wi-Fi `Switch`), then **nearby networks** with signal bars, a lock icon, `Conectado`/`Guardada`/band and a *Escanear* button — picking a secured network that is not saved opens a password field with a *Conectar* button right under its row — and below them the **saved connections that are not in range** (Ethernet, networks elsewhere), filtered by AP `connPath` so nothing appears twice. Right-click on a saved network offers *Olvidar esta red*; a red strip shows the last NM error. The popup is `modal: true` + `dockWindow.setKeyboardInteractive(true/false)` + the `focusRetry` timer, the same keyboard dance as `ClipboardPopup` — the password field needs key events and the layer surface is inert. **Right click on the widget** (blocks never reach `secMouse`, so this is its own `Menu`, like the clipboard's) offers *Configurar redes…* → `dockWindow.openNetworkSettings()` → `SettingsDialog::showNetworkTab()`, *Buscar redes Wi-Fi* and a checkable *Wi-Fi*.

**Settings → Redes** is the full editor, modeled on KDE's network KCM and reached the same way the mixer is (widget right-click). `NetworkSettings` (`src/networksettings.{h,cpp}`) is the GUI-free backend: `devices()` (Ethernet + Wi-Fi only), `connections()`, `settings()`/`secrets()`, `addConnection()`/`updateConnection()`/`deleteConnection()`/`activate()`/`deactivate()` (each returning the D-Bus error message or an empty string), `deviceIpSummary()` for the read-only device page, and the two **pure** conversions `ipFromSettings()`/`ipToSettings()` between an NM ipv4/ipv6 group and an `IpConfig` struct. Those write the modern `address-data` / `route-data` (`aa{sv}`) + `gateway` + **`dns-data`** (`as`) and strip the legacy `addresses`/`routes`/`dns` so there is only one source of truth — verified by round-tripping a real connection: NM 1.54 accepts `dns-data` and mirrors it into the legacy numeric `dns` itself, so no network-byte-order conversion is needed. `NetworkSettingsWidget` (`src/networksettingswidget.{h,cpp}`) is the tab: a `QTreeWidget` of *Dispositivos* + *Conexiones* with Agregar (Wi-Fi/Ethernet) / Borrar / Conectar on the left, and on the right either a read-only device page or `ConnectionEditor` (`src/connectioneditor.{h,cpp}`) — an inner horizontal `QTabWidget` with **General / Wi-Fi / Seguridad / Ethernet / IPv4 / IPv6** (the middle pages swap with the connection type) plus Aplicar/Descartar, where the two IP pages are one reusable `IpConfigPage` and *Rutas…* opens `RoutesDialog` (`src/routesdialog.{h,cpp}`). Security covers WPA/WPA2-Personal (`wpa-psk`), WPA3 (`sae`), WEP and open; the saved password is seeded from `GetSecrets` (system-owned secrets come back without a polkit prompt, agent-owned ones — plasma-nm's, in KWallet — come back empty and the field is left blank). A connection carrying an `802-1x` group is shown but not saved, so the editor cannot silently drop it. `collect()` starts from the loaded map so groups the editor does not model survive a save. **Ruta C complete (disks + network).**

20. **Session / power + settings widgets** — `PowerControl` (`src/powercontrol.{h,cpp}`, exposed to QML as `power`, KDE-only via the session check) fires KDE session actions over D-Bus: **logout/reboot/shutdown** via `org.kde.LogoutPrompt` (`promptLogout`/`promptReboot`/`promptShutDown` — shows KDE's **confirmation** screen), **lock** via `org.freedesktop.ScreenSaver.Lock`, **suspend** via `org.freedesktop.PowerManagement.Suspend`. Used in three places (all gated by `power.available`):
    - **`session` token** (`sessionComp` in `Dock.qml`) — a **block** button (icon `system-shutdown`) whose `MouseArea` opens a `Menu` (`Popup.Window`, `IconMenuItem`) with the five actions. Gated by `config.showSessionButton`.
    - **App-menu footer** (`AppMenuPopup.qml`) — a right-aligned row of five icon buttons, gated by `config.showMenuPower` (default **on**).
    - **`settings` token** (`settingsComp`) — a **single-icon** draggable widget (icon `configure`); click → `dockWindow.openSettings()` via `sectionClick("settings")`. Gated by `config.showSettingsButton`. No backend (reuses `DockWindow::openSettings`).
    Checkboxes for all three live in Settings → Widgets.

## QML ↔ C++ Interface Summary

| C++ Class | QML Property / Method | Notes |
|-----------|----------------------|-------|
| `AppearanceControl` | `appearance.*` | `iconThemes()`, `colorSchemes()` (listas de mapas con id/name/current/**fav**, + bg/fg/sel en los esquemas, favoritos primero), `refreshIfStale()`, `applyIconTheme(id)`, `applyColorScheme(id)`, `isFavorite(kind,id)`/`setFavorite(kind,id,on)` (kind = `"icons"`/`"colors"`) + `currentIconTheme`/`currentColorScheme`, `keepPickerOpen` (lectura/escritura, compartida por todos los pickers) y las señales `changed`/`favoritesChanged`/`keepPickerOpenChanged` |
| `DockConfig` | `config.*` | Exposed as context property; many `Q_PROPERTY` values. Section layout: `widgetOrder` (QStringList), `moveSection(from,to)`, `insertSpring(at)`, `insertSeparator(at)` (static `sep` token), `removeSectionAt(at)`, `separatorSize` (px, both kinds of static separator). Dock length: `dockLength` (int 0-100, 0=auto, >0=% of screen edge). App-icon labels: `iconLabelMode` (0=icon only/default, 1=name below, 2=name at right, 3=name only, 4=name above, 5=name at left), `iconLabelWidth` (a **cap**), `iconLabelFontSize` (0=auto) + read-only `iconLabelFontPx`/`iconLabelLineHeight`/`iconLabelGap`/`dockThickness`/`effectiveLabelWidth`, and `setMeasuredLabelWidth(px)` (QML reports the widest name it draws; see App-icon labels). Section labels: `widgetLabelMode` (same values, no 3), `widgetName(token)`, `setWidgetName(token,name)` + read-only `widgetNamesRevision`. Auto-shrink: `autoShrinkIcons` (bool), `autoShrinkMinIconSize` (px). Negritas: `labelBold` (bool, todos los nombres del dock). Renglones: `labelLines` (1 o 2 — con 2 los nombres se envuelven en vez de elidirse) + read-only `iconLabelBoxHeight`. Modo oscuro: `darkModeActive` (bool **resuelto**, read-only — el que QML tiene que leer), `darkMode` (flag propio del dock, solo vale cuando el alcance es por dock), `darkAccent`/`darkBackground` (QColor, app-wide), `setDarkModeActive(on)`, `showDarkMode` — todas notifican con `darkModeChanged`. Íconos de apps: `showAppIcons` (bool, default true — en false el dock es una barra de solo widgets y la sección `apps` sale de `dockThickness()`). Pickers de apariencia: `showIconThemes`, `showColorSchemes`. Widget Control Manager: `showControlManager`, `controlManagerIcon`, `controlManagerDisplay` (0 solo ícono / 1 ícono+texto / 2 solo texto), `controlManagerText` (vacío = el reloj, con `controlManagerFormat`), `controlManagerFontSize` (0 = automático: la fuente del reloj y, si no, una fracción del ícono; independiente de los modos de etiqueta — ver *Control Manager*). App menu (all in the shared-when-enabled menu group): `menuIcon`, `menuPopupWidth`/`menuPopupHeight`, `menuColumns` (1-8), `menuAppIconSize` (px, both list layouts), `menuGridSpacing` (px), `menuEditorApp` (.desktop id or command), `showMenuPower`, `menuFavorites` |
| `Theme` | `theme.background`, `theme.foreground`, `theme.highlight`, `theme.revision` | Live-reloading color scheme |
| `Translations` | (sin context property) | La capa de traducciones se sirve a QML a través del `QTranslator` instalado: los `qsTr()` no cambian. Lo único que QML ve del tema es `config.widgetName(token)` (ya traducido) y `config.widgetNamesRevision`, que se bumpea también al cambiar de idioma |
| `DockModel` | `dockModel` (model) | `activate(row)`, `launch(row)`, `togglePinned(row)`, `closeAll(row)`, `windowList(row)`, `moveItem(from, to)`, `activateWindow(row, idx)`, `syncWindows()`, `sendToDesktop(row, position)` (mueve **todas** las ventanas del ícono a un escritorio 1-based, sin seguirlas). Roles: `name`, `iconName`, `pinned`, `windowCount`, `active`, `minimized`, `title`, `isSeparator`, `separatorTransparent` (el separador inline que ocupa lugar y no dibuja línea) |
| `DockWindow` | `dockWindow` | `setHidden(bool)`, `openSettings()`, `openSettingsToDock()`, `createEmptyDock()`, `deleteDock()`, `restart()`, `quit()` |
| `VolumeControl` | `volume` | `available`, `volume`, `muted`, `iconName`, `setVolume(v)`, `toggleMute()`, `refresh()` (called on hover to avoid acting on a stale cache) |
| `AudioControl` | (no context property) | Mixer backend for Settings → Audio only: `available`, `maxVolume`; devices/streams via C++ API |
| `IconProvider` | `image://icon/name@rev[@themeId]` | `theme.revision` appended to bust cache on theme change; optional `themeId` resolves that icon against another icon set (widget icons adapted to the dock color) |
| `PreviewConfig` | `config.*` (binario `kdock-previews`) | Contexto de `PreviewStrip.qml`: `edge`, `alignment`, `opacity`, `panelColor`/`panelColorSet`/`panelPresetColors`/`resetPanelColor()`, `stripThickness` + read-only `stripThicknessPx`/`cardWidthPx`/`pad`, `stripLength` (0=todo el borde), `screenMargin`, `reserveSpace`, `autohide`, `showTitles`, `showScrollBar`, `cardSpacing`, `autoFitCards`, `fitMinCardWidth`, `captureMode` (0=una captura al primer foco/default, 1=periódico), `refreshInterval`, `activeRefreshInterval` (solo en modo 1), `includeMinimized`, `currentDesktopOnly`, `thisMonitorOnly`, `screenName` |
| `PreviewModel` | `previews` (model) | Roles `uuid`, **`thumbId`** (uuid sin llaves: el único que va en una URL), `title`, `appName`, `iconName`, `thumbRevision`, `aspect`, `active`, `minimized`; read-only `cardScale` (auto-fit); `activate(row)`, `closeWindow(row)`, `toggleMinimize(row)`, `refreshNow(row)` (no-op salvo en modo periódico), `setVisibleRange(first,last)`, `setAvailableLength(px)` (auto-fit) |
| `PreviewWindow` | `previewWindow` | `setHidden(bool)`, `openSettings()`, `restart()`, `quit()` |
| `TileMenuLauncher` | `tileLauncher` (en kdock) | `toggle(screenName)`, `openSettings()`. Todo el acoplamiento del dock con `kdock-tilemenu`: si el proceso corre, D-Bus; si no, lo lanza |
| `TileConfig` | `tileConfig.*` (binario `kdock-tilemenu`) | Grilla: `columns` (0=según el ancho), `cellSize`, `cellStretch`, `cellMin`/`cellMax`, `cellSpacing`. Apariencia: `sidebar` (0 izq/1 der/2 oculta), `sidebarWidth`, `showIcons`, `showLabels`, `iconScale` (%), `labelPosition`, `labelBold` (default **true**), `groupTabs` (borde de la barra de solapas), `backgroundMode`, `backgroundColor`/`backgroundColorSet`, `backgroundOpacity`, `backgroundImage`/`backgroundImageUrl`, `presetColors` (read-only, los ocho colores rápidos del `kdock.conf` compartido). Comportamiento: `showSearch`, `showPower`, `showLetterIndex`, `closeOnLaunch`, `closeOnFocusLoss`, `keepOpen`, `rememberSection`, `lastSection`. **Una sola señal `settingsChanged` para todas** |
| `TileLayout` | `tileLayout` | El motor, todo `Q_INVOKABLE`: `groups(section)` (`{index, title, tiles, rows}` por solapa), `rowsOfGroup(section,group)`, `isCustomized(section)`, `dropKind(section,id,group,col,row)` (0 libre / 1 swap / 2 rechazo, **read-only**, para el fantasma), `moveTile(...)` (false = rechazado), **`moveTileToGroup(section,id,group)`**, `resizeTile(...)`, `setTileProperty(section,id,key,value)` (`bg`/`image`/`label`/`icon`/`showIcon`/`showLabel`), `resetTile`, `addGroup`/`renameGroup`/`moveGroup`/`removeGroup`, `resetSection`/`resetAll`, `exportToFile`/`importFromFile`, `setAutoColumns(n)` |
| `TileModel` | `tiles` (model) | Roles `tileId`, `name`, `comment`, `icon`, `favorite`, `group`, `col`, `row`, `span`/`vspan`, `background`, `image`, `showIcon`/`showLabel`. Props `section` (rw), `query` (rw), `currentGroup` (rw: la solapa visible), `searching`, `rows` (del grupo actual), `groups`, `customized`; `get(row)`, `indexOfLetter(letra)`, `availableLetters()` |
| `TileWindow` | `win` | `hideMenu()`, `launch(id)`, `openSettings()`, `quitApp()` + los diálogos modales que el menú del mosaico necesita: `pickIcon`, `pickColor`, `pickImage`, `promptText`, `confirm` (cada uno bloquea el cierre por pérdida de foco mientras está arriba) |
| `ControlManagerLauncher` | `cmLauncher` (en kdock) | `toggle(screenName)`, `showSection(id, screenName)`, `openSettings()`. Todo el acoplamiento del dock con `kdock-controlmanager`: si el proceso corre, D-Bus; si no, lo lanza |
| `DockService` | (sin context property) | `org.kdock.Dock` en `/Dock`: `openSettings(dockId)`, `restart()`, `darkMode()`, `setDarkMode(b)`, `toggleDarkMode()`, `dockIds()`, `dockScreens()`, `primaryDockId()` + señal `darkModeChanged(b)`. Lo consume `DockLink` desde el panel |
| `CmConfig` | `cmConfig.*` (binario `kdock-controlmanager`) | Ventana: `edge`, `alignment`, `panelWidth`/`panelHeight` (+ `panelWidthPercent`/`panelHeightPercent`, 0 = usar los px), `screenMargin`, `keepOpen`, `closeOnFocusLoss`, `closeOnLeave`, `panelWidthFor(w)`/`panelHeightFor(h)`. Apariencia: `backgroundMode`, `backgroundColor`/`backgroundColorSet`, `backgroundOpacity`, `backgroundImage`/`backgroundImageUrl`, `cornerRadius`, `labelBold`, `tabsPosition`, `showTabIcons`, `showCardTitles`, `presetColors` (read-only, del `kdock.conf` compartido), `buttonWidth`/`buttonHeight` (mínimos de los `CmButton`, 0 = natural), `fontSize` (0 = predeterminado 12 px) + `fontScale` read-only (multiplica cada `font.pixelSize` del panel), `iconTheme` (iconset propio; vacío = el par por luminancia). Grilla: `columns` (0=según el ancho), `cellSize`, `cellStretch`, `cellMin`/`cellMax`, `cellSpacing`. Secciones: `sectionOrder`, `enabledSections`, `principalCards`, `sectionEnabled(id)`/`setSectionEnabled(id,on)`, `cardEnabled(id)`/`setCardEnabled(id,on)`, `moveSection(from,to)`, `visibleTabs()`, `rememberTab`/`lastTab`, `wallpaperScript`. **Tres señales**: `settingsChanged` (repinta), `windowChanged` (re-commitea la superficie), `sectionsChanged` (rehace solapas y grilla) |
| `CmLayout` | `cmLayout` | El motor de la grilla de *Principal*, todo `Q_INVOKABLE`: `rows()`, `dropKind(id,col,row)` (0 libre / 1 swap / 2 rechazo, **read-only**, para el fantasma), `moveCard(id,col,row)` (false = rechazado), `resizeCard(id,w,h)` (nunca falla: reubica), `setCardProperty(id,key,value)` (`bg`/`label`/`showTitle`), `resetCard`, `resetAll`, `exportToFile`/`importFromFile`, `setAutoColumns(n)` |
| `CmModel` | `cards` (model) | Roles `cardId`, `name`, `icon`, `col`, `row`, `span`/`vspan`, `background`, `showTitle`; prop `rows`; `get(row)` |
| `CmWindow` | `win` | `hidePanel()`, `currentTab` (rw: "" = Principal), `sections` (la tabla de `CmSections`), **`iconSuffix`** (el sufijo de TODA URL `image://icon` del panel: revisión del tema + iconset que se lee sobre este fondo), `openSettings()`, `quitApp()`, `restartSelf()`, `launchDesktop(id)`, `runCommand(prog,args)`, `runScript(path,screen)` (con `KDOCK_SCREEN`) + los modales `pickColor`, `promptText`, `confirm` |
| `ScreenBrightness` | `screens` | `available`, `count`, `displays()` (→ `{name, label, internal, value 0..1, brightness, max}`), `setBrightness(name, ratio)`, `setAll(ratio)`, `refresh()`. Brillo **por monitor** vía PowerDevil; los nombres de los objetos son volátiles y no se cachean |
| `MprisControl` | `mpris` | `available`, `playerId`, `identity`, `title`, `artist`, `album`, `artUrl`, `status`, `playing`, `canGoNext`/`canGoPrevious`/`canPause`/`canSeek`, `position`/`length` (µs), `players()`, `setPlayer(id)`, `playPause()`/`play()`/`pause()`/`stop()`/`next()`/`previous()`, `seekToRatio(r)`, `raisePlayer()`, `setMonitoring(on)` (el sondeo de posición solo mientras se mira) |
| `DockLink` | `dock` | `available`, `darkMode` (rw), `toggleDarkMode()`, `openSettings(dockId)`, `restartDock()`, `docks()`. Cliente de `org.kdock.Dock`; `available` en false es normal (kdock apagado) y las tarjetas lo usan como reja |
| `ThumbnailImageProvider` | `image://thumb/<thumbId>@<rev>` | Captura escalada de una ventana; `rev` (de `thumbRevision`) invalida la caché de QtQuick. 1×1 transparente cuando todavía no hay captura, y un aviso por stderr si la clave no resuelve. **`thumbId`, no `uuid`** (QUrl percent-codifica las llaves) |
| `IconColorProvider` | `iconColors.dominant(iconName, revision)`, `iconColors.contrasting(...)` | Dominant icon color (QColor) for the running-app background; `contrasting()` is the color for the dots/edge line drawn *over* that background. Cached, revision-invalidated |
| `VirtualDesktops` | `virtualDesktops` | `count`, `current` (posición **1-based**, 0 = KWin no contesta), `names`, `nameOf(position)`, `switchTo(position)`. Lo usan el widget `pager` y el submenú *Escritorio* de los íconos de apps |
| `WindowMonitor` | `showdesktop` | `showDesktopSupported`, `showDesktopActive`, `toggleShowDesktop()`; null on X11 |
| `BrightnessControl` | `brightness` | `available`, `brightness`, `setBrightness(v)`, `increase()`, `decrease()` |
| `ClockWidget` | `clock` | `timeString`, `dateString`, `format24h`, `showDate`, `showSeconds` |
| `ClockWidget2` | `clock2` | Same API as `ClockWidget`; rendered with a larger styled tooltip |
| `OverviewControl` | `overview` | `available`, `active`, `toggle()` (KWin Overview via kglobalaccel) |
| `DesktopControl` | `desktopControl` | `available`, `moveToNextDesktop()` (KWin "Window to Next Desktop") |
| `MonitorControl` | `monitorControl` | `available` (KDE session check), `moveToNextScreen()` (KWin "Window to Next Screen"), `moveToPreviousScreen()` ("Window to Previous Screen", right click) |
| `ActiveWindowControl` | `activeWindow` | `available` (hay backend de ventanas), `closeActive()`, `sendActiveToNextDesktop()` (protocolo plasma-window, **no** atajo: no cambia de escritorio). Token `closewindow`, ícono `window-close` |
| `MaxMinControl` | `maxmin` | `available` (KDE session check), `maximize()` (KWin "Window Maximize", un *toggle*: también restaura), `minimize()` ("Window Minimize"). Token `maxmin`, ícono `window-maximize` |
| `PowerControl` | `power` | `available` (KDE), `logout()`, `reboot()`, `shutdown()` (LogoutPrompt), `lock()`, `suspend()` |
| `SystrayModel` | `systray` | `count`, `activate(row, x, y)`, `contextMenu(row, x, y)`, `secondaryActivate(row, x, y)`. Item menu (DBusMenu): `requestMenu(row)`, `menuTree(row)`, `triggerMenuItem(row, id)`, `menuAboutToShow(row, id)`, `setMenuOpen(row, open)` + signals `menuReady(row)` / `menuFailed(row)` / `menuInvalidated(row)`; roles include `hasMenu` and `itemIsMenu`. One per dock; `config.showSystray` decides which dock draws it |
| `RelanzadoresManager` | `relanzadores` | `count`, `get(id)`, `createRelanzador(title)`, `removeRelanzador(id)` |
| `RelanzadorConfig` | (via relanzadores.get()) | `id`, `title`, `iconName`, `pinned`, `showVolume`, `showBrightness`, `showClock`, `showAutohide`, `showSystray`, `hasSubRelanzador`, `subRelanzadorId`, `nestingLevel` |
| `DesktopEntryIndex` | `apps` | (passed to QML for icon resolution) |
| `AppMenu` | `appMenu` | `sections()` (`{key,label,icon,depth}`; replaced `categories()`), `appsInCategory(key)`, `search(query)`, `favorites()`, `launch(id)`, `launchMenuEditor()`, `isFavorite(id)`, `addFavorite(id)`, `removeFavorite(id)`, `toggleFavorite(id)`, `moveFavorite(from,to)` |
| `BatteryControl` | `battery` | `available`, `profilesAvailable`, `percent`, `charging`, `iconName`, `profile`, `setProfile(id)` (UPower + power-profiles-daemon, system bus) |
| `DisksControl` | `disks` | `available`, `volumes()` (→ removable drives), `mount(path)`, `unmount(path)`, `eject(path)`, `openMount(path)` (UDisks2) |
| `NetworkControl` | `network` | `available`, `iconName`, `primaryName`, `wifiEnabled`, `wifiAvailable`, `scanning`, `connections()`, `accessPoints()` (→ `{ssid, strength, security, secure, saved, active, path, connPath, band}`), `activate(path)`, `deactivate(path)`, `requestScan()`, `connectToAccessPoint(ssid, password, apPath)`, `forgetConnection(path)`, `setApTrackingEnabled(on)`, `setWifiEnabled(on)`, signal `errorOccurred(msg)` (NetworkManager). Joining a new Wi-Fi by password needs **no** secret agent: the psk travels inline with `psk-flags=0` |
| `ScriptRunnersManager` | `scriptRunners` | `count`, `get(id)`, `removeScriptRunner(id)`, `visibleIds(isPrimary, hidden, shown)`, `run(id)` |
| `ClipboardHistory` | `clipboardHistory` | `count`, `captureImages`, `entries(query)` (→ `{text, preview, isImage, file, imageUrl, width, height}`), `setClipboard(text)`, `setClipboardImage(file)`, `clearHistory()`, `openInEditor()`, `saveHistoryDialog()`; shared singleton, text + images, 50-entry / 20-image cap, JSON index + PNGs (plus a plain-text export). Captured in the background over `ext-data-control-v1` |

### Trap: Popup.Windows created at component-instantiation time

Every `ToolTip`, `Menu` or `Popup` with `popupType: Popup.Window` opens its own
`QQuickWindow` backed by a **dedicated `QSGRenderThread`** and **two Mesa driver threads**
per GL context. Those windows are created on first show and, once presented, stay alive
**for the lifetime of the process** — there is no code in QtQuick.Controls that destroys
them after close(). The result is monotonic growth in threads, RSS and GPU buffers with
every hover / right-click / popup a user ever opens on any dock.

Two fixes, both with precedent measured in `tilemenu/` (783 MB → 245 MB):

1. **ToolTips → attached `ToolTip`** (QtQuick.Controls attached properties). One shared
   instance per `QQuickWindow` instead of one per item. Apply on the root `Item` of the
   delegate, above the `MouseArea` whose ids the binding references. Custom `contentItem`
   cannot be expressed via the attached API and must stay inline (clock2's styled tooltip).

2. **Click popups → `Loader { active: false }` + 30 s idle `Timer`**. The popup lives in
   `sourceComponent`, created on first use and torn down after 30 s of being closed.
   `closedAt` (the anti-reopen guard) moves from the popup to the `Loader`. When a click
   removes the popup from the scene **without closing it** (e.g. a click inside the popup
   itself), the `Timer` must **not** tear the popup down — only start on `onVisibleChanged`
   with `!visible`. All references to popup ids by name (`menuPopup.visible`, etc.) must be
   re-routed through `loader.item && loader.item.visible`.

## Potential Improvement Areas

- Animation smoothness (custom vs. QML `Behavior`).
- Additional widgets (battery, etc.). ✅ Done: overview, move-to-next-desktop, move-to-next-monitor, second clock (styled tooltip), next-wallpaper (per-monitor), script runner (`sh -c`), session/power menu, kdock-settings button, clipboard history (text-only, plain-text persistence — Wayland background-capture caveat, see **Clipboard history**).
- **Native Plasma-tray replacements ("Ruta C")** — ✅ **DONE** (2026-07-21): rather than embedding Plasma plasmoids (they are QML KPackages, not standalone binaries, loadable only by the Plasma runtime), kdock reimplements the tray applets natively over the same backends. Added **Disks** (`DisksControl`, UDisks2 — see point 19b) and **Network** (`NetworkControl`, NetworkManager — see point 19c), joining the existing volume/brightness/battery/clipboard controls. Also added an **independent systray icon scale** (`config.systrayIconScale`, see point 12). Network caveat: joining a new Wi-Fi by password needs a NM secret agent (not implemented); only saved connections activate.
- **Multi-instance (up to 3 docks per monitor)** — ✅ **DONE** (2026-07-15): opt-in per (monitor, slot) via dockId, per-dock config files, hotplug, systray in any single dock (2026-08-04; primary-only before that), relanzadores/scriptrunners per-dock (opt-in checkbox), auto free-edge for new docks. See **DockManager**. Known rough edge: changing the *primary* monitor at runtime recreates the affected docks (brief flicker).
- Configurable keyboard shortcuts (e.g., Super+num to launch/focus).
- Customizable context menu / plugin system.
- Icon bounce / progress indicators.
- Better handling of `.desktop` files with `OnlyShowIn` / `NotShowIn`.
- Task grouping / window previews (would require more compositor protocols).
- **Systray / StatusNotifierItem support** — ✅ **FIXED** (2026-07-15): was talking to the wrong bus names (`org.freedesktop.*`) so it saw no items on KDE. Now uses `org.kde.*` throughout (watcher, item interface, SNI change signals), reads `RegisteredStatusNotifierItems` as a property, parses concatenated `service+path` ids, and renders pixmap-only icons via `image://systray`. See point 12.
- **Multi-monitor bugs** — ✅ **FIXED** (2026-07-14):
  - Bug 1: Dock not moving between displays when manually selecting in Settings. Root cause: `LayerSurface` resolved `wl_output` from `window->waylandScreen()` which could lag behind after `destroy()+setScreen()+show()`. Fix: derive `wl_output` from `QWindow::screen()->handle()` instead, with fallback.
  - Bug 2: Dock not detecting already-open apps when starting on a non-primary monitor. Root cause: Wayland protocol events (`window_with_uuid` from KWin) are queued during construction and dispatched after `app.exec()` starts, but `DockModel::rebuild()` runs before the event loop. Fix: deferred `syncWindows()` call 200 ms after construction to catch missed windows.
- **Relanzador (nested dock widget)** — ✅ **IMPLEMENTADO y FUNCIONAL** (ver `relanzador.md`). Incluye:
  - `RelanzadorConfig`, `RelanzadorModel`, `RelanzadoresManager` (C++)
  - `RelanzadorWidget.qml`, `RelanzadorPopup.qml` (QML)
  - Persistencia en QSettings, popup con modelo real, límite de 2 niveles de anidamiento
  - UI de configuración en `SettingsDialog` (pestaña "Relanzadores": alta/baja + editor de apps) — ✅ hecho
  - **Fix**: `RelanzadorPopup` debe ser `popupType: Popup.Window` para no quedar recortado dentro de la superficie del dock (igual que menús/tooltips). Sin esto, el popup no muestra los lanzadores.
  - Pendiente: widgets opcionales dentro del popup (hoy placeholders vacíos), apertura de sub-relanzador anidado, drag & drop entre relanzadores, menú contextual + selector de icono.

