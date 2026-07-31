# Plan — widgets nuevos: selector de iconset y de esquema de color de KDE

Estado: **IMPLEMENTADO** el 2026-07-29 (compilado y verificado; falta instalar en
`/usr/local`, que lo decide el usuario). La documentación viva está en `AGENTS.md`
→ "Selectores de apariencia de KDE"; esto queda como registro del plan y de lo que
cambió respecto de él.

## Contexto

El dock ya cambia cosas del escritorio (fondo de pantalla, brillo, audio, red), pero para
cambiar el **iconset** o el **esquema de color** de KDE hay que abrir Preferencias del
Sistema. Se pidieron dos widgets nuevos, cada uno con un popup desplegable con scroll, que
listen lo instalado y lo apliquen al sistema con un clic.

Relevamiento previo (se confirmó todo en la implementación):

- Instalados: 183 directorios de iconsets (**122** son iconsets de verdad; el resto son
  temas de cursor u ocultos) y 456 archivos `.colors` (**445** tras deduplicar por id) →
  sin buscador la lista es inusable.
- `plasma-apply-colorscheme` está en el PATH (`/opt/kde/bin`); **`plasma-changeicons` no**:
  vive en `…/libexec` y hay que buscarlo por ruta.
- Listar por CLI no sirve: `--list-schemes` imprime texto **traducido**.
- `Theme::availableIconThemes()` (`src/theme.cpp:129`) ya hace el listado de iconsets → se
  reutilizó tal cual. `IconProvider` ya resuelve `image://icon/<name>@<rev>@<theme>`, así que
  las miniaturas por fila salieron gratis.
- Estado actual desde `~/.config/kdeglobals`: `[Icons] Theme` y `[General] ColorScheme`
  (esta última **no existe** en esta máquina → ninguna fila de esquemas lleva check).

Decisiones tomadas con el usuario: buscador + scroll; cada fila con preview; y el widget de
iconsets **no toca** el override propio de kdock.

## Lo implementado

- `src/appearancecontrol.{h,cpp}` — `AppearanceControl`, context property `appearance`:
  `iconThemes()`, `colorSchemes()` (con `bg`/`fg`/`sel` para el preview), `refreshIfStale()`,
  `applyIconTheme()`, `applyColorScheme()`, `currentIconTheme`/`currentColorScheme`.
  `findTool()` busca `plasma-changeicons` en varios `libexec` y cae a `kwriteconfig6`.
- `qml/ThemeListPopup.qml` — un popup con `mode: "icons"|"colors"`: buscador, contador,
  `ListView` con `ScrollBar`, previews por fila y check en la activa. Patrón de foco de
  teclado de `AppMenuPopup`.
- `qml/Dock.qml` — tokens `iconthemes` y `colorschemes` con sus `Component`, tooltip que
  muestra lo aplicado, y popup posicionado según el borde del dock.
- `src/dockconfig.*` — `showIconThemes` / `showColorSchemes` (default false), tokens en
  `knownWidgetTokens()` y nombres en `defaultWidgetLabel()`.
- Plomería: `CMakeLists.txt`, `src/main.cpp`, `src/dockmanager.*`, `src/dockwindow.*`,
  y dos checkboxes en Ajustes → Widgets.

## Diferencias respecto del plan original

- Los dos widgets quedaron como **bloques** (`isBlock()`), igual que red/discos/portapapeles:
  manejan su propio mouse. Como widgets simples, el `Binding` de `hovered` de la sección
  avisaba *"Property 'hovered' does not exist"* y el clic se lo llevaba el `MouseArea` de la
  sección. Costo: no se arrastran para reordenar (mismo caso que los otros popups).
- Se agregó un fallback de nombre para los `index.theme` que traen el placeholder de
  empaquetado sin sustituir (`Name=@ThemeName@` → se muestra el id).

## Verificación hecha

Arnés de Xvfb de `CLAUDE.md`, con `~/.local/share/{icons,color-schemes}` enlazados dentro del
`XDG_DATA_HOME` de prueba (si no, solo se ven los temas del sistema):

- Listados: 122 iconsets (33 ms) y 445 esquemas (2 ms), cero entradas con campos vacíos.
- Popups abiertos por código: sin warnings de QML ni *binding loops*.
- `applyIconTheme()` con el tema ya activo: `plasma-changeicons` respondió
  *"Icon theme is already used"* y `kdeglobals` quedó byte a byte idéntico.
