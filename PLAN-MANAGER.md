# PLAN-MANAGER.md — `kdock-controlmanager`: centro de control en panel

> **Estado: implementado y funcional (2026-08-08).** El árbol `controlmanager/` compila
> dentro de `build/`, el widget `controlmanager` del dock lo prende y apaga, y el panel de
> ajustes propio (`CmSettingsDialog`) está en uso. Referencia visual:
> `ejemplo-kdock-controlmanager.jpg` (la solapa Home con sus tarjetas),
> `ejemplo2.jpg` (la solapa Config del original) y `ejemplo3.jpg` (el panel flotando sobre
> el escritorio, anclado arriba y al centro).
> La documentación viva está en `AGENTS.md` → *Control Manager* (incluido el cierre del bug
> del tamaño de fuente del widget y la mejora de botones redimensionables), y las recetas de
> build/arnés a `CLAUDE.md`.

## Contexto

kdock tiene hoy un widget por cada cosa que se controla: volumen, brillo, batería/perfil,
modo oscuro, red, discos, wallpaper. Cada uno es un ícono en la barra, con su popup chico y
su propio menú. Eso funciona para el gesto rápido, pero **no hay una sola superficie donde
ver y tocar todo junto**, y varias de esas cosas se quedaron a mitad de camino: el brillo
solo maneja el backlight interno (`brightnessctl`), el mezclador completo vive escondido en
una solapa del diálogo de Configuración, no hay nada de reproducción multimedia, y para
llegar a las cuatro configuraciones (KDE, kdock, tilemenu, previews) hay que saber de memoria
dónde está cada botón.

**ControlManager** es ese lugar: un **quinto binario** —al estilo de `kdock-tilemenu`— que
el dock prende y apaga desde un widget. Un panel anclado a un borde de la pantalla, con
**solapas horizontales** (Principal, Audio, Video/Energía, Calendario, Sistema, Play, Red,
Wallpaper) y, en la primera solapa, una **grilla de tarjetas** reacomodables y
redimensionables igual que los mosaicos del menú de mosaicos. Cada sección es un módulo:
tiene su solapa propia *y* puede aparecer como tarjeta en Principal, a elección del usuario.

**No maneja íconos de aplicaciones para nada** — eso es del dock y del menú de mosaicos.

### Las cuatro decisiones de arquitectura (elegidas por el usuario)

**1. La ventana es una superficie *layer-shell* anclada, no un toplevel.**
Al revés que `kdock-tilemenu` (que se maximiza y deja que KWin le reste los struts), este
panel tiene tamaño propio y tiene que **aparecer donde el usuario diga**: en Wayland un
cliente no puede ubicar un toplevel, así que un panel de 900x420 lo colocaría KWin al medio
de la pantalla activa y no habría forma de anclarlo arriba-centro como el mockup. Con
layer-shell se pide `anchor` + `margins` + `layer=top` y listo. Se reusa
**`src/layershell.cpp` tal cual** (`kdock-previews` ya lo compila así) y **no hace falta
ningún privilegio de KWin**: `zwlr_layer_shell_v1` se anuncia a clientes sin autorizar
(verificado con `wayland-info` en esta sesión), así que su `.desktop` no necesita refrescar
ksycoca, igual que el del menú de mosaicos.

**2. Las siete secciones entran de una** (Audio, Video/Energía, Calendario, Sistema, Play,
Red, Wallpaper). Cuatro reusan backends que ya existen y son QML-ready; lo único nuevo de
verdad es el brillo multi-monitor y MPRIS.

**3. La configuración es un `QDialog` de Qt Widgets aparte**, clonando `TileSettingsDialog`.
Consistente con los otros dos accesorios y con la capa de traducciones ya resuelta.

**4. kdock gana un servicio D-Bus mínimo `org.kdock.Dock`** (abrir Configuración, reiniciar,
modo oscuro en vivo) **más un atajo global de KDE para el modo oscuro**. Sin eso no hay
camino: kdock no expone D-Bus hoy, su diálogo solo se abre desde su propio menú, y escribir
`darkModeOn` en el `.conf` desde afuera **no repinta** los docks corriendo (no hay watcher de
archivos; ver `AGENTS.md` → *Modo Dark*).

---

## 1. Árbol nuevo y qué reusa

```
controlmanager/
  CMakeLists.txt
  kdock-controlmanager.desktop.in
  src/main.cpp
  src/cmconfig.{h,cpp}          # settings + persistencia (clon de tileconfig)
  src/cmsections.{h,cpp}        # registro estático de secciones (id, label, ícono, tamaños)
  src/cmlayout.{h,cpp}          # motor de grilla de la solapa Principal (clon de tilelayout)
  src/cmmodel.{h,cpp}           # QAbstractListModel de las tarjetas visibles
  src/cmwindow.{h,cpp}          # QQuickView layer-shell (mezcla de previewwindow + tilewindow)
  src/cmsettingsdialog.{h,cpp}  # panel propio, Qt Widgets (clon de tilesettingsdialog)
  src/cmservice.{h,cpp}         # org.kdock.ControlManager (clon de tilemenuservice)
  src/screenbrightness.{h,cpp}  # NUEVO: brillo por monitor (org.kde.ScreenBrightness)
  src/mpriscontrol.{h,cpp}      # NUEVO: reproductores MPRIS2
  src/docklink.{h,cpp}          # NUEVO: cliente de org.kdock.Dock (modo oscuro, settings, restart)
  qml/ControlManager.qml        # raíz: fondo + barra de solapas + lienzo
  qml/CmTabs.qml                # solapas horizontales (adaptado de TileGroupTabs)
  qml/CmCard.qml                # marco de tarjeta + arrastre (adaptado de TileMenuTile)
  qml/cards/AudioCard.qml  VideoEnergyCard.qml  CalendarCard.qml  SystemCard.qml
            PlayCard.qml   NetworkCard.qml      WallpaperCard.qml ClockCard.qml
```

**Fuentes de `src/` compiladas de nuevo acá** (mismo criterio que `previews/` y `tilemenu/`:
son autocontenidas y compilarlas dos veces deja el target `kdock` intacto):

| Fuente | Para qué |
| --- | --- |
| `layershell.cpp` | la superficie anclada |
| `theme.cpp`, `iconprovider.cpp` | colores e íconos, mismo iconset que el dock |
| `dockconfig.cpp` | `Theme` le pregunta dónde está el `.conf` compartido; además de ahí salen los colores rápidos y los del modo oscuro |
| `translations.cpp` | la capa de traducciones, en modo `BaseOnly` |
| `audiocontrol.cpp` | sección **Audio** (mezclador completo, `pactl`) |
| `batterycontrol.cpp` | perfil de energía y batería (UPower + power-profiles-daemon) |
| `brightnesscontrol.cpp` | fallback de brillo cuando no hay PowerDevil |
| `networkcontrol.cpp` + `nmdbus.cpp` | sección **Red** |
| `wallpapercontrol.cpp` | sección **Wallpaper** (avance por monitor) |
| `powercontrol.cpp` | bloquear / cerrar sesión / suspender / reiniciar / apagar en **Sistema** |
| `desktopentry.cpp` | lanzar `systemsettings` y demás por id de `.desktop` |
| `iconpickerdialog.cpp` | elegir el ícono de una tarjeta desde el panel de config |

**Único cambio a una fuente compartida**: `AudioControl` expone hoy `QVector<Device>`, que QML
no ve. Se le agregan tres `Q_INVOKABLE QVariantList outputList() / inputList() / appList()`
que arman el mapa por dispositivo (`type, index, name, description, icon, volume, muted,
isDefault`). Es aditivo y no toca ni un camino existente — kdock linkea el mismo archivo.

---

## 2. La ventana (`CmWindow`)

Mezcla de `PreviewWindow` (la parte layer-shell) y `TileWindow` (mostrar/ocultar, foco,
diálogos modales).

- Propiedades antes del primer `show()`, igual que `PreviewWindow::applyLayerProperties()`:
  `kdock.layershell=true`, `kdock.layer=2` (top), `kdock.anchors` según `edge`+`alignment`,
  `kdock.margins` con el margen de pantalla, `kdock.output` resuelto desde el `QScreen`
  destino (el conector que manda el dock que abrió).
- **Zona exclusiva siempre 0**: es un panel que se abre y se cierra, no reserva espacio, así
  que nunca le mueve las ventanas maximizadas al usuario.
- **Tamaño**: `SizeViewToRootObject` y el QML raíz toma `width`/`height` de
  `cmConfig.panelWidth/panelHeight` (px, acotados a la pantalla). Opción "porcentaje de la
  pantalla" para cada eje (`panelWidthPercent`, 0 = usar px).
- **Teclado on-demand**: `kdock.keyboardInteractivity=1` (exclusive) al mostrar, `0` al
  ocultar. Es lo que da Esc y `activeChanged`, o sea el cierre al perder el foco. La misma
  guarda de 400 ms tras `show()` que tiene `TileWindow` (el compositor todavía está
  entregando el foco) y el mismo contador de diálogos propios arriba (`m_modalDepth`), o
  abrir la configuración cerraría el panel que la abrió.
- **Cierre**: ✕ de la esquina, Esc, perder el foco, y opcional "cerrar cuando el puntero
  sale" (`closeOnLeave`, apagado por defecto). Los cuatro anulados por el casillero
  **Ventana permanente** (`keepOpen`), que es el pedido explícito del usuario.
- Se `hide()`, nunca se destruye: el segundo clic del widget es instantáneo.
  Excepción: cambiar de pantalla o de borde **recrea** la superficie (el `wl_output` y el
  anchor se fijan al crearla) — mismo `destroy()` + `show()` que hace `PreviewWindow`.
- **`--dump-sections`**: diagnóstico read-only al estilo de `--dump-layout` / `--dump-captures`,
  imprime qué reporta cada backend (monitores con brillo, perfiles, reproductores MPRIS,
  conexiones, secciones habilitadas y la grilla de Principal en ASCII) y sale sin abrir
  ninguna ventana.

**D-Bus**: `org.kdock.ControlManager` en `/ControlManager`, con `toggle(screen)`, `show(screen)`,
`hide()`, `showSettings()`, `quit()`. Ser dueño del nombre **es** el candado de instancia
única, igual que en los otros dos accesorios.

**Config**: `~/.local/share/kdock/controlmanager.conf`, una sola para toda la sesión (hay un
solo proceso; la pantalla la elige quien lo abre). Claves:

- *Ventana*: `edge`, `alignment`, `panelWidth`, `panelHeight`, `panelWidthPercent`,
  `panelHeightPercent`, `screenMargin`, `keepOpen`, `closeOnLeave`, `preload`.
- *Apariencia*: `backgroundMode`, `backgroundColor`, `backgroundOpacity`, `backgroundImage`,
  `cornerRadius`, `labelBold`, `tabsPosition` (0 arriba / 1 abajo), `showTabIcons`.
- *Grilla*: `columns`, `cellSize`, `cellStretch`, `cellMin`, `cellMax`, `cellSpacing`.
- *Secciones*: `enabledSections` (lista de ids), `principalCards` (lista de ids),
  `rememberTab`, `lastTab`, y `layout` (el JSON de la grilla).

---

## 3. Secciones y grilla

**Registro estático en C++** (`cmsections.cpp`): una tabla de
`{ id, etiqueta capabase, ícono, tamaño por defecto (w×h celdas), tamaño mínimo, tiene solapa,
puede ir en Principal }`. Es la lista que recorren el modelo, el panel de configuración y el
`--dump-sections`; agregar una sección nueva es una fila más acá y un `.qml` en `cards/`.

**Solapas**: `Principal` primero y fija, después una por sección habilitada, en el orden que
el usuario deje en el panel de configuración. Barra horizontal, misma estética que
`TileGroupTabs` (ícono + nombre, resaltado en la actual), arriba o abajo según config.

**Solapa de una sección**: dibuja *la misma* componente de la tarjeta con `compact: false`,
ocupando todo el lienzo. O sea que cada `cards/*.qml` se escribe una vez y se ve en los dos
tamaños; la property `compact` decide qué se recorta (en chico: lo esencial y un encabezado
que lleva a la solapa completa).

**Solapa Principal**: la grilla. `CmLayout` es `TileLayout` recortado a **una sola grilla**
(sin secciones ni grupos: acá el universo son ~8 tarjetas), conservando lo que importa y ya
está probado:

- posiciones `(col, row)` + tamaño `(w, h)` en celdas; columnas fijas por config
  (0 = ajustar al ancho) y la **celda** es lo que se estira, acotada por `cellMin`/`cellMax`
  — así una posición guardada vale igual en cualquier monitor;
- auto-placement lineal (grilla de ocupación + cursor) para la tarjeta que todavía no tiene
  lugar;
- colisión manual: libre → coloca; mismo tamaño → **swap**; cualquier otro solapamiento →
  **rechazo**, y el QML devuelve la tarjeta a su lugar. `dropKind()` contesta lo mismo
  *antes* de soltar, así el fantasma no miente (azul libre / verde swap / rojo rechazo);
- agrandar **reubica** si no entra, nunca falla;
- las tarjetas de secciones deshabilitadas se guardan aparte (los `orphans` de `TileLayout`):
  volver a habilitar una recupera su lugar, y mientras tanto no participa de ninguna colisión;
- persistencia en un **único JSON** en la clave `layout` (las claves con `/` y `:` las lee
  QSettings como jerarquía de grupos — la misma razón que en el menú de mosaicos).

El arrastre en QML es `CmCard.qml`, calcado de `TileMenuTile.qml`: `Binding` de x/y con
`when: !dragging`, `Behavior` para animar a la celda final, y el objetivo del drop calculado
**desde el puntero**, no desde el centro de la tarjeta. Menú contextual **uno solo para todo
el lienzo** (parametrizado por "qué tarjeta lo abrió"), con: tamaño (1x1…4x3), color de
fondo, mostrar/ocultar nombre, "Ir a la solapa", y "Quitar de Principal".

---

## 4. Las siete secciones

| Sección | Backend | En chico (tarjeta) | En grande (solapa) |
| --- | --- | --- | --- |
| **Audio** | `AudioControl` (+3 `Q_INVOKABLE` nuevos) | dispositivo por defecto: nombre, slider, mute | mezclador completo: salidas, entradas y streams por app, con predeterminado/mute/volumen y el techo de 150% |
| **Video/Energía** | `ScreenBrightness` (nuevo) + `BatteryControl` + `DockLink` | slider de brillo del monitor activo + estado de perfil | un slider **por monitor** (con su nombre), selector de perfil de energía, y modo oscuro on/off |
| **Calendario** | ninguno (QML puro) | mes actual compacto, hoy resaltado | mes navegable + botón "Abrir kdock-calendar" |
| **Sistema** | `DockLink` + `DesktopEntryIndex` + `PowerControl` | 4 botones de configuración | las 4 configuraciones (KDE / kdock / tilemenu / previews), reinicios (dock, previews, tilemenu, este panel) y la fila de sesión (bloquear, cerrar sesión, suspender, reiniciar, apagar) |
| **Play** | `MprisControl` (nuevo) | carátula + título + play/pausa | carátula, título/artista/álbum, barra de progreso, anterior/play-pausa/stop/siguiente y selector cuando hay varios reproductores |
| **Red** | `NetworkControl` (tal cual) | conexión activa + ícono de señal | conexiones guardadas, redes cercanas, activar/desactivar, olvidar, Wi-Fi on/off |
| **Wallpaper** | `WallpaperControl` + script | botón "siguiente" del monitor actual | un botón por monitor conectado + "todos los monitores" (corre el script configurable, por defecto `/usr/local/bin/next-wall.sh`) |

Se agrega además una tarjeta **Reloj** (solo tarjeta, sin solapa): la hora grande del mockup,
que es lo que le da cara a la solapa Principal.

### Backends nuevos

**`ScreenBrightness`** — `org.kde.ScreenBrightness` en el bus de sesión (PowerDevil).
Verificado en esta máquina: `DisplaysDBusNames` devuelve `display11`/`display12`, y cada
objeto expone `Label` ("Samsung Electric Company S22D300"), `Brightness`, `MaxBrightness`
(10000) e `IsInternal`, más `SetBrightness(iu)` y las señales `BrightnessChanged`,
`DisplayAdded`, `DisplayRemoved`. Expone a QML una `QVariantList` de
`{path, label, internal, value (0..1), max}` y `Q_INVOKABLE setBrightness(path, ratio)`.
Si el servicio no está, cae al `BrightnessControl` de siempre (un solo slider,
`brightnessctl`).

**`MprisControl`** — enumera `org.mpris.MediaPlayer2.*` del bus de sesión (hoy hay tres
corriendo: `edge`, `strawberry`, `plasma-browser-integration`), sigue `NameOwnerChanged` para
altas y bajas y `PropertiesChanged` de `org.mpris.MediaPlayer2.Player` para el estado. Expone
título/artista/álbum/carátula/estado/posición/duración del reproductor **activo** (preferido:
el que está `Playing`), la lista para cambiar de uno a otro, y
`playPause/next/previous/stop/seek`. Todo `a{sv}` y `Metadata` se demarshalan con
`QDBusArgument` **const** (ver trampas).

**`DockLink`** — cliente de `org.kdock.Dock`: `darkMode` (property + señal), `setDarkMode()`,
`openSettings(dockId)`, `restart(dockId)`, `docks()` (lista `{id, screen, primary}`). Si kdock
no está en el bus, la sección Sistema muestra los ítems en gris en vez de fallar.

---

## 5. Qué se toca en kdock

Poco, y casi todo es la convención de widget nuevo cuyo backend es **un lanzador de otro
binario** — el mismo caso que `tilemenu`, que se saltea `dockmanager.*` porque el objeto es
fino y lo crea el propio `DockWindow`.

1. **`src/controlmanagerlauncher.{h,cpp}`** — copia de `TileMenuLauncher`: `binaryPath()`
   (junto a kdock → `build/controlmanager/` → `$PATH`), `running()`, `toggle(screen)`,
   `openSettings()`, `preload()`/`setPreload()`, `startIfPreloading()`. Servicio
   `org.kdock.ControlManager`.
2. **`DockConfig`** — cinco claves con su `Q_PROPERTY`/getter/setter/señal, su línea en
   `load()` y, la que se olvida y **no da error**, la línea en **`knownWidgetTokens()`**
   (sin ella `reconcileWidgetOrder()` descarta el token y el widget no aparece nunca), más su
   entrada en `defaultWidgetLabel()` (**sin `tr()`**: esa tabla *es* capabase):
   - `showControlManager` (bool), `controlManagerIcon` (default `preferences-system`),
   - `controlManagerDisplay`: 0 solo ícono, 1 ícono + texto, 2 solo texto,
   - `controlManagerText`: cadena corta del usuario ("Máquina de Pruebas"); **vacía = reloj**,
   - `controlManagerFormat`: formato Qt del reloj (default `ddd d MMM  HH:mm`).
3. **`src/dockwindow.cpp`** — `m_cmLauncher = new ControlManagerLauncher(this)` en el ctor y
   `setContextProperty("cmLauncher", …)`, exactamente como `tileLauncher`.
4. **`qml/Dock.qml`** — `sectionVisible`, `componentFor`, `sectionTooltip`, `isBlock`
   (es bloque: maneja su propio mouse y su menú contextual) y el `Component`. El dibujo copia
   `clock2Comp`: una `Column`/`Row` con ícono y/o texto cuyo `implicitWidth/Height` hace crecer
   la sección. Menú contextual: *Configurar ControlManager…*, *Ventana permanente* (tilde),
   *Dock settings…*.
5. **`src/settingsdialog.cpp`** — `createControlManagerGroup()` en la solapa **Widgets**
   (mismo formato que el grupo del menú de mosaicos en la solapa Menu): casillero del widget,
   ícono, modo de visualización, texto/formato, precargar, estado del proceso y botón
   *Configurar…*. Un grupo y no una solapa nueva, por consistencia con los otros dos
   accesorios (la barra ya es vertical, así que una solapa entraría — pero no aporta).
6. **`src/main.cpp`** — `ControlManagerLauncher::startIfPreloading()` junto al del menú de
   mosaicos.
7. **`src/configarchive.cpp`** — agregar `controlmanager*.conf` a la lista de globs, o la
   disposición no entra al backup.

### `src/dockservice.{h,cpp}` — `org.kdock.Dock` (nuevo)

Objeto `/Dock`, registrado en `main.cpp` después del `DockManager`:

- `openSettings(QString dockId)` — vacío = el dock primario. Llama a
  `DockWindow::openSettings()` del dock que corresponda.
- `restart(QString dockId)` — `DockWindow::restart()` (relanza el proceso conservando los
  argumentos de la CLI). Ojo: kdock es **un** proceso con N docks, así que reiniciar es
  reiniciarlos a todos; el ítem de la UI lo tiene que decir.
- `setDarkMode(bool)` / `darkMode()` / señal `darkModeChanged(bool)` — sobre
  `DockConfig::setDarkModeActive()`, que ya resuelve alcance app-wide vs. por dock.
- `docks()` — `a(ssb)` con `{id, screen, primary}` para poblar los menús de Sistema.

### Atajo global de modo oscuro (`src/globalshortcut.{h,cpp}`, nuevo)

`KWinShortcut` solo **dispara** atajos ajenos; registrar uno propio es otra cosa. Se hace por
D-Bus crudo contra `org.kde.kglobalaccel` (nada de KDE Frameworks, como manda la convención):
`doRegister` con `["kdock", "toggle-dark-mode", "kdock", "Alternar modo oscuro"]`,
`setShortcutKeys` con **la lista vacía** (sin combinación por defecto: aparece en
*Preferencias del sistema → Atajos* para que el usuario le asigne una, en vez de robarle una
tecla), y `connect` a `globalShortcutPressed(ss x)` del objeto `/component/kdock`. Verificado
que la interfaz existe y expone esa señal.

---

## 6. Panel de configuración propio (`CmSettingsDialog`)

`QDialog` con `QScrollArea` + `QDialogButtonBox::Close`, clon estructural de
`TileSettingsDialog`, con estos grupos:

- **Ventana**: borde, alineación, ancho/alto (px o % de pantalla), margen, ventana permanente,
  cerrar al salir el puntero, precargar con kdock.
- **Apariencia**: fondo (tema / color propio / imagen), opacidad, radio de esquina, negritas,
  posición de las solapas, íconos en las solapas.
- **Grilla**: columnas, tamaño de celda, estirar, mín/máx, separación.
- **Secciones**: lista con dos casilleros por fila — *habilitada* (tiene solapa) y *en
  Principal* (tiene tarjeta) — más subir/bajar para el orden de las solapas.
  **Trampa conocida**: `Qt::WA_TransparentForMouseEvents` sobre la fila deja muertos los
  casilleros hijos; si la fila necesita ser clickeable *y* contener controles, que reporte su
  clic desde `mouseReleaseEvent` y los hijos sean hijos normales.
- **Disposición**: restablecer Principal, exportar/importar el JSON.
- **Wallpaper**: ruta del script de "todos los monitores" (con *Examinar*), default
  `/usr/local/bin/next-wall.sh`.

Los tres detalles que ya se pagaron caros y hay que copiar: el `eventFilter` que le saca la
rueda a spinboxes y combos sin foco (si no, scrollear el panel cambia las opciones que pasan
por debajo), el `setText(tr("Close"))` explícito sobre el botón estándar (o sale en el locale
del sistema y no en el idioma elegido), y **no elegir widgets por índice** en las sondas
(`findChildren` recorre el árbol de padres, no el orden de construcción).

---

## 7. Traducciones

- Agregar `controlmanager/src` y `controlmanager/qml` a `SCAN_DIRS` y `QML_DIRS` de
  `tools/gen-capabase.py`, y correr después `gen-capabase.py` + `sync-translations.py`.
- Los cuatro `.md` base al qrc del binario con `BASE ".."`, igual que en `tilemenu/`.
- `Translations translations(Translations::BaseOnly)` **primero** en `main()`, antes de
  cualquier otro objeto: instala el `QTranslator`, y lo construido antes se queda en capabase.
- El merge por clave de `mergeSection()` ya hace que un `.md` de usuario viejo se ponga al día
  solo con las claves nuevas — o sea que las cadenas de ControlManager le van a aparecer al
  usuario sin borrar nada suyo.
- En los arneses, `rm -rf $XDG_DATA_HOME/kdock/translations` entre corridas o seguís leyendo
  el catálogo que sembró la corrida anterior.

---

## 8. Build

`add_subdirectory(controlmanager)` al final del `CMakeLists.txt` raíz. El target necesita,
además de lo de `tilemenu/`:

```cmake
qt_generate_wayland_protocol_client_sources(kdock-controlmanager FILES
    ../protocols/xdg-shell.xml                    # zwlr_layer_surface_v1.get_popup lo referencia
    ../protocols/wlr-layer-shell-unstable-v1.xml)
target_compile_definitions(kdock-controlmanager PRIVATE QT_STATICPLUGIN)
target_link_libraries(… Qt6::CorePrivate Qt6::WaylandClient Qt6::WaylandClientPrivate …)
```

y **tres** `qt_add_resources`: el QML propio (incluido `qml/cards/*.qml` — un `.qml` que no
está en la lista simplemente no entra al qrc), el `IconMenuItem.qml` de kdock con `BASE ".."`
(cae en `:/qml/` y se usa como tipo vecino sin import), y las cuatro traducciones base.
El `.desktop` es solo para el nombre y el ícono en el gestor de tareas: **no necesita
refrescar ksycoca**.

---

## 9. Verificación

**Antes de instalar, siempre**: `qmllint controlmanager/qml/*.qml controlmanager/qml/cards/*.qml`
(no ve errores de scope de ids, por eso no reemplaza al arnés) y una corrida del arnés.

**Arnés de Xvfb** — prueba que el QML carga y que las tarjetas dibujan:

```bash
rm -rf /tmp/cm && mkdir -p /tmp/cm/kdock
ln -s ~/.local/share/applications /tmp/cm/applications
ln -s ~/.local/share/icons        /tmp/cm/icons
printf '[General]\nkeepOpen=true\n' > /tmp/cm/kdock/controlmanager.conf
xvfb-run -a -s "-screen 0 1920x1080x24" dbus-run-session -- bash -c '
  XDG_DATA_HOME=/tmp/cm QT_QPA_PLATFORM=xcb QT_QUICK_BACKEND=software PATH=/tmp/fakebin:$PATH \
      ./build/controlmanager/kdock-controlmanager --show > /tmp/cm.log 2>&1 &
  APP=$!; sleep 8; import -window root /tmp/cm.png; kill $APP'
```

Cinco cosas de este arnés, todas heredadas de trampas ya pagadas:

- **`dbus-run-session` es obligatorio.** El candado de instancia única es el nombre en el bus,
  y `xvfb-run` hereda el bus del usuario: sin eso el `--show` se le reenvía a la instancia real
  y **le abre el panel en la pantalla del usuario**, mientras la corrida sale vacía.
- **Bajo X no hay layer-shell**: la ventana es un X normal, así que el arnés **no** prueba
  anclaje, alineación ni margen. Silencio ahí prueba que el QML carga, nada más.
- **El `PATH` falso es obligatorio acá**, más que en cualquier otro arnés del proyecto: este
  binario maneja brillo, audio y modo oscuro. Sin los scripts falsos de `/tmp/fakebin`
  (receta de `CLAUDE.md`) una corrida **le cambia el brillo y el volumen al usuario**. Para el
  brillo por D-Bus la reja es distinta: `ScreenBrightness` solo escribe desde un clic, así que
  la sonda no toca nada mientras no se le mande uno.
- **La captura es de la raíz de X** (`import -window root`), no de la `QQuickView`: los menús
  son `Popup.Window` y viven en otra ventana.
- **El proceso tiene que seguir vivo** cuando sacás la captura, o el PNG sale gris y parece
  que no renderizó.

**Prueba en vivo del layer-shell** (lo único que valida anclaje y tamaño): segunda instancia
en la sesión Wayland real, con la instancia del usuario apagada (`quit` por D-Bus) o antes de
instalar, `XDG_DATA_HOME` descartable, y captura con `spectacle-custom -b -n -f -o /tmp/x.png`
(el `spectacle` de `/usr/bin` no está autorizado en esta máquina). Matar por PID de
`pgrep -x kdock-controlmanager`, nunca con `pkill -f`, que también mata el shell del comando.

**Antes de escribir cualquier demarshalling de D-Bus, `busctl` primero.** Costó un proceso
abortado la última vez: `qdbus6` imprime `{D-Bus type "a(iss)"}` y se queda ahí, `busctl`
escupe el contenido **y la firma de verdad**. Y el `QDBusArgument` local va **`const`**, o el
compilador elige las sobrecargas de *escritura* de `beginArray()`/`beginStructure()`, la
lectura se desincroniza y libdbus **aborta el proceso**. Aplica a `Metadata` de MPRIS
(`a{sv}`) y a las properties de `ScreenBrightness`.

**Sonda del diálogo linkeando los `.o`** (para la solapa Secciones y el editor de disposición),
igual que la de `SettingsDialog`: `find build/CMakeFiles/kdock-controlmanager.dir -name '*.o'`
menos `main.cpp.o`, un `dlg.cpp` que instancie `CmConfig` + `CmLayout`, maneje los widgets con
`findChildren` + `->click()` en el orden del usuario e imprima lo que quedó en la config.
**Relinkear la sonda después de cada `cmake --build`** o probás la versión vieja.

**`--dump-sections`** después de cada cambio del motor: dice si la tarjeta se movió de verdad
y si quedó guardada, que es justo lo que la captura no muestra. Y **`rm -rf` del
`XDG_DATA_HOME` entre corridas**, o la siguiente arranca con la disposición de la anterior.

**Modo oscuro de punta a punta**: sonda que llama `org.kdock.Dock setDarkMode true` sobre el
kdock real y captura el dock del usuario. Es el único camino que prueba que el repintado en
vivo funciona; el `.conf` se ve idéntico en los dos casos.

---

## 10. Trampas del proyecto que aplican directamente

- **`Component.onCompleted` se declara UNA sola vez por objeto.** Un segundo handler en la
  raíz no es error de compilación ni advertencia de `qmllint`: es *"Property value set
  multiple times"* y **el QML entero no carga**.
- **QML no concatena literales adyacentes como C++**: un `qsTr("parte "` + salto de línea +
  `"resto")` es error de sintaxis y tira la carga del archivo. `qmllint` no lo reporta.
- **Un `Menu` no se dimensiona a su ítem más largo**: `width: Math.max(implicitWidth + 64, N)`
  en cada menú. Los 64 px están medidos — con 16 el español seguía cortando la última letra.
- **Nunca declarar `Menu` ni `ToolTip` dentro de un delegate**: con 8 tarjetas no duele, pero
  la regla es del proyecto (450 mosaicos costaron 783 MB y 3,5 s). El `ToolTip` va **adjunto**.
- **Popups y menús siempre `popupType: Popup.Window`**, o quedan recortados dentro de la
  superficie en Wayland. Y ojo con el costo: cada `Popup.Window` es un `QSGRenderThread`
  (ver el perfil de memoria en `PLAN-PERF-1.md`) — un menú compartido para todo el lienzo, no
  uno por tarjeta.
- **`QSettings` mapea `[General]` al nivel raíz** (`value("General/x")` no lee nada, sin
  avisar) y **solo el primer `/` es sección**: para claves con datos adentro (un conector, una
  ruta), una sección por valor o JSON en una sola clave.
- **`QIcon::setThemeName()` no se toca fuera de `Theme`**: re-entra al `IconProvider` y se
  filtra a íconos que no lo pidieron.
- **`QUrl` percent-codifica las llaves**: nunca armar una URL con un uuid de KWin.
- **Un hijo `QProcess` de larga vida va con `kdock::tieToParent()`** (`src/childprocess.h`).
  Acá aplica si alguna tarjeta lanza algo persistente; el `pactl subscribe` de `AudioControl`
  ya lo trae resuelto.
- **El grosor del dock**: el widget nuevo dibuja texto, así que sigue el camino de `clock2`
  (crece por `implicitWidth`). Igual hay que **medir** el grosor de un dock vertical antes y
  después de prenderlo bajo Xvfb; si aparece cualquier opción de peso o tamaño de fuente para
  ese texto, va en las **cuatro** puntas de la medición (`appNameProbe`, `secNameProbe`,
  `labelMetrics`, `secLabelMetrics`).

---

## 11. Orden de trabajo

Cada fase termina en algo que se puede *ver* o *volcar*, que es la única forma de no acumular
deuda invisible.

1. **Esqueleto** — `CMakeLists.txt`, `main.cpp`, `CmConfig`, `CmWindow` layer-shell,
   `ControlManager.qml` con fondo + barra de solapas, y dos tarjetas triviales (Reloj y Modo
   oscuro, esta última todavía sin backend). *Checkpoint*: captura bajo Xvfb + prueba en vivo
   del anclaje en la sesión real.
2. **Motor de grilla** — `CmSections`, `CmLayout`, `CmModel`, `CmCard.qml` con arrastre,
   `--dump-sections`. *Checkpoint*: arrastrar con `xdotool` y confirmar con el volcado que
   quedó guardado.
3. **Widget en el dock** — `ControlManagerLauncher`, las cinco claves de `DockConfig` (con su
   línea en `knownWidgetTokens()`), el `Component` de `Dock.qml`, el grupo del diálogo, el
   preload y el backup. *Checkpoint*: arnés del dock — el widget aparece, el clic abre el
   panel, el clic derecho abre su configuración.
4. **`org.kdock.Dock` + atajo global** — `DockService`, `GlobalShortcut`, `DockLink`.
   *Checkpoint*: la sonda prende el modo oscuro en el kdock real y la captura lo muestra.
5. **Secciones con backend existente** — Audio (con los tres `Q_INVOKABLE` nuevos de
   `AudioControl`), Red, Wallpaper, Sistema, Calendario.
6. **Backends nuevos** — `ScreenBrightness` y `MprisControl`, y con ellos Video/Energía y Play.
   *Checkpoint*: `busctl` primero, sonda de 30 líneas después, recién ahí el código.
7. **Panel de configuración** — `CmSettingsDialog` completo. *Checkpoint*: la sonda que linkea
   los `.o` lo maneja desde código y verifica el `.conf`.
8. **Cierre** — regenerar el catálogo (`gen-capabase.py` + `sync-translations.py`), documentar
   en `AGENTS.md` (sección *Control Manager* + tabla QML↔C++) y en `CLAUDE.md` (el binario
   nuevo, su arnés y las trampas que aparezcan), y recién entonces `install` con backup previo.
   **Reiniciarle el dock al usuario es decisión suya: preguntar antes.**
