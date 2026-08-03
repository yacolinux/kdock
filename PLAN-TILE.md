# PLAN-TILE.md — `kdock-tilemenu`: menú de mosaicos a pantalla completa

> **Estado: implementado (2026-08-03).** Este archivo queda como el registro de por qué se
> eligió cada cosa. La documentación viva —la que hay que mantener al día— está en
> `AGENTS.md` → *Menú de mosaicos*, y las recetas de build y arneses en `CLAUDE.md`.
> Falta un solo paso, que es decisión del usuario: **instalar** (`sudo cmake --install build`)
> y reiniciar el dock.

## Contexto

kdock ya tiene un menú de aplicaciones tipo Kickoff: el widget `menu` (token `"menu"`,
`config.showMenuButton`) abre `qml/AppMenuPopup.qml`, un `Popup` de tamaño fijo con buscador,
barra lateral de categorías y grilla de 1..8 columnas. El backend es `src/appmenu.cpp`
(`AppMenu`), que agrupa los `.desktop` por categoría freedesktop, lee los submenús de los
archivos `.menu` (`src/xdgmenutree.cpp`) y maneja las favoritas.

Falta un **menú de pantalla completa con disposición libre**: una matriz de celdas
rectangulares donde cada app ocupa un mosaico que el usuario **arrastra a donde quiere**, con
tamaños distintos, grupos con etiqueta, y barra lateral de categorías opcional a izquierda o
derecha. Referencia: `plasma-applet-tiledmenu-prime`. **Lo mejoramos** en que cada sección
—Favoritos, Todas, cada categoría, cada submenú `.menu`— recuerda su propia disposición, no
solo un lienzo de anclados.

El lienzo tiene que **cubrir todo el espacio disponible del escritorio menos el dock (o los
docks) visibles**, sin importar en qué borde estén ni cuántos haya.

### Las dos decisiones de arquitectura que definen este plan

**1. Es un binario aparte, `kdock-tilemenu`, al estilo de `kdock-previews`.** Lanzado solo
por el widget desde un dock. Y a diferencia de `kdock-previews`, **no necesita ningún
privilegio de KWin** (no usa `org_kde_plasma_window_management` ni `ScreenShot2`): se corre
directo desde `build/`, sin `.desktop` temporal y sin tocar ksycoca — o sea que desaparece la
fricción de desarrollo más cara del otro binario, incluida la trampa del `kbuildsycoca6`.

**2. La ventana es un toplevel normal *maximizado*, no una superficie layer-shell.**
KWin ya resta los *struts* de las superficies layer-shell al calcular el área de
maximización: una ventana maximizada es, por definición, "todo el escritorio menos los
paneles". Eso nos da gratis, y correctamente:

- cualquier borde (abajo/arriba/izquierda/derecha) y cualquier cantidad de docks;
- los paneles de Plasma y las tiras de `kdock-previews`, que también reservan espacio;
- el **autohide**: un dock en autohide reserva 0, así que la ventana cubre toda la pantalla
  y el dock —que vive en la capa `top`, por encima de las ventanas normales— se dibuja
  encima al revelarse. Sin márgenes calculados a mano;
- el foco de teclado, que en una ventana normal es lo que pasa por defecto (nada del baile
  de `keyboard_interactivity` double-buffered que necesita `AppMenuPopup`);
- y **se puede probar bajo Xvfb**, porque ahí es una ventana X normal que se captura entera
  con `import -window root`.

Consecuencia directa: **este binario no compila `src/layershell.cpp` ni setea
`QT_WAYLAND_SHELL_INTEGRATION`**. Menos código, menos superficie de rotura.

## Decisiones ya tomadas (respondidas por el usuario)

| Decisión | Elegido |
|---|---|
| Alcance de la grilla libre | **Cada sección tiene su propia grilla** (auto por defecto, libre en cuanto se arrastra) |
| Tamaños de mosaico | **Varios**: 1x1, 2x1, 1x2, 2x2, 4x2, 4x4 |
| Persistencia del layout | **Global**: un solo proceso, un solo archivo → todos los docks ven lo mismo |
| Extras de la v1 | **Los cuatro**: grupos con etiqueta, color/imagen por mosaico, ícono+nombre por mosaico con override individual, índice alfabético |
| Ciclo de vida | **Residente tras el primer clic**; los siguientes son toggle por D-Bus. Opción "precargar al iniciar", default off |
| Cierre | **Esc + botón X + al perder el foco + al lanzar una app**, los cuatro anulables por un **checkbox "Mantener abierto"** en una esquina |
| Clic en el fondo vacío | **No cierra** (descartado: sorprende al soltar un arrastre fallido) |

## Árbol nuevo

```
tilemenu/
  CMakeLists.txt                   — add_subdirectory desde el CMakeLists raíz
  kdock-tilemenu.desktop.in        — NoDisplay=true; SIN claves de privilegio
  src/main.cpp                     — CLI + candado de instancia única
  src/tilemenuservice.{h,cpp}      — org.kdock.TileMenu en /TileMenu
  src/tileconfig.{h,cpp}           — QSettings tilemenu.conf: apariencia + layout
  src/tilelayout.{h,cpp}           — MOTOR: auto-placement, colisiones, grupos, resize
  src/tilemodel.{h,cpp}            — QAbstractListModel de la sección visible
  src/tilewindow.{h,cpp}           — QQuickView maximizado + frameless + cierre por foco
  src/tilesettingsdialog.{h,cpp}   — panel propio, Qt Widgets
  qml/TileMenu.qml                 — raíz: sidebar + lienzo + buscador + footer
  qml/TileMenuTile.qml             — un mosaico: ícono/nombre/color/imagen, arrastre, menú
  qml/TileGroupBand.qml            — cabecera de grupo (título, colapsar, reordenar)
  qml/TileSidebar.qml              — lista de secciones
  qml/TileSizeMenu.qml             — submenú de tamaños, reusable
```

**Fuentes de `src/` compiladas de nuevo, sin modificar** (mismo criterio que
`previews/CMakeLists.txt:24`, que ya lo hace con cinco): `desktopentry`, `appmenu`,
`xdgmenutree`, `iconprovider`, `theme`, `dockconfig`, `powercontrol`, `iconpickerdialog`.
Verificado que ninguna arrastra dependencias fuera de Qt: `powercontrol.cpp` solo usa QtDBus,
`iconpickerdialog.cpp` solo QtWidgets, `iconprovider.cpp` y `xdgmenutree.cpp` solo QtCore/Gui.
`dockconfig.cpp` entra por la misma razón que en previews (`theme.cpp` le pide
`settingsFilePath()`) **y** porque `AppMenu` recibe un `DockConfig*` para las favoritas: se
instancia el ctor sin `dockId` (`DockConfig::DockConfig(QObject*)`, `src/dockconfig.h:305`),
que abre el archivo compartido — así el menú de mosaicos y el menú del dock comparten
favoritas y editor de menús sin duplicar nada.

**Sin protocolos de Wayland**: no hay `qt_generate_wayland_protocol_client_sources` en este
target.

## Los módulos

### `TileLayout` — el motor (C++ puro, sin GUI)

**Toda la lógica de colocación vive acá**, no en QML. Es la decisión que hace la feature
testeable: una sonda de consola ejercita arrastres, colisiones, resizes y grupos e imprime la
grilla en ASCII, sin abrir una ventana.

Un registro por mosaico: `{ id, group, col, row, w, h, bg, image, label, icon, showIcon,
showLabel }`, agrupados por clave de sección (`cat:__favorites__`, `cat:Internet`,
`menu:Web/Apps`… — las mismas que devuelve `AppMenu::sections()`).

**Resolución de una sección**: se pide la lista viva a `AppMenu::appsInCategory(key)` y se
cruza con los registros por `id`:
- app con registro → usa su posición, tamaño y apariencia;
- app sin registro (recién instalada, o sección nunca tocada) → **auto-placement**: primer
  hueco libre en orden lectura del último grupo, ordenando las nuevas por nombre;
- registro sin app (desinstalada) → se **conserva** (no se pierde la posición si se
  reinstala) pero no se dibuja. `resetSection()` los limpia.

**Grupos = bandas de filas dentro de una única matriz.** Cada grupo ocupa un rango contiguo
de filas; el modelo publica también la **fila absoluta** (`absRow = startRow(grupo) + row`).
Así hay **un solo espacio de coordenadas**: un `Repeater` posiciona todo por `(col, absRow)`,
las cabeceras de grupo son items en `startRow(N) * pitch`, el arrastre usa la misma
aritmética en cualquier parte del lienzo, y "en qué grupo lo soltaron" lo resuelve
`groupAtRow(absRow)` en C++. Reordenar o colapsar un grupo es recalcular `startRow`.

**Política de colisión al soltar** — deliberadamente manual y predecible, **sin
auto-compactar**, para que una disposición hecha a mano no se reacomode sola al mover un
vecino:
1. el rect destino se clampea a `[0, columns)`;
2. si está libre (ignorando el propio mosaico) → se coloca;
3. si lo ocupa **exactamente un** mosaico **del mismo tamaño** → **swap**;
4. si no → **rechazo**: `moveTile()` devuelve `false`, el QML lo devuelve a su lugar y el
   fantasma se pinta en rojo.

API (toda persiste y emite señal): `moveTile`, `resizeTile`, `setTileProperty`, `resetTile`,
`addGroup`, `renameGroup`, `moveGroup`, `removeGroup` (sus mosaicos caen al grupo anterior),
`resetSection`, `isCustomized(section)` (gobierna si se muestra el índice A-Z).

### `TileConfig` — configuración y persistencia

`QSettings` en `~/.local/share/kdock/tilemenu.conf`, al lado de `previews.conf`.

El **layout** va como un único JSON compacto bajo `[General] layout=`. Un blob y no claves
por sección porque las claves de sección llevan `/` y `:`, que QSettings interpreta como
jerarquía de grupos. Forma:

```json
{"cat:__favorites__":{"g":[{"t":"Trabajo","tiles":[
   {"id":"firefox.desktop","c":0,"r":0,"w":2,"h":2,"bg":"#3daee9"},
   {"id":"org.kde.kate.desktop","c":2,"r":0,"w":1,"h":1,"label":"Editor"}]}]}}
```

Claves de apariencia (todas en el mismo archivo, todas con panel propio):

| Clave | Default | Qué es |
|---|---|---|
| `columns` | `10` | columnas de la matriz; `0` = auto-ajustar al ancho |
| `cellSize` | `96` | px de la celda cuando no se estira |
| `cellStretch` | `true` | la celda crece para que la matriz llene el ancho disponible |
| `cellMin` / `cellMax` | `48` / `220` | límites del estiramiento; fuera de rango, el lienzo se centra |
| `cellSpacing` | `8` | separación entre celdas |
| `sidebar` | `0` | 0 izquierda / 1 derecha / 2 oculta |
| `sidebarWidth` | `200` | px |
| `showIcons` / `showLabels` | `true` / `true` | globales; cada mosaico puede sobreescribirlos |
| `iconScale` | `55` | % del lado corto de la celda |
| `labelPosition` | `0` | 0 debajo / 1 al lado (mosaicos anchos) |
| `backgroundMode` | `0` | 0 color del tema / 1 color propio |
| `backgroundColor` / `backgroundOpacity` / `backgroundImage` | — / `0.92` / vacío | fondo del lienzo |
| `showSearch` / `showPower` / `showLetterIndex` | `true` | elementos opcionales |
| `closeOnLaunch` / `closeOnFocusLoss` | `true` / `true` | anulados por "Mantener abierto" |
| `keepOpen` | `false` | el checkbox de la esquina, persistido |
| `rememberSection` | `true` | reabre en la última sección usada |
| `preload` | `false` | lanzarlo al iniciar kdock en vez de en el primer clic |

`TileConfig` emite **una sola señal `settingsChanged()`** como `NOTIFY` de todas las
`Q_PROPERTY` de apariencia: el QML reevalúa sus bindings ante ella y evitamos ~20 señales
para una pantalla que se repinta entera igual. El layout tiene la suya
(`layoutChanged(section)`), que sí es granular porque mueve mosaicos.

### `TileModel` — `QAbstractListModel` de la sección visible

Roles: `id, name, comment, icon, favorite, group, col, row, absRow, w, h, bg, image,
showIcon, showLabel, label`. `setSection(key)` recarga; `setQuery(text)` cambia a modo
búsqueda (`AppMenu::search()`, grilla automática, sin arrastre — no hay sección donde
persistir una posición).

Es un modelo y no un `QVariantList` **para que un `moveTile` sea un `dataChanged` de una
fila** en vez de reconstruir todo: el mosaico se anima a su posición final con
`Behavior on x/y` y el resto del lienzo ni parpadea.

### `TileWindow` — la ventana

`QQuickView` con `setResizeMode(SizeRootObjectToView)` (al revés que el dock, que usa
`SizeViewToRootObject`), `Qt::Window | Qt::FramelessWindowHint`, `showMaximized()`.

- **`show(screenName)`**: antes de mostrar, `setScreen()` al `QScreen` del dock que la lanzó.
  En Wayland la ubicación de un toplevel la decide el compositor, así que esto es una
  *pista*: en la práctica KWin coloca la ventana nueva en la pantalla activa, que es donde
  está el puntero — y el puntero está ahí porque el usuario acaba de hacer clic en ese dock.
  Documentado como limitación conocida; ver *Riesgos*.
- **Cierre por pérdida de foco**: `connect(this, &QWindow::activeChanged, …)`. Dos guardias
  imprescindibles: (a) ignorar desactivaciones durante los primeros ~300 ms (recién
  mostrada, `isActive()` todavía puede ser falso); (b) un flag mientras hay un diálogo
  propio arriba (panel de configuración, `IconPickerDialog`, `QColorDialog`, `QFileDialog`),
  o abrir la configuración cerraría el menú que la abrió. Los menús contextuales no cuentan:
  son `xdg_popup` hijos y no desactivan al padre.
- **"Mantener abierto"**: checkbox en la esquina (junto a la X). Con él puesto se desactivan
  los cuatro caminos de cierre automático y la ventana queda como una ventana normal más —
  se puede mandar al fondo, alt-tabear y volver. Persistido en `keepOpen`.
- Registra su propio `IconProvider` (`engine()->addImageProvider("icon", …)`) y expone como
  context properties: `theme`, `tileConfig`, `appMenu`, `tiles` (el `TileModel`), `layout`
  (el `TileLayout`), `power`, `apps`, `win` (ella misma).
- Al ocultarse **no se destruye**: `hide()`/`show()`, para que el toggle sea instantáneo.

### `TileMenuService` — D-Bus

`org.kdock.TileMenu` en `/TileMenu`, calcado de `previews/src/previewsservice.cpp`:
`toggle(QString screenName)`, `show(QString screenName)`, `hide()`, `showSettings()`,
`reload()`, `quit()`. **Ser dueño del nombre del bus es el candado de instancia única**, igual
que en previews.

`main.cpp`: banderas `--toggle`, `--show`, `--hide`, `--settings`, `--screen NAME`,
`--dump-layout` (imprime la disposición resuelta en ASCII y sale — el diagnóstico que
`--dump-captures` es para previews). Si ya hay instancia, reenvía por D-Bus y sale con 0.

### `TileSettingsDialog` — panel propio

Qt Widgets, modelado sobre `previews/src/previewsettingsdialog.cpp`. Grupos: *Grilla*
(columnas, tamaño de celda, estiramiento, separación), *Apariencia* (fondo, opacidad,
imagen, ícono/nombre, escala, posición del nombre), *Barra lateral* (posición, ancho),
*Comportamiento* (buscador, apagado, índice A-Z, cerrar al lanzar, cerrar al perder foco,
recordar sección, precargar), *Disposición* (Exportar…, Importar…, Restablecer sección,
Restablecer todo).

## La UI (`qml/TileMenu.qml`)

```
Item (llena la ventana; KWin le dio el área útil por maximizar)
├── Rectangle de fondo (color del tema o propio, opacidad, imagen opcional)
├── esquina superior derecha: [☑ Mantener abierto]  [✕]
└── Row
    ├── [TileSidebar, si sidebar == izquierda]
    │     ListView sobre appMenu.sections() — mismo modelo que AppMenuPopup:
    │     Favoritos, Todas, categorías, submenús .menu indentados por `depth`
    ├── Column (área principal)
    │     ├── TextField de búsqueda (opcional)
    │     ├── Flickable → Item (lienzo)
    │     │     ├── Repeater de TileGroupBand  (cabecera en startRow(N)*pitch)
    │     │     └── Repeater de TileMenuTile   (x = col*pitch, y = absRow*pitch,
    │     │                                     w = w*pitch - spacing, idem h)
    │     └── fila de sesión/apagado (opcional, reusa `power` como AppMenuPopup)
    ├── [rail A-Z, visible solo si !layout.isCustomized(sección)]
    └── [TileSidebar, si sidebar == derecha]
```

- **Geometría**: `pitch = cellSize + cellSpacing`. Con `cellStretch`, `cellSize` se recalcula
  como `floor(anchoDisponible / columns) - cellSpacing`, acotado a `[cellMin, cellMax]`; si
  el clamp muerde, el lienzo se centra. Con `columns == 0`, las columnas salen del ancho y el
  tamaño de celda queda fijo. Sin realimentación: el ancho disponible no depende de la celda.
- **Arrastre**: `MouseArea` con `drag.target` sobre el mosaico. Mientras se arrastra, un
  fantasma marca la celda destino — verde libre, azul swap, rojo rechazo. Al soltar,
  `layout.moveTile(...)`: el modelo es la autoridad y el mosaico salta (animado) a donde el
  motor lo puso. Soltar sobre otra banda lo cambia de grupo.
- **Menú contextual del mosaico** (`popupType: Popup.Window`, obligatorio en Wayland o queda
  recortado): Tamaño ▸ (`TileSizeMenu.qml`), Color de fondo ▸ (los ocho colores rápidos de
  `DockConfig::panelPresetColors`, como `qml/BackgroundColorMenu.qml`), Imagen de fondo…,
  Mostrar ícono / Mostrar nombre (override del global), Renombrar…, Cambiar ícono…
  (`IconPickerDialog`, reusado tal cual), Anclar a favoritos, Restablecer mosaico.
- **Menú contextual del lienzo**: Nuevo grupo…, Restablecer esta sección, Configurar…
- **Índice A-Z**: rail vertical del lado opuesto al sidebar, solo cuando la sección **no**
  está personalizada — con posiciones puestas a mano, "saltar a la K" no significa nada.
- **Teclado**: foco al buscador al abrir, flechas mueven la selección por la matriz, Enter
  lanza, Esc cierra. Sin `Timer` de reintento de foco: es una ventana normal.

## Lo que se toca en kdock (el widget)

Widget nuevo, token **`tilemenu`**. El backend es un lanzador estático, así que **no toca
`dockmanager.*`** (igual que `PreviewsLauncher`, que ni siquiera está en `Shared`):

1. **`src/tilemenulauncher.{h,cpp}`** — copia de `src/previewslauncher.cpp` con los nombres
   del binario/servicio nuevos, más `toggle(screenName)`: si corre, D-Bus; si no, lanza el
   binario con `--toggle --screen <name>`. Agregarlo a `CMakeLists.txt`.
2. **`src/dockconfig.{h,cpp}`** — `showTileMenu` (bool, default false) y `tileMenuIcon`
   (default `view-list-icons`): `Q_PROPERTY`/getter/setter/señal/miembro, `load()`,
   **`knownWidgetTokens()`** ← *la línea que se olvida y no da ningún error: sin ella
   `reconcileWidgetOrder()` descarta el token y el widget no aparece nunca*, y
   `defaultWidgetLabel()` (`{"tilemenu", tr("Menú de mosaicos")}`).
3. **`src/dockwindow.{h,cpp}`** — instancia de `TileMenuLauncher` +
   `setContextProperty("tileLauncher", …)`.
4. **`qml/Dock.qml`** — `sectionVisible("tilemenu")` → `config.showTileMenu && tileLauncher`;
   `componentFor` → `tileMenuComp`; el `Component` (bloque con `MouseArea` propio y menú
   contextual, calcado de `menuComp`, `qml/Dock.qml:1689`); **`isBlock()`** ← agregarlo o el
   `Binding` de `hovered` de la sección escupe *"Property 'hovered' does not exist"* en cada
   arranque; `sectionTooltip`. Clic izquierdo = `tileLauncher.toggle(config.screenName)`;
   clic derecho = menú con *Configurar menú de mosaicos…* / *Configuración del dock…*.
5. **`src/settingsdialog.cpp`** — un `QGroupBox` "Menú de mosaicos" al final de la solapa
   **Menu** (`createMenuTab()`, `src/settingsdialog.cpp:983`), con el checkbox del widget, el
   selector de ícono (patrón `menuIconBtn`), el estado del proceso y el botón *Configurar…*
   que abre el panel propio del binario. Mismo reparto que Previews.
   **Deliberadamente no agregamos una solapa**: con once, la barra ya pide 1086 px sobre un
   diálogo de 1120 y una duodécima cae en modo flechas de scroll sin avisar (AGENTS.md → *La
   solapa nueva desbordó la barra*).
6. **`src/main.cpp`** — una línea junto a `previews.startIfEnabled()` para la opción
   *precargar*.
7. **`CMakeLists.txt` raíz** — `add_subdirectory(tilemenu)`.

## Backup

`ConfigArchive` archiva solo `kdock*.conf` (`src/configarchive.cpp:32`), así que hoy
`previews.conf` tampoco se respalda. Se extiende el filtro a los tres patrones
(`kdock*.conf`, `previews*.conf`, `tilemenu*.conf`) — un arreglo contenido que de paso cubre
previews. **Cuidado en el import**: la sustitución limpia borra los archivos actuales antes
de escribir los del `.zip`, así que un archivo viejo (sin `tilemenu.conf`) borraría la
disposición del usuario. Regla: **solo se borran los archivos de un patrón del que el
archivo traiga al menos una entrada**. El chequeo de "tiene que venir `kdock.conf`" se
mantiene igual.

## Orden de implementación

1. **Esqueleto del binario**: `tilemenu/CMakeLists.txt`, `main.cpp`, `TileMenuService`,
   `TileWindow` mostrando un `Rectangle` de color. Cierra la pregunta de si maximizado +
   frameless da exactamente el área útil (paso 1 de *Verificación*).
2. **`TileConfig` + `TileLayout`**: JSON, auto-placement, colisiones, grupos, resize. Se
   cierra con la sonda de consola / `--dump-layout`, sin UI.
3. **`TileModel` + `qml/TileMenu.qml` estático**: sidebar, lienzo, mosaicos dibujados desde
   el modelo, sin arrastre.
4. **Arrastre + fantasma + menú contextual del mosaico** (tamaños, color, imagen, ícono,
   nombre, overrides).
5. **Grupos**: crear, renombrar, reordenar, colapsar, mover mosaicos entre bandas.
6. **Buscador, índice A-Z, fila de apagado, teclado, X y "Mantener abierto".**
7. **`TileSettingsDialog`** completo, con exportar/importar.
8. **El widget en kdock** (los siete puntos de arriba) + el arreglo de `ConfigArchive`.
9. **Documentación**: sección temática en `AGENTS.md` + tabla QML↔C++; en `CLAUDE.md`, la
   receta de arranque/prueba del binario nuevo y cualquier trampa que haya mordido.

## Verificación

**1. Que la ventana maximizada dé el área útil exacta** (sesión Wayland real, sin tocar nada
del usuario). El binario se corre directo, sin `.desktop` ni ksycoca:

```bash
./build/tilemenu/kdock-tilemenu --show &
spectacle-custom -b -n -f -o /tmp/tile.png    # el spectacle de /usr/bin NO está autorizado
```

Se comprueba en la captura que el lienzo llegue a los cuatro bordes **menos** la franja del
dock, y se repite moviendo el dock a los cuatro bordes y con dos docks a la vez. Se mira
también que **no haya decoración** (`Qt::FramelessWindowHint`) — si KWin igual la dibuja, ver
*Riesgos*.

**2. El motor de disposición, sin GUI** (~2 s):

```bash
./build/tilemenu/kdock-tilemenu --dump-layout          # imprime la grilla en ASCII
```

Y una sonda que linkea los `.o` del build salteando `main.cpp.o` (receta de `CLAUDE.md`) para
ejercitar las transiciones: auto-placement inicial, `moveTile` a un hueco, sobre un mosaico
del mismo tamaño (**swap**), sobre uno de tamaño distinto (**rechazo**), `resizeTile` que se
sale de las columnas (**clampeo**), `addGroup` + `moveGroup`, y app desinstalada (registro
conservado, no dibujado). **Correrla dos veces sobre el mismo `XDG_DATA_HOME`** es lo único
que distingue "lo hizo en memoria" de "lo guardó". `XDG_DATA_HOME` descartable siempre, o le
escribe la config real al usuario.

**3. Ver la UI entera bajo Xvfb** — esto es lo que el plan layer-shell no permitía:

```bash
qmllint tilemenu/qml/*.qml 2>&1 | grep -iE "error|syntax"
rm -rf /tmp/tm && mkdir -p /tmp/tm/kdock
ln -s ~/.local/share/applications /tmp/tm/applications
ln -s ~/.local/share/desktop-directories /tmp/tm/desktop-directories
ln -s ~/.local/share/icons /tmp/tm/icons
XDG_DATA_HOME=/tmp/tm QT_QPA_PLATFORM=xcb QT_QUICK_BACKEND=software \
  xvfb-run -s "-screen 0 1920x1080x24" ./build/tilemenu/kdock-tilemenu --show &
sleep 5; import -window root /tmp/tile-xvfb.png
```

Los tres enlaces son obligatorios: con un `XDG_DATA_HOME` aislado y sin ellos, no se ven los
`.desktop`, ni los `.directory`, ni los íconos, **los submenús salen vacíos y parece un bug
del código** (mordió en el arnés de `AppMenuPopup`, 2026-07-31). `QT_QUICK_BACKEND=software`
también es obligatorio o el PNG sale negro.

**4. Control positivo**: romper `TileMenu.qml` a propósito (`property int x: noExiste.nada`),
confirmar que la corrida lo reporta, y revertir. Silencio es también lo que se ve cuando el
QML nunca se cargó.

**5. El widget, con el arnés de Xvfb del dock** (que no rompa el dock ni su QML):

```bash
qmllint qml/*.qml previews/qml/*.qml 2>&1 | grep -iE "error|syntax"
rm -rf /tmp/kdock-t && mkdir -p /tmp/kdock-t/kdock
cp <config-de-prueba>.conf /tmp/kdock-t/kdock/kdock.conf          # showTileMenu=true
cp /tmp/kdock-t/kdock/kdock.conf /tmp/kdock-t/kdock/kdock-screen.conf
timeout 7 xvfb-run -s "-screen 0 1920x1080x24" \
    env XDG_DATA_HOME=/tmp/kdock-t QT_QPA_PLATFORM=xcb ./build/kdock > /tmp/x.log 2>&1
grep -vE "Failed to get image|QDBusArgument|^kdock: systray" /tmp/x.log
```

Silencio = limpio. **No encadenar `timeout` a un pipe** (se pierde la salida y una corrida
rota parece limpia). `qmllint` **no** reemplaza al arnés: no ve los errores de scope de ids.
`QT_QPA_PLATFORM=offscreen` no sirve (sin nombre de pantalla no se crea ningún dock y el QML
nunca se instancia).

**6. Prueba manual final** en la sesión real: toggle desde el widget, arrastrar mosaicos
entre celdas y grupos, cambiar tamaños, buscar, lanzar una app (cierra), "Mantener abierto"
puesto (no cierra ni al perder foco ni al lanzar, y se puede alt-tabear de vuelta), Esc, X,
reabrir y confirmar que la disposición volvió igual; matar el proceso y reabrir para
confirmar que persistió en disco.

**7. Instalación** (solo con el OK del usuario): backup primero —binarios + `.desktop` a
`/usr/local/share/kdock-backup/<timestamp>/` y los `.conf` a
`~/.local/share/kdock/backup-<timestamp>/`—, después `sudo cmake --install build`. No hace
falta ksycoca: este binario no pide privilegios. Reiniciarle el dock al usuario le deja la
pantalla sin dock: **preguntar antes**.

## Riesgos anotados

| Riesgo | Mitigación |
|---|---|
| KWin decora igual la ventana frameless | Se ve en el paso 1. Plan B: `Qt::FramelessWindowHint` + `xdg_toplevel_decoration` en modo cliente ya es lo que hace Qt; si fallara, la última carta es `showFullScreen()` + márgenes calculados del dock — se pierde el "gratis" pero solo en ese modo |
| En Wayland el cliente no elige el monitor de un toplevel | `setScreen()` como pista; en la práctica KWin usa la pantalla activa, que es donde está el puntero que acaba de hacer clic en el dock. Documentado |
| El menú aparece en el gestor de tareas / alt-tab | Con "Mantener abierto" eso es **deseable**. Sin él la ventana vive segundos y se cierra al perder foco, así que apenas se nota. No se tocan reglas de KWin |
| Cerrar por pérdida de foco mata al menú al abrir su propia configuración | Flag mientras hay diálogo propio arriba + ventana muerta de ~300 ms tras `show()` |
| Posiciones que se rompen al cambiar de resolución o de monitor | Columnas **fijas por config**; lo que escala es la celda, no la matriz |
| Import de un `.zip` viejo que borre `tilemenu.conf` | Solo se borran los patrones de los que el archivo trae al menos una entrada |
| `knownWidgetTokens()` olvidado | Está en el checklist; el síntoma es "el widget no aparece nunca y no se imprime nada" |
| Popups recortados dentro de la superficie | `popupType: Popup.Window` en todos los menús contextuales del lienzo |
