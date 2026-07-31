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
  windowmonitor.{h,cpp}   — Backend factory; AbstractWindow base class
  taskmanager.{h,cpp}     — wlroots backend: zwlr_foreign_toplevel_management_v1
  plasmawindowmonitor.{h,cpp} — KWin backend: org_kde_plasma_window_management
  desktopentry.{h,cpp}    — XDG .desktop file indexer and launcher (no KDE code)
  dockmodel.{h,cpp}       — QAbstractListModel merging pinned launchers + running windows + separators
  dockconfig.{h,cpp}      — QSettings-backed persistent config, exposed to QML
  appmenu.{h,cpp}          — XDG application menu backend (Kickoff-like): categories, search, favorites
  theme.{h,cpp}           — KDE color scheme parser from ~/.config/kdeglobals with live-reload
  iconprovider.{h,cpp}    — QQuickImageProvider exposing XDG themed icons as image://icon/<name>
  iconcolorprovider.{h,cpp} — Dominant-color extractor for icons (QML "iconColors"); tints the running-app background
  dockwindow.{h,cpp}      — QQuickView layer-shell surface; maps DockConfig to layer-shell props
  settingsdialog.{h,cpp}  — Qt Widgets configuration dialog
  volumecontrol.{h,cpp}   — PipeWire volume via wpctl/pactl (no libpulse linkage); tiny default-sink indicator for the dock widget
  audiocontrol.{h,cpp}    — Full mixer backend for Settings → Audio (outputs/inputs/per-app streams via pactl); separate from VolumeControl
  batterycontrol.{h,cpp}  — Battery level/state via UPower + power profiles via power-profiles-daemon (system bus)
  diskscontrol.{h,cpp}    — Removable drives via UDisks2 (mount/unmount/eject/open); token disks
  networkcontrol.{h,cpp}  — Connections + Wi-Fi state via NetworkManager; token network
  clipboardhistory.{h,cpp} — Text-only clipboard history singleton (plain-text persistence in the XDG data dir)
  clockwidget.{h,cpp}     — Live clock with configurable 12/24h, date, seconds
  clockwidget2.{h,cpp}    — Second clock (same display) with a larger styled tooltip
  brightnesscontrol.{h,cpp} — Screen brightness via brightnessctl (no extra libraries)
  overviewcontrol.{h,cpp} — Toggles KWin's Overview effect via kglobalaccel (KDE only)
  desktopcontrol.{h,cpp}  — Moves active window to next virtual desktop via KWin shortcut (KDE only)
  monitorcontrol.{h,cpp}  — Moves active window to next monitor via KWin shortcut; available on KWin
  wallpapercontrol.{h,cpp} — Advances the KDE slideshow wallpaper to the next image on a specific monitor (token nextwallpaper)
  powercontrol.{h,cpp}     — Session/power actions via KDE D-Bus (logout/reboot/shutdown/lock/suspend); tokens session + app-menu footer
  relanzadorconfig.{h,cpp}    — Config for one Relanzador (nested mini-dock); see relanzador.md
  relanzadormodel.{h,cpp}     — Model of the apps inside one Relanzador
  relanzadoresmanager.{h,cpp} — Global collection of Relanzadores (token relanzadores)
  scriptrunnerconfig.{h,cpp}  — Config for one Script Runner (id, title, iconName, script)
  scriptrunnersmanager.{h,cpp} — Global collection of Script Runners; run(id) executes the script via `sh -c` (token scriptrunners)
  iconpickerdialog.{h,cpp}    — Self-contained Qt icon picker (lists current theme icons; no KDE deps)
  configarchive.{h,cpp}       — Export/import the whole config to/from a .zip (QtCore private QZip)
  systray.{h,cpp}         — DBus StatusNotifierItem host + watcher (SNI protocol, org.kde.* names)
  systrayimageprovider.{h,cpp} — image://systray/<service> provider for pixmap-only tray icons
  systraymodel.{h,cpp}    — QAbstractListModel filtering visible systray icons
qml/
  SystrayMenu.qml         — A tray item's own menu, built from the DBusMenu tree (recursive, created at runtime)
  Dock.qml                — Main UI: section-based layout (GridLayout driven by config.widgetOrder) rendering the apps block + widgets (volume, brightness, clock, clock2, overview, movetodesktop, movetoscreen, nextwallpaper, autohide, showdesktop, systray, relanzadores, scriptrunners, session, settings) + dynamic separators (springs) + app menu button; per-icon and per-section drag & drop, context menus, autohide
  AppMenuPopup.qml        — XDG application menu popup (Kickoff-like): search field, category sidebar, favorites, power/session footer row
  RelanzadorWidget.qml    — Icon widget for a Relanzador in the main dock
  RelanzadorPopup.qml     — Popup layout for a Relanzador
  ScriptRunnerWidget.qml  — Icon widget for a Script Runner (click → runs its sh script)
  IconMenuItem.qml        — MenuItem rendering its icon via image://icon (works in Popup.Window menus on Wayland)
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
- El estado actual sale de `kdeglobals`: `[Icons] Theme` y `[General] ColorScheme`. **Esta última puede no existir** (escritorios cuyo esquema vino de un look-and-feel): entonces ninguna fila lleva el check, y no es un bug.
- Ambos son **bloques** (`isBlock()`), como los otros widgets con popup: manejan su propio mouse. Si no, el `Binding` de `hovered` de la sección avisa *"Property 'hovered' does not exist"* y el clic se lo lleva el `MouseArea` de la sección.
- Deliberadamente **no tocan el override de iconset propio de kdock** (`Theme::setIconTheme`): si está puesto, el dock conserva sus íconos y solo cambia el resto del escritorio. Dicho en el tooltip del checkbox de Ajustes.
- UI: Settings → Widgets → "Icon theme picker" / "Color scheme picker" (ambos apagados por defecto).

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
- **Widget icon set adapted to the dock color** (per-dock): standard widget icons (volume, network, session, settings…) are monochrome and painted in a color meant for *one* background, so a dark icon set over a light panel is unreadable. `image://icon` therefore accepts an optional third field — `name@rev@themeId` — and `IconProvider` resolves that one icon against `themeId` by swapping `QIcon::setThemeName()` around the lookup (Qt resolves themed icons against a single global name) and restoring it right after; the pixmap **must** be rendered inside the swap because `QIcon` re-checks the current theme lazily. `QIcon::hasThemeIcon()` guards the swap: names the override lacks (custom icons, uninstalled theme) fall through to the normal path instead of becoming missing-icon placeholders. `Dock.qml` picks the id in `widgetIconTheme` from `config.widgetIconThemeMode` (`0` follow icon theme / `1` match dock color / `2` always dark icons / `3` always light icons; default `1`) plus `config.widgetIconThemeLightBg` / `widgetIconThemeDarkBg` (defaults `breeze` / `breeze-dark`), reusing the same `dockBaseIsLight` luminance test as the clock text color, and appends it through `root.widgetIconSuffix` (the URL must change or QML serves the cached pixmap). Scope: the ~15 widget sections only — launchers, relanzadores, script runners, systray and popup/menu icons keep the global icon theme (popups paint on `theme.background`, not the panel color). Configured in Settings → General → "Widget icons" + the two icon-set combos, populated from `Theme::availableIconThemes()`.
- **Per-panel color override**: `config.panelColor` (QColor, **per-dock**) overrides the inherited theme background. Stored as `#RRGGBB` (empty = inherit); `panelColorSet` (bool) reports validity. `Dock.qml`'s `background.baseColor` uses `config.panelColorSet ? config.panelColor : theme.background`, still modulated by `config.opacity`. Chosen via a `QColorDialog` swatch button in Settings → **General → "Panel color"** (plus a "Reset to theme" button); no RGB text entry.
- **Quick color presets**: `config.panelPresetColors` (QStringList, **exactly 4** hex strings — `load()` pads/truncates so the UI can index them safely) + `resetPanelColor()`. Editable from Settings → General → "Quick colors" (four swatch buttons) and applied from `qml/BackgroundColorMenu.qml`, a reusable submenu (4 presets with a checkmark on the active one + "Predeterminado (tema)") that is instantiated **twice** in `Dock.qml`: in the app-icon menu and in the widget-section menu. Same dual-use pattern as `IconLabelMenu.qml`.
- **Text/icon color derived from the dock background**: `Dock.qml`'s root exposes `dockBaseColor` (= `panelColorSet ? panelColor : theme.background`), `dockBaseIsLight` (perceptual luminance 0.299/0.587/0.114 > 0.5) and `clockTextColor` (`#141414` / `#F2F2F2`). Both clock widgets use it, because they would otherwise inherit the KDE theme's gray foreground and wash out over a custom panel color. **Opacity deliberately plays no part** — a translucent dark panel still reads as dark (the four dark presets at the default 85% opacity would otherwise get black text). `dockBaseIsLight` is the single "is the dock light or dark?" test, reused by the widget icon set below.
- **Per-panel background image (tiled)**: `config.panelImage` (absolute path, **per-dock**, empty = none) + read-only `panelImageUrl` (`QUrl::fromLocalFile`). `Dock.qml` draws an `Image` (`id: bgImage`) inside the `background` Rectangle, **over** the base color, `fillMode` = `TileHorizontally` (horizontal docks) / `TileVertically` (vertical docks) — scaled to the dock thickness, repeated along its length. Honors `config.opacity`. Rounded corners in non-panel mode via `layer.effect: OpacityMask` (needs `import Qt5Compat.GraphicalEffects` — **the only QML effects module used**; runtime dep `qml6-module-qt5compat-graphicaleffects`). Chosen via a `QFileDialog` button in Settings → **General → "Panel image"** (+ "Clear"). **Note**: the config stores only the *path*; the Backup `.zip` therefore does **not** bundle the image file.

### Volume Control (`src/volumecontrol.cpp`)
- Requires `wpctl` (WirePlumber) at runtime. If missing, the widget is hidden.
- Uses `pactl subscribe` for live change notifications; falls back to 3-second polling if `pactl` is absent.
- No linkage to `libpulse` or `libpipewire` — purely `QProcess`.
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
- **El ancho importa**: las diez solapas ya piden ~940 px y el diálogo mide 1000. `tabSizeHint()` agrega apenas +4 px; con +12 la barra pasa a modo flechas de scroll (verificado con un arnés `xvfb` que instancia solo el `ColoredTabWidget`).

### App Menu (`src/appmenu.cpp` + `qml/AppMenuPopup.qml`)
- Kickoff-like application menu, exposed to QML as `appMenu` context property.
- Groups installed `.desktop` apps by freedesktop main categories (Development, Games, Graphics, Internet, Multimedia, Office, Settings, System, Utilities, Other).
- Sidebar categories: **Favorites**, **All Applications**, then present categories.
- Search filters by name, comment, and desktop file id.
- Editable favorites: star toggle per app, drag-to-reorder, persisted in `DockConfig::menuFavorites`.
- The menu **button** in the dock is a regular section (token `"menu"`) gated by `config.showMenuButton` (default off). Like the `apps`/`systray` blocks, it is a **drop-only anchor** (not draggable as a unit).
- **Keyboard interactivity**: When the popup opens, `dockWindow.setKeyboardInteractive(true)` sets `kdock.keyboardInteractivity` to **exclusive** (1) so the search field can receive text input. On close (via both `onAboutToHide` and `onClosed`) it reverts to none (0). `requestActivate()` is also called to ensure the compositor routes focus to the dock.
- `AppMenu` is instantiated in `DockWindow`'s constructor as a child of the `DockWindow`; `main.cpp` does not need to include it.

### Multi-instance / `DockManager` (`src/dockmanager.cpp`)
- `main.cpp` no longer builds a single dock. It creates the **system-wide singletons** (`Theme`, `DesktopEntryIndex`, `WindowMonitor`, `VolumeControl`, `BrightnessControl`, `OverviewControl`, `DesktopControl`, `MonitorControl`, `SystrayHost`, `RelanzadoresManager`, `ScriptRunnersManager`, `ClipboardHistory`), packs them into a `DockManager::Shared` struct, and constructs a `DockManager`.
- **Opt-in per (monitor, slot)**: the user enables a dock (Settings → Monitor + Dock selectors → "Show dock here"). The enabled set is persisted as `enabledScreens` (StringList of **dockIds**; key name kept for backward compat) in the shared `kdock.conf` via `DockConfig::enabledDocks()` / `DockConfig::setDockEnabled()`. `DockManager::setDockEnabled` also **seeds a new non-slot-0 dock with a free screen edge** (one not used by the monitor's other docks) so they don't overlap.
- **Per-dock config**: `DockConfig(const QString &dockId)` opens the dock's file. The constructor body is a shared `DockConfig::load()` used by both constructors. The dockId is retained (`DockConfig::dockId()`); `screenName` (the bound output, `screenOfDockId(dockId)`, shared by all slots on a monitor) is written into the file so `DockWindow::applyScreen()` binds the surface to the right `wl_output`.
- **First-run migration** (`DockManager::migrateFirstRun`): when `enabledScreens` is empty, copies the old shared `kdock.conf` into `kdock-<primary>.conf` (slot 0) and enables the primary monitor's slot 0 — the existing single-dock setup carries over unchanged.
- **Hotplug**: `sync()` runs on `screenAdded`/`screenRemoved`/`primaryScreenChanged` and on enable/disable. It creates a dock for every enabled dockId whose screen is connected and destroys the rest. A change of **primary** recreates the affected instances because only the primary dock hosts systray/relanzadores.
- **Shared vs per-instance**: volume/brightness/overview/desktop/monitor/wallpaper/power/window-monitor/systray-host/relanzadores/scriptRunners/clipboardHistory are shared across all docks. Per-instance objects (created in `createInstance`): `DockConfig`, `DockModel`, `SystrayModel` (primary only), and **both clocks** — `ClockWidget`/`ClockWidget2` are per-dock because their 12/24h/date/seconds format is a per-dock config setting. Configs are cached in `m_configs` (keyed by dockId, kept alive even when the dock is hidden) so `SettingsDialog` can edit any dock; `configFor(dockId)` returns the same object the live dock uses (edits apply live).
- **Systray only on the primary dock**: `primaryDockId()` = the lowest-slot enabled dock on the primary monitor. Non-primary docks receive `nullptr` for `systrayModel`/`systrayHost`. `Dock.qml` guards `systray && …` so that section stays hidden regardless of the per-dock config.
- **Relanzadores are per-dock (opt-in)**: the shared `RelanzadoresManager` is passed to **every** dock; `createInstance` also calls `window->setPrimary(primary)` which exposes the `dockIsPrimary` context property. Visibility is filtered per dock via two `DockConfig` lists: the **primary** dock uses `relanzadoresHidden` (default empty ⇒ all shown, incl. newly created ones), every **other** dock uses `relanzadoresShown` (default empty ⇒ none). `RelanzadoresManager::visibleIds(primary, hidden, shown)` centralizes the rule; `Dock.qml`'s `root.visibleRelanzadorIds` drives both the section visibility and the `relanzadoresComp` Repeater (delegate uses `relanzadores.get(modelData)`). In Settings → **Relanzadores**, each row is a **checkbox** whose state reflects the currently-edited dock (primary → hidden semantics, else → shown); the relanzador **list/apps are still global** (one config), only visibility is per-dock. This preserves the old behaviour (relanzadores only on the primary dock) with no migration.
- `SettingsDialog` takes a `DockManager*`: a monitor selector + a **Dock (1..3)** slot selector + "Show dock here" checkbox at the top; changing either combo recomputes the dockId (`selectFromCombos` → `selectDock`) and calls `buildTabs()` to rebind every tab to that dock's config. The legacy per-dock "Screen (Automatic)" combo is hidden when a manager is present. It also takes a `Theme*` (for the Icon-theme combo). **Each tab is wrapped in a `QScrollArea`** (in `buildTabs()`) so the tall General/Widgets tabs scroll internally, and the dialog's initial height is clamped to the screen (`resize(500, min(560, avail-80))`) so the bottom Close/Quit buttons are always visible on small screens.

### KWin-shortcut widgets (overview / move-to-desktop / move-to-monitor)
- Three KDE-only widgets are thin `QObject`s that fire a KWin **global shortcut** over D-Bus (`org.kde.kglobalaccel /component/kwin invokeShortcut <name>`), rather than reimplementing compositor protocols:
  - `OverviewControl::toggle()` → `"Overview"`. **Note**: `org.kde.kwin.Effects.toggleEffect "overview"` does *not* work in Plasma 6 (it loads/unloads the plugin, not the screen); the shortcut is the correct path. `active` is tracked optimistically per click.
  - `DesktopControl::moveToNextDesktop()` → `"Window to Next Desktop"` (token `movetodesktop`, icon `go-next`).
  - `MonitorControl::moveToNextScreen()` → `"Window to Next Screen"` (token `movetoscreen`, icon `video-display`). Its `available` is just the KDE-session check (like overview/move-to-desktop), so the button shows whenever the user enables it. (It previously also required `QGuiApplication::screens().size() > 1`, which hid the button on single-monitor states even when enabled — removed.)
- Availability of overview/move-to-desktop is a static KDE-session check (`XDG_CURRENT_DESKTOP`/`XDG_SESSION_DESKTOP`). Each has a matching `show*` flag in `DockConfig` and a checkbox in Settings → Widgets.

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

### Clock2 (`src/clockwidget2.cpp` + `clock2Comp` in `Dock.qml`)
- A second clock widget (token `clock2`, flag `config.showClock2`) that shows the time like `clock` but with a **larger custom tooltip**: gray `#404040` rounded background, time in yellow `#FFD700` 16px bold, date below in white. Its on-dock font sizes are ~30% larger than `clock` (`iconSize*0.455` / `*0.26`). Bound to the same per-monitor clock format settings as `clock`.
- **Font size (both clocks)**: `config.clockFontSize` (px, `0` = automatic → the historic factors `iconSize*0.35` for `clock` and `*0.455` for `clock2`; the date line derives from it with ratio `0.57`). Set in Settings → Widgets → "Clock font size" (`setSpecialValueText("Automatic")`). The widget's `Item` grows with the font via `Math.max(widgetIconSize, column.implicitWidth)`, so a large font does not get clipped.
- **Text color**: `root.clockTextColor`, derived from the dock background's luminance — see the `dockBaseIsLight` bullet in the **Theme** section.

### Clipboard history (`src/clipboardhistory.cpp` + `qml/ClipboardPopup.qml`)
- Text-only clipboard manager, exposed to QML as `clipboardHistory` context property. Token `"clipboard"`, gated by `config.showClipboard` (default **off**, opt-in). A **drop-only anchor** block (owns its own mouse handling, popup and context menu), like `menu`/`apps`. Icon `edit-paste`.
- **Shared singleton**: `ClipboardHistory` is a system-wide singleton created in `main.cpp`, packed into `DockManager::Shared`, and passed to every `DockWindow` → one global history shared by all docks/monitors.
- **Capture**: connects `QGuiApplication::clipboard()`'s `QClipboard::dataChanged` → `captureClipboard()`; keeps text only, deduplicates (an existing copy moves to the top), caps at `kMaxEntries` (50). Also seeds from the current clipboard at construction. `setClipboard(text)` (used when the user clicks a row) writes the entry back to the system clipboard; `m_ownText` skips re-capturing that echo.
- **Persistence** (survives full shutdowns): plain-text file at `~/.local/share/kdock/clipboard-history.txt` (`QStandardPaths::GenericDataLocation` + `/kdock`). Entries are separated by a human-readable delimiter line `=== kdock clipboard entry ===`, so the file stays legible/lightly-editable. `load()` in the ctor, `save()` on every change.
- **Left click** = `ClipboardPopup` (modeled on `AppMenuPopup`, same modal `Popup.Window` + `dockWindow.setKeyboardInteractive` + `focusRetry` keyboard dance): a search field on top and a scrollable list (~10 rows visible). Search filters case-insensitively (`entries(query)`). Clicking a row copies it to the active clipboard and closes. Size is `config.clipboardPopupWidth`/`clipboardPopupHeight` (Settings → Widgets).
- **Right click** = `Menu` with three `IconMenuItem`s: **Borrar Historial** (`clearHistory()`), **Ver historial actual** (`openInEditor()` → `QDesktopServices::openUrl` the plain-text file in the default editor), **Guardar historial** (`saveHistoryDialog()` → `QFileDialog::getSaveFileName` then export to a plain-text file).
- **⚠️ Wayland caveat (unresolved)**: on KWin/wlroots an app can only read the clipboard while it holds keyboard focus. The dock's layer surface is keyboard-inert, so passive background `dataChanged` capture may be empty until the popup grabs focus. The proper fix is the `wlr-data-control-unstable-v1` protocol (Klipper/cliphist/wl-clipboard). **Not implemented** — revisit if background capture proves unreliable.

### Dock Preview — binario accesorio `kdock-previews` (`previews/`)

Tira de **vistas previas de ventanas** en un borde de pantalla (referencia: el Stage
Manager de macOS): una tarjeta por ventana con una captura real de su
contenido, clic para activarla. **No es parte de kdock**: es un segundo binario, con su
propio árbol (`previews/`), su propia config y su propio multimonitor, que corre *junto* al
dock. kdock solo lo prende, lo apaga y le abre su panel.

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
  pantalla) — la lee el QML *y* la zona exclusiva. `reserveSpace` y `autohide` son
  independientes, pero una tira oculta nunca reserva. Config en
  `~/.local/share/kdock/previews.conf` (compartida: `enabled`, `enabledScreens`,
  `knownScreens`) + `previews-<screen>.conf` por tira; `PreviewManager` replica el
  `sync()` + `migrateFirstRun()` de `DockManager` (adopta el monitor primario en el primer
  arranque). Con el switch maestro `enabled=false` no se muestra ninguna tira, así
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

Dependencies: `qt6-base-dev`, `qt6-base-private-dev` (for the private `QZip*` headers used by `configarchive.cpp`), `qt6-declarative-dev`, `qt6-wayland-dev`, `qt6-wayland-private-dev`, `libqt6dbus6-dev` (or `qt6-tools-dev` depending on distro), `cmake`, `ninja-build`. CMake links `Qt6::CorePrivate` + `Qt6::WaylandClientPrivate` (private Qt modules).

## Dormant / UI-unreachable code

Some widgets had their **Settings checkboxes removed to save space** but the rest
of their code is left **intact but dormant** (existing configs that enabled them
still render and work; they just can't be toggled from the UI anymore).

- **Next-wallpaper widget (`nextwallpaper` token)** — checkbox removed 2026-07-15.
  - Intact: `src/wallpapercontrol.{h,cpp}` (`WallpaperControl`, ctx prop `wallpaperControl`, wired through `main.cpp`/`DockManager`/`DockWindow`); `config.showNextWallpaper`; the `nextwallpaper` token in `knownWidgetTokens()`; `nextWallpaperComp` + its cases in `Dock.qml`; `sectionLabel("nextwallpaper")`.
- **Clock widget (`clock` token)** — "Show clock" checkbox removed 2026-07-16.
  - Intact: `ClockWidget` (`src/clockwidget.{h,cpp}`, ctx prop `clock`); `config.showClock`; the `clock` token + `clockComp` in `Dock.qml`. **Note**: the clock **format** checkboxes (24h / date / seconds) stay in Settings and still apply to **both** clocks; `clock2` (enhanced-tooltip clock) keeps its own "Show clock (enhanced tooltip)" checkbox and is fully usable.
- **Layout tab hides `clock` and `nextwallpaper`** — `SettingsDialog::reloadLayoutList()` filters them out (`kHiddenFromLayout`) so they can't be reordered there. Because the list is filtered, the Layout reorder/add/remove handlers map a list row to its `widgetOrder` index through `orderIndexOfRow()`, which reads the index **stored on each item** (`Qt::UserRole + 1`). It used to resolve the row by its *token*, which silently broke with **two or more springs**: `widgetOrder.indexOf("spring")` always returns the first one, so Up/Down/Remove acted on the wrong separator and dragged a widget along with it (bug 2026-07-29). The list also **numbers the separators** ("Dynamic separator 1/2") — identical rows make that class of bug invisible.
- **To re-enable** any of the above: re-add the checkbox in `SettingsDialog::createWidgetsTab()` and (for clock/nextwallpaper) remove the token from `kHiddenFromLayout`. **To fully remove**: delete the backend + token + `*Comp` + config flag.

## Important Considerations for Modifications

1. **Layer-shell private API** — When modifying `layershell.cpp`, be aware that it uses QtWaylandClient private headers (`QWaylandShellIntegrationTemplate`, `QWaylandShellSurface`, etc.). These are not ABI-stable across Qt major versions.
2. **Protocol objects** — The generated Wayland protocol classes (`QtWayland::zwlr_*`, `QtWayland::org_kde_*`) are created by `qt_generate_wayland_protocol_client_sources`. Do not hand-edit the generated files in `build/`; they are regenerated from `protocols/*.xml`.
3. **Autohide + input mask** — Dock thickness now has a **single source of truth in C++**: `DockConfig::dockThickness()` (= `qMax(iconSize, appCellThickness()) + padding`). `DockWindow::thickness()` returns it (layer-shell exclusive zone) and `Dock.qml`’s `thickness` binds to `config.dockThickness`, so the two can no longer drift. Any new setting that changes the dock’s cross-axis size must be folded into `appCellThickness()` and must emit `dockThicknessChanged()` (`DockWindow` re-applies its layer properties on that signal). One input of that formula is *measured in QML*, not configured — `config.effectiveLabelWidth`, fed by `setMeasuredLabelWidth()` (see the widest-name bullet under **App-icon labels**). That is the only QML→C++ feedback into the thickness, and it is safe because a label's natural width does not depend on the thickness; do not add a remeasure hook on `dockThicknessChanged`.
4. **Window geometry in panel mode** — When `panelMode` is true and `dockLength` is 0, the compositor dictates the stretched dimension (layer-shell size = 0 in that dimension). `Dock.qml` uses `config.panelMode` to decide whether to follow `Window.width/height` or content size. When `dockLength` > 0, the dock has a fixed length (percentage of the screen edge) regardless of `panelMode`; the layer-shell anchors only one side corner (not both) so the compositor respects the explicit `set_size`.
5. **KWin privilege** — If the window list stops working on KWin during development, verify the `.desktop` file is installed with the correct `X-KDE-Wayland-Interfaces` and `Exec=` path.
6. **QML context properties** — `DockWindow` (`dockwindow.cpp`) injects `config`, `theme`, `dockModel`, `dockWindow`, `volume`, `clock`, `clock2`, `brightness`, `overview`, `desktopControl`, `monitorControl`, `wallpaperControl`, `power`, `systray`, `relanzadores`, `scriptRunners`, `clipboardHistory`, `battery`, `disks`, `network`, `dockIsPrimary`, `apps`, `showdesktop`, `appMenu`, `iconColors` as context properties. Any new C++ object exposed to QML should be added there. For non-primary docks `systray` is `nullptr` (guard with `systray && …` in QML); `relanzadores` is passed to every dock but filtered per-dock (see the relanzadores note under DockManager) with `dockIsPrimary` selecting the default. Note: inside `RelanzadorWidget`/`RelanzadorPopup`/`ScriptRunnerWidget`/`AppMenuPopup`, alias context properties (`_config`, `_theme`, …) before passing them to same-named component properties to avoid self-shadowing.
7. **Multi-screen** — Changing screens recreates the platform window (`hide(); destroy(); setScreen(); show()`) because `wl_output` is fixed at layer-surface creation. The `wl_output` is resolved from `QWindow::screen()->handle()` (cast to `QWaylandScreen*`) rather than `window->waylandScreen()` because the latter may not have picked up the target screen yet after the destroy/set/show cycle. Falls back to `waylandScreen()` if the cast fails.
8. **Window detection on startup** — At construction time, Wayland protocol events from the compositor (e.g. `window_with_uuid` from KWin) are still queued in the socket buffer. `DockModel::rebuild()` sees an empty `monitor->windows` list. A deferred `syncWindows()` call (200 ms via `QTimer::singleShot` in `DockWindow` constructor) iterates all tracked windows after the event loop starts and places any that were missed.
8. **Drag & drop** — `DockModel::moveItem()` enforces that dragged items cannot cross the pinned/unpinned boundary or move separators.
9. **Static separators** — `DockModel` inserts up to 2 fixed-size separator items **inside the apps block** based on `DockConfig::separator1`/`separator2`. `DockConfig::separatorSize` (default 16 px) controls the lateral space in horizontal mode and vertical space in vertical mode. They render as thin lines in QML and are non-interactive. Distinct from the **dynamic separator** (see #13).
10. **Brightness widget** — Uses `brightnessctl` (via QProcess, no extra library linkage). The widget is hidden if `brightnessctl` is not in `$PATH`.
11. **Autohide toggle** — A dock widget button that toggles `config.autohide`. Uses `window-pin`/`window-unpin` icons.
12. **Systray DBus (SNI)** — `SystrayHost` implements the StatusNotifierItem protocol using the **`org.kde.*`** bus names, which is what the ecosystem actually implements (KDE, libappindicator, Qt/GTK trays); the `org.freedesktop.*` names are only registered as extra aliases when kdock has to *become* the watcher (bare wlroots, no existing watcher). Key points (see `src/systray.cpp`):
    - **Watcher discovery**: prefer an existing `org.kde.StatusNotifierWatcher`, else `org.freedesktop.StatusNotifierWatcher`, else register both ourselves. The chosen name is `m_watcherService` (used for host registration, the item list and the `StatusNotifierItemRegistered/Unregistered` signal subscriptions).
    - **`RegisteredStatusNotifierItems` is a D-Bus *property*** of the real watcher (not a method), read via `org.freedesktop.DBus.Properties.Get`. (Our own scriptable method form only applied when kdock was the watcher.)
    - **Item id parsing** (`splitItemId`): the watcher returns items as `service+path` concatenated (e.g. `:1.67/org/blueman/sni`); split at the first `/` (no `/` ⇒ path `/StatusNotifierItem`).
    - **Item properties/signals** use `org.kde.StatusNotifierItem`. Live updates come from the SNI signals `NewIcon`/`NewOverlayIcon`/`NewAttentionIcon`/`NewToolTip`/`NewTitle`/`NewStatus` (items do **not** emit `PropertiesChanged`); each re-runs `readProperties()`. `readProperties()` is a **private slot** so the string-based `SLOT()` connection resolves.
    - **Pixmap fallback**: items without a themed `IconName` expose only `IconPixmap` (`a(iiay)` = array of `(w, h, ARGB32-big-endian bytes)`). **These wire types MUST be registered with `qDBusRegisterMetaType` (KDbusImageStruct/KDbusImageVector, done in the SystrayHost ctor)** — otherwise `QDBusInterface::property()` returns empty and manual `QDBusArgument` demarshalling desyncs and **libdbus aborts** (`type struct not a basic type`). `SystrayItem::readPixmapProperty()` reads the property into `KDbusImageVector`, picks the largest frame and converts big-endian ARGB32 → native (`qFromBigEndian`). `SystrayImageProvider` (`image://systray/<service>`) serves it; on no icon it returns a transparent 1×1 (renders nothing, no QML warning). `SystrayModel` adds an `iconSerial` role (bumped every `readProperties()`) so the QML `Image` busts its cache on change. `Dock.qml` uses `image://icon/<name>` when `iconName` is set, else `image://systray/<service>@<iconSerial>`. Registered per primary `DockWindow` engine when `systrayHost` is non-null.
    - **Item menus are drawn by the dock, over DBusMenu** (`src/dbusmenu.cpp`, `qml/SystrayMenu.qml`; 2026-07-29). SNI does not carry the menu: the item exposes a `Menu` object path speaking `com.canonical.dbusmenu` and the **host** is expected to fetch the tree and render it. Asking the item to draw its own (`ContextMenu(x,y)`) **cannot work here** — on Wayland an app with no visible window has no surface to parent an `xdg_popup` to, and plenty of items do not even export the method (blueman); it is kept only as the last resort for items with no `Menu` path. Verified on this host: the five live items all expose a real menu (`/MenuBar` for Qt/KDE, `/org/blueman/sni/menu`).
        - **Wire type**: `GetLayout` returns `u, (ia{sv}av)` where the children are an `av` of variants each wrapping the same struct, so `DBusMenuLayoutItem`'s `operator>>` **recurses** through the inner `QDBusArgument`. As with the SNI pixmaps, `qDBusRegisterMetaType` is mandatory before any call or demarshalling desyncs and libdbus aborts.
        - **Everything is async** (`QDBusPendingCallWatcher`, 2 s timeout): a blocking call against a hung item would freeze the dock for the D-Bus default of 25 s. So a click only *asks* (`requestMenu`) and QML opens the menu on `menuReady`. A layout already fetched is served immediately — `LayoutUpdated`/`ItemsPropertiesUpdated` keep it fresh and reach QML as `menuInvalidated`, which refreshes an open menu.
        - **Click routing** (`systrayComp` in `Dock.qml`): left = `Activate`, except that `ItemIsMenu` items get the menu (per spec, their Activate does nothing), and `Activate` is an **async call whose reply is inspected** — an error emits `SystrayItem::activateFailed`, which the model turns into a `requestMenu`, so blueman (no `Activate` at all) opens its menu instead of doing nothing. Right = the menu (`ContextMenu` only without one). Middle = `SecondaryActivate`. No wheel/`Scroll` support (deliberate).
        - **`SystrayMenu.qml` builds itself imperatively**, not with an `Instantiator`: the three kinds of entry are three different types (`MenuSeparator`/`IconMenuItem`/`Menu`) and one delegate can only produce one of them. Two traps: submenus make the component instantiate **itself**, which QML rejects declaratively (*"SystrayMenu is instantiated recursively"* — the check is static), so they go through `Qt.createComponent("SystrayMenu.qml")` at runtime; and items must be created with **`control.contentItem`** as parent, or every entry logs *"Created graphical object was not placed in the graphics scene"* (a `Menu` is a `Popup`, not an `Item`, so it is no visual parent). Teardown tracks what it created and uses `removeMenu` for submenus, `removeItem` for the rest.
        - **Labels and icons**: mnemonic markers (`_`/`&`) are stripped — QtQuick Controls interprets neither, so they would show up literally. Entries carry `icon-name` and/or `icon-data` (a raw PNG blob); the **theme name wins** (follows the icon theme, scales) and the blob is the fallback, served by extending the existing provider with an `image://systray/menu:<service>:<itemId>@<serial>` id. `toggle-type` renders as a check either way: QQC's radio indicator needs an exclusive group, which DBusMenu does not describe.
    - **Settings**: a "System tray" checkbox in Settings → Widgets toggles `config.showSystray` (per-dock; only the primary dock renders the tray, so it is effective there). A "Systray icon scale" spinbox (20–100%) sets `config.systrayIconScale`, an **independent** scale (mirrors `widgetIconScale`): the derived read-only `config.systrayIconSize` = `qMax(16, iconSize * systrayIconScale%)` drives the tray icon cell/pixmap in `Dock.qml`'s `systrayComp` (so tray icons resize separately from other widgets, and `setIconSize` re-emits `systrayIconSizeChanged` for live rescale).
13. **Section layout & dynamic separator (`config.widgetOrder`)** — The dock content is an **ordered list of sections** stored in `DockConfig::widgetOrder` (a persisted `QStringList` of tokens). Known widget tokens: `menu`, `apps`, `clipboard`, `disks`, `network`, `volume`, `brightness`, `battery`, `clock`, `clock2`, `overview`, `movetodesktop`, `movetoscreen`, `nextwallpaper`, `autohide`, `showdesktop`, `systray`, `relanzadores`, `scriptrunners`, `session`, `settings`. Zero or more `spring` tokens are **dynamic separators**. `Dock.qml` renders them with a single-line `GridLayout` (`flow`/`rows`/`columns` switch by orientation) fed by a `Repeater` over `config.widgetOrder`, mapping each token to a `Component` via `componentFor()`.
    - **Reconciliation**: `DockConfig::reconcileWidgetOrder()` (run on load and in `setWidgetOrder`) guarantees `apps` + every known widget token appear exactly once (missing ones appended), keeps `spring` tokens, and drops unknown tokens. This makes upgrades safe (new widgets appear automatically) and prevents a widget from ever disappearing — **per-widget visibility is still governed by the existing `show*` flags**, not by presence in `widgetOrder`.
    - **Dynamic separator (spring)** expands via `Layout.fillWidth`/`Layout.fillHeight` to push sections toward an extreme. It only has room to expand when the dock spans the whole edge (`panelMode` && `dockLength == 0`); otherwise it collapses to size 0 (**no-op**). `root.fillMain = panelMode && hasSpring` gates whether the layout fills the main axis; when **true the `alignment` (start/center/end) is ignored** (the spring dictates positioning); when false the existing `alignment` logic applies (unchanged behavior when no spring exists). Because a no-op alignment combo confuses users, `SettingsDialog` **disables the alignment combo** and shows an explanatory note whenever the dock spans the whole edge AND a spring is present (updated live on `panelModeChanged`/`widgetOrderChanged`/`dockLengthChanged`); removing the spring, disabling panel mode, or setting a fixed `dockLength` re-enables it.
    - **Widget/section drag & drop**: single-icon widgets (`volume`/`brightness`/`clock`/`autohide`) and `spring` are draggable to reorder (`DockConfig::moveSection(from,to)`), mirroring the app-icon `Drag`+`DropArea`+reparent pattern. Their click/wheel actions are dispatched from the section-level `MouseArea` (`root.sectionClick`/`sectionWheel`). **Multi-icon blocks** (`apps`, `systray`, `relanzadores`) are **drop-only anchors** (not draggable as a unit) so their inner per-icon mouse handling stays intact; reposition them by dragging widgets/springs around them.
    - **Add/remove spring**: right-click any widget → "Add dynamic separator" (`DockConfig::insertSpring(at)`); right-click a spring → "Remove dynamic separator" (`DockConfig::removeSectionAt(at)`, which only removes `spring` tokens).
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
16. **Layout settings tab** — `SettingsDialog::createLayoutTab()` (`src/settingsdialog.cpp`) lists `config.widgetOrder` and lets the user reorder sections (up/down → `moveSection`), add a dynamic separator (`insertSpring`), and remove one (`removeSectionAt`, enabled only when a `spring` row is selected). This is the manual alternative to in-dock drag & drop / right-click.
17. **AppMenu (Kickoff-like launcher)** — `src/appmenu.cpp` + `qml/AppMenuPopup.qml`. The menu button (`"menu"` token in `widgetOrder`) is gated by `config.showMenuButton` (Settings → Widgets → "Show application menu button"). The popup opens toward the opposite side of the dock edge. Keyboard interactivity is set to **exclusive** (1) on the dock's layer surface while open so the compositor grants keyboard focus; `requestActivate()` is called alongside, and both `onAboutToHide` + `onClosed` revert to none (0) as a safety net. The popup itself uses `modal: true` + `popupType: Popup.Window` so the compositor grants a **keyboard grab** to the xdg_popup (this is what actually lets the search `TextField` receive key events — the layer-surface interactivity alone is not enough). A 50 ms `Timer` retries `forceActiveFocus()` after the commit settles. Favorites are persisted in `config.menuFavorites`. The menu button block is drop-only (like `apps`/`systray`).
    - **Configurable icon**: the button icon is `config.menuIcon` (per-dock, default `applications-all`), chosen via `IconPickerDialog` in Settings → Widgets ("Menu icon").
    - **Configurable size**: the popup `width`/`height` bind to `config.menuPopupWidth`/`menuPopupHeight` (per-dock, default 540×460), set via two spin boxes in Settings → Widgets ("Menu width"/"Menu height"). (Border drag-resize was attempted but abandoned — an `xdg_popup` on Wayland can't be reliably user-resized by dragging; the config spinboxes are the robust approach.)
    - **Toggle close**: because the popup is `modal` + `Popup.Window`, the press that lands on the icon while it is open *dismisses* the popup before the `MouseArea.onClicked` fires — a naive `visible ? close() : open()` would reopen it. Fix: `AppMenuPopup` records `closedAt = Date.now()` in `onVisibleChanged` when hidden; the icon's `onClicked` suppresses reopening if `Date.now() - closedAt < 300` (so a second click on the icon toggles it closed).

18. **Script Runner widget (`scriptrunners` token)** — A dock button that runs a shell script. Mirrors the relanzadores architecture: `ScriptRunnersManager` (shared singleton, exposed to QML as `scriptRunners`) holds a **global collection** of `ScriptRunnerConfig` (`{id,title,iconName,script}`) persisted under `[scriptrunners]` in the shared `kdock.conf`. `run(id)` executes `QProcess::startDetached("sh", {"-c", script})` (fire-and-forget). **Per-dock visibility** reuses the exact relanzadores scheme: `DockConfig::scriptRunnersHidden` (primary dock, default all shown) / `scriptRunnersShown` (other docks, default none), filtered by `ScriptRunnersManager::visibleIds(primary,hidden,shown)` and `Dock.qml`'s `root.visibleScriptRunnerIds`. `qml/ScriptRunnerWidget.qml` is the on-dock icon (click → `scriptRunners.run(id)`, no popup). Settings → **Script Runner** tab: checkbox per runner (per-dock visibility, same logic as Relanzadores) + editor (title / icon / multiline script). The icon is chosen via `IconPickerDialog` (`src/iconpickerdialog.{h,cpp}`) — a **self-contained Qt Widgets** picker (no KDE Frameworks dep) that lists the current icon theme's icons (scans `themeSearchPaths()` for the active theme + `Inherits=` + `hicolor`, cached; searchable IconMode grid, results capped for responsiveness). Reusable for other icon fields. The `scriptrunners` block is a drop-only anchor like `relanzadores`.

19. **Config Import/Export (Settings → Backup tab)** — `ConfigArchive` (`src/configarchive.{h,cpp}`) exports/imports the **entire** configuration as a `.zip`: every `kdock*.conf` in the config dir (`ConfigArchive::configDir()` = dir of `DockConfig::settingsFilePath()`), i.e. the shared `kdock.conf` (enabledScreens, relanzadores, **script runners incl. their scripts**, favorites) + each per-dock `kdock-<dock>.conf`. Uses **QtCore's private `QZipWriter`/`QZipReader`** (`#include <private/qzipwriter_p.h>`; links `Qt6::CorePrivate`) — no external `zip`/`unzip` runtime dependency. Import: validates the archive contains `kdock.conf`, only accepts entries matching `^kdock[\w.#-]*\.conf$` (**anti zip-slip**, no path separators), **backs up** the current config to `backup-<timestamp>/`, does a **clean replace** (removes current `kdock*.conf` then writes the archive's), and the Settings dialog **relaunches kdock** (`QProcess::startDetached(applicationFilePath())` + `quit()`) so all docks/managers reload. The `Backup` tab is always present (config is global, not per-dock).

19a. **Favorites export/import (Settings → Application menu)** — Separate from the full `.zip` backup: `ConfigArchive::exportFavorites()` / `importFavorites()` write and read a small JSON file `{ "app": "kdock-favorites", "version": 1, "favorites": [<.desktop ids>] }`. The import validates the `app` marker and that `favorites` is an array of strings before touching the config.

19b. **Removable disks widget (`disks` token)** — Native reimplementation of Plasma's device-notifier tray applet (the chosen "Ruta C" over embedding plasmoids). `DisksControl` (`src/diskscontrol.{h,cpp}`, shared singleton, exposed to QML as `disks`) speaks raw **UDisks2** over the **system** D-Bus (`org.freedesktop.UDisks2`, no extra library): `GetManagedObjects` (demarshalled into `QMap<QDBusObjectPath, QMap<QString, QVariantMap>>`), keeping Block objects that have a `Filesystem` interface, `IdUsage=="filesystem"`, not `HintIgnore`, and whose backing `Drive` is **removable/usb** (this drops `loop*` snap mounts, whose Drive is `/`). `volumes()` returns maps `{path,drive,label,device,mountPoint,mounted,ejectable,size}`; `mount`/`unmount` call `Filesystem.Mount`/`Unmount`, `eject` calls `Drive.Eject` (empty `a{sv}` options), `openMount` runs `xdg-open`. Live updates: connects the ObjectManager `InterfacesAdded`/`InterfacesRemoved` **and** `PropertiesChanged` (empty path = any object, catches mount/unmount) to a 300 ms-debounced `rescan()`. UI: `disksComp` block in `Dock.qml` (icon `drive-removable-media-usb`, left-click opens `qml/DisksPopup.qml` — a **non-modal** popup, no keyboard grab needed, listing volumes with mount/unmount + eject buttons; click a mounted row → open in file manager). Opt-in via `config.showDisks` (Settings → Widgets → "Disks"); drop-only block like `clipboard`. Threaded through `main.cpp` (singleton) → `DockManager::Shared::disks` → `DockWindow` ctor → context property.

19c. **Network widget (`network` token)** — The other native Ruta C widget. `NetworkControl` (`src/networkcontrol.{h,cpp}`, shared singleton, exposed to QML as `network`) speaks raw **NetworkManager** over the **system** D-Bus (`org.freedesktop.NetworkManager`, no extra library). `rescan()` (debounced 500 ms, driven by any `PropertiesChanged` under the NM service, empty path) reads NM's `GetAll`: `State`, `WirelessEnabled`, `NetworkingEnabled`, `PrimaryConnectionType`, `PrimaryConnection` (→ active `Id`), and for wireless links the active AP `Strength` (iterates `Devices`, `Device.Wireless.ActiveAccessPoint` → `AccessPoint.Strength`); derives a themed `iconName` (`network-wireless-signal-{none..excellent}` / `network-wired-activated` / `network-offline`). `connections()` (called when the popup opens) does `Settings.ListConnections` + per-connection `GetSettings` (demarshalled into `QMap<QString,QVariantMap>`, reads `connection.id`/`type`) and flags each active by matching against NM `ActiveConnections`' `Connection` path; sorted active-first. `activate` calls `ActivateConnection(conn,"/","/")`, `deactivate` calls `DeactivateConnection(activePath)`, `setWifiEnabled` sets the `WirelessEnabled` property. **Out of scope (needs a NM secret agent): joining a brand-new Wi-Fi network by password — only already-saved connections can be activated.** UI: `networkComp` block in `Dock.qml` (icon bound to `network.iconName`, left-click opens `qml/NetworkPopup.qml` — non-modal; header shows primary name + a Wi-Fi `Switch`, list shows saved connections with active-first, click to activate/disconnect). Opt-in via `config.showNetwork` (Settings → Widgets → "Network"); drop-only block. Threaded like `disks` through `main.cpp` → `DockManager::Shared::network` → `DockWindow` ctor → context property. **Ruta C complete (disks + network).**

20. **Session / power + settings widgets** — `PowerControl` (`src/powercontrol.{h,cpp}`, exposed to QML as `power`, KDE-only via the session check) fires KDE session actions over D-Bus: **logout/reboot/shutdown** via `org.kde.LogoutPrompt` (`promptLogout`/`promptReboot`/`promptShutDown` — shows KDE's **confirmation** screen), **lock** via `org.freedesktop.ScreenSaver.Lock`, **suspend** via `org.freedesktop.PowerManagement.Suspend`. Used in three places (all gated by `power.available`):
    - **`session` token** (`sessionComp` in `Dock.qml`) — a **block** button (icon `system-shutdown`) whose `MouseArea` opens a `Menu` (`Popup.Window`, `IconMenuItem`) with the five actions. Gated by `config.showSessionButton`.
    - **App-menu footer** (`AppMenuPopup.qml`) — a right-aligned row of five icon buttons, gated by `config.showMenuPower` (default **on**).
    - **`settings` token** (`settingsComp`) — a **single-icon** draggable widget (icon `configure`); click → `dockWindow.openSettings()` via `sectionClick("settings")`. Gated by `config.showSettingsButton`. No backend (reuses `DockWindow::openSettings`).
    Checkboxes for all three live in Settings → Widgets.

## QML ↔ C++ Interface Summary

| C++ Class | QML Property / Method | Notes |
|-----------|----------------------|-------|
| `AppearanceControl` | `appearance.*` | `iconThemes()`, `colorSchemes()` (listas de mapas con id/name/current, + bg/fg/sel en los esquemas), `refreshIfStale()`, `applyIconTheme(id)`, `applyColorScheme(id)` + `currentIconTheme`/`currentColorScheme` |
| `DockConfig` | `config.*` | Exposed as context property; many `Q_PROPERTY` values. Section layout: `widgetOrder` (QStringList), `moveSection(from,to)`, `insertSpring(at)`, `removeSectionAt(at)`. Dock length: `dockLength` (int 0-100, 0=auto, >0=% of screen edge). App-icon labels: `iconLabelMode` (0=icon only/default, 1=name below, 2=name at right, 3=name only, 4=name above, 5=name at left), `iconLabelWidth` (a **cap**), `iconLabelFontSize` (0=auto) + read-only `iconLabelFontPx`/`iconLabelLineHeight`/`iconLabelGap`/`dockThickness`/`effectiveLabelWidth`, and `setMeasuredLabelWidth(px)` (QML reports the widest name it draws; see App-icon labels). Section labels: `widgetLabelMode` (same values, no 3), `widgetName(token)`, `setWidgetName(token,name)` + read-only `widgetNamesRevision`. Auto-shrink: `autoShrinkIcons` (bool), `autoShrinkMinIconSize` (px). Pickers de apariencia: `showIconThemes`, `showColorSchemes` |
| `Theme` | `theme.background`, `theme.foreground`, `theme.highlight`, `theme.revision` | Live-reloading color scheme |
| `DockModel` | `dockModel` (model) | `activate(row)`, `launch(row)`, `togglePinned(row)`, `closeAll(row)`, `windowList(row)`, `moveItem(from, to)`, `activateWindow(row, idx)`, `syncWindows()` |
| `DockWindow` | `dockWindow` | `setHidden(bool)`, `openSettings()`, `quit()` |
| `VolumeControl` | `volume` | `available`, `volume`, `muted`, `iconName`, `setVolume(v)`, `toggleMute()`, `refresh()` (called on hover to avoid acting on a stale cache) |
| `AudioControl` | (no context property) | Mixer backend for Settings → Audio only: `available`, `maxVolume`; devices/streams via C++ API |
| `IconProvider` | `image://icon/name@rev[@themeId]` | `theme.revision` appended to bust cache on theme change; optional `themeId` resolves that icon against another icon set (widget icons adapted to the dock color) |
| `PreviewConfig` | `config.*` (binario `kdock-previews`) | Contexto de `PreviewStrip.qml`: `edge`, `alignment`, `opacity`, `panelColor`/`panelColorSet`/`panelPresetColors`/`resetPanelColor()`, `stripThickness` + read-only `stripThicknessPx`/`cardWidthPx`/`pad`, `stripLength` (0=todo el borde), `screenMargin`, `reserveSpace`, `autohide`, `showTitles`, `cardSpacing`, `captureMode` (0=una captura al primer foco/default, 1=periódico), `refreshInterval`, `activeRefreshInterval` (solo en modo 1), `includeMinimized`, `currentDesktopOnly`, `thisMonitorOnly`, `screenName` |
| `PreviewModel` | `previews` (model) | Roles `uuid`, **`thumbId`** (uuid sin llaves: el único que va en una URL), `title`, `appName`, `iconName`, `thumbRevision`, `aspect`, `active`, `minimized`; `activate(row)`, `closeWindow(row)`, `toggleMinimize(row)`, `refreshNow(row)` (no-op salvo en modo periódico), `setVisibleRange(first,last)` |
| `PreviewWindow` | `previewWindow` | `setHidden(bool)`, `openSettings()`, `restart()`, `quit()` |
| `ThumbnailImageProvider` | `image://thumb/<thumbId>@<rev>` | Captura escalada de una ventana; `rev` (de `thumbRevision`) invalida la caché de QtQuick. 1×1 transparente cuando todavía no hay captura, y un aviso por stderr si la clave no resuelve. **`thumbId`, no `uuid`** (QUrl percent-codifica las llaves) |
| `IconColorProvider` | `iconColors.dominant(iconName, revision)`, `iconColors.contrasting(...)` | Dominant icon color (QColor) for the running-app background; `contrasting()` is the color for the dots/edge line drawn *over* that background. Cached, revision-invalidated |
| `WindowMonitor` | `showdesktop` | `showDesktopSupported`, `showDesktopActive`, `toggleShowDesktop()`; null on X11 |
| `BrightnessControl` | `brightness` | `available`, `brightness`, `setBrightness(v)`, `increase()`, `decrease()` |
| `ClockWidget` | `clock` | `timeString`, `dateString`, `format24h`, `showDate`, `showSeconds` |
| `ClockWidget2` | `clock2` | Same API as `ClockWidget`; rendered with a larger styled tooltip |
| `OverviewControl` | `overview` | `available`, `active`, `toggle()` (KWin Overview via kglobalaccel) |
| `DesktopControl` | `desktopControl` | `available`, `moveToNextDesktop()` (KWin "Window to Next Desktop") |
| `MonitorControl` | `monitorControl` | `available` (KDE session check), `moveToNextScreen()` (KWin "Window to Next Screen") |
| `PowerControl` | `power` | `available` (KDE), `logout()`, `reboot()`, `shutdown()` (LogoutPrompt), `lock()`, `suspend()` |
| `SystrayModel` | `systray` | `count`, `activate(row, x, y)`, `contextMenu(row, x, y)`, `secondaryActivate(row, x, y)`. Item menu (DBusMenu): `requestMenu(row)`, `menuTree(row)`, `triggerMenuItem(row, id)`, `menuAboutToShow(row, id)`, `setMenuOpen(row, open)` + signals `menuReady(row)` / `menuFailed(row)` / `menuInvalidated(row)`; roles include `hasMenu` and `itemIsMenu`. `nullptr` on non-primary docks |
| `RelanzadoresManager` | `relanzadores` | `count`, `get(id)`, `createRelanzador(title)`, `removeRelanzador(id)` |
| `RelanzadorConfig` | (via relanzadores.get()) | `id`, `title`, `iconName`, `pinned`, `showVolume`, `showBrightness`, `showClock`, `showAutohide`, `showSystray`, `hasSubRelanzador`, `subRelanzadorId`, `nestingLevel` |
| `DesktopEntryIndex` | `apps` | (passed to QML for icon resolution) |
| `AppMenu` | `appMenu` | `categories()`, `appsInCategory(cat)`, `search(query)`, `favorites()`, `launch(id)`, `isFavorite(id)`, `addFavorite(id)`, `removeFavorite(id)`, `toggleFavorite(id)`, `moveFavorite(from,to)` |
| `BatteryControl` | `battery` | `available`, `profilesAvailable`, `percent`, `charging`, `iconName`, `profile`, `setProfile(id)` (UPower + power-profiles-daemon, system bus) |
| `DisksControl` | `disks` | `available`, `volumes()` (→ removable drives), `mount(path)`, `unmount(path)`, `eject(path)`, `openMount(path)` (UDisks2) |
| `NetworkControl` | `network` | `available`, `iconName`, `connections()`, `activate(path)`, `deactivate(path)`, `setWifiEnabled(on)` (NetworkManager). Joining a *new* Wi-Fi by password needs an NM secret agent — not implemented |
| `ScriptRunnersManager` | `scriptRunners` | `count`, `get(id)`, `removeScriptRunner(id)`, `visibleIds(isPrimary, hidden, shown)`, `run(id)` |
| `ClipboardHistory` | `clipboardHistory` | `count`, `entries(query)` (→ `{text, preview}`), `setClipboard(text)`, `clearHistory()`, `openInEditor()`, `saveHistoryDialog()`; shared singleton, text-only, 50-entry cap, plain-text persistence |

## Potential Improvement Areas

- Animation smoothness (custom vs. QML `Behavior`).
- Additional widgets (battery, etc.). ✅ Done: overview, move-to-next-desktop, move-to-next-monitor, second clock (styled tooltip), next-wallpaper (per-monitor), script runner (`sh -c`), session/power menu, kdock-settings button, clipboard history (text-only, plain-text persistence — Wayland background-capture caveat, see **Clipboard history**).
- **Native Plasma-tray replacements ("Ruta C")** — ✅ **DONE** (2026-07-21): rather than embedding Plasma plasmoids (they are QML KPackages, not standalone binaries, loadable only by the Plasma runtime), kdock reimplements the tray applets natively over the same backends. Added **Disks** (`DisksControl`, UDisks2 — see point 19b) and **Network** (`NetworkControl`, NetworkManager — see point 19c), joining the existing volume/brightness/battery/clipboard controls. Also added an **independent systray icon scale** (`config.systrayIconScale`, see point 12). Network caveat: joining a new Wi-Fi by password needs a NM secret agent (not implemented); only saved connections activate.
- **Multi-instance (up to 3 docks per monitor)** — ✅ **DONE** (2026-07-15): opt-in per (monitor, slot) via dockId, per-dock config files, hotplug, systray on the primary dock only, relanzadores/scriptrunners per-dock (opt-in checkbox), auto free-edge for new docks. See **DockManager**. Known rough edge: changing the *primary* monitor at runtime recreates the affected docks (brief flicker).
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

