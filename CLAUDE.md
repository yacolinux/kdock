# kdock — instrucciones para Claude Code

**Leé `AGENTS.md` antes de tocar código.** Es el documento de contexto del proyecto
(arquitectura, cada widget, trampas de Wayland/layer-shell, tabla QML↔C++) y se mantiene
al día con cada feature; este archivo solo tiene lo que hay que saber sí o sí.

## Build

```bash
env -u CC -u CXX cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/kdock
```

`env -u CC -u CXX` es obligatorio: las variables apuntan a ccache y CMake falla si están
seteadas.

**Son cuatro binarios**, los cuatro los compila el mismo `cmake --build build`:

- `kdock`;
- `kdock-previews` (`build/previews/kdock-previews`, árbol `previews/`): tiras de vista previa
  de ventanas, se prende desde *Configuración → Previews*. Detalles en `AGENTS.md` →
  *Dock Preview*;
- `kdock-tilemenu` (`build/tilemenu/kdock-tilemenu`, árbol `tilemenu/`): el menú de
  aplicaciones a pantalla completa, lo prende el widget `tilemenu` del dock. Detalles en
  `AGENTS.md` → *Menú de mosaicos*.
- `kdock-calendar` (`build/calendar/kdock-calendar`, árbol `calendar/`): calendario de mes
  standalone, autocontenido (Qt Widgets, sin archivos de `src/`). Se lanza desde el widget
  del reloj (o un Script Runner); el reloj no se toca. Detalles en `AGENTS.md` → *Calendario*.

**`kdock-tilemenu` es el binario fácil de desarrollar, y conviene saber por qué**: su ventana
es un **toplevel normal maximizado**, no una superficie layer-shell, y **no pide ningún
privilegio de KWin**. O sea que —a diferencia de `kdock` y de `kdock-previews`— se corre
directo desde `build/`, sin `.desktop` temporal, sin tocar ksycoca, y **anda bajo Xvfb como
una ventana X cualquiera**, así que se le puede sacar una captura de la UI entera en cada
iteración:

```bash
./build/tilemenu/kdock-tilemenu --show      # en la sesión real, sin instalar nada
./build/tilemenu/kdock-tilemenu --dump-layout "cat:Internet"   # la grilla en ASCII, sin GUI
```

**Instalación** (el dock del usuario corre desde `/usr/local/bin/kdock`): backup primero
—binario + `.desktop` a `/usr/local/share/kdock-backup/<timestamp>/` y los `.conf` a
`~/.local/share/kdock/backup-<timestamp>/`, mismo formato que usa la solapa Backup— y
después `sudo cmake --install build`. No hace falta parar el proceso: el binario viejo
sigue mapeado en memoria y el install lo reemplaza sin `ETXTBSY`. Sí hay que reiniciarlo
para que tome el nuevo (su menú *Dock → Reiniciar* se re-lanza solo); reiniciarle el dock
al usuario sin avisar deja la pantalla sin dock, así que preguntá antes.

Los dos accesorios se reinician distinto y sin riesgo de dejar la pantalla pelada: cada uno
sale por D-Bus (`org.kdock.Previews` / `org.kdock.TileMenu`, método `quit`) y vuelve a
levantarse solo — el de previews al prender su casilla, el del menú de mosaicos en el próximo
clic del widget. O sea que actualizar `kdock-tilemenu` es `install` + matarlo; no hace falta
tocar el dock.

El install escribe **ocho** cosas: los cuatro binarios y sus cuatro `.desktop`. Después de
instalar hay que refrescar ksycoca, pero **solo por los dos primeros**: KWin busca ahí los
`.desktop` para conceder los privilegios (sin eso `kdock-previews` se queda sin capturas y las
tarjetas caen a ícono). **`kdock-tilemenu` y `kdock-calendar` no necesitan el refresco** —no
piden ningún privilegio, y su `.desktop` es solo para el nombre y el ícono del gestor de
tareas—, así que si lo único que tocaste fue uno de esos dos, saltealo y evitás el riesgo de
abajo. Y ojo:
**`kbuildsycoca6` a secas es peligroso en esta máquina** (ver abajo):

```bash
env XDG_DATA_DIRS="/opt/kde/share:$XDG_DATA_DIRS" /opt/kde/bin/kbuildsycoca6
```

**Por qué la ruta explícita.** Esta sesión mezcla dos KDE: Plasma/KWin salen de un build
propio en `/opt/kde` (KF6 **6.28**) y las apps de Ubuntu (Dolphin, Okular, Krusader,
`spectacle-custom`, el `kioworker` de KIO) usan las libs del sistema (KF6 **6.24**). El
formato de ksycoca cambió entre las dos: 6.28 escribe versión `0x133`, 6.24 escribe `0x132`.
Una base `0x133` leída por una app 6.24 **no da error**: devuelve cero ofertas para parte de
los mimetypes (`image/png`, `inode/directory`…), o sea *"KDE se olvidó de con qué abrir los
archivos"*. Y el shell de trabajo tiene `/opt/kde/bin` primero en `PATH`, así que
`kbuildsycoca6` pelado = el de 6.28.

Encima, el shell corre con el entorno *scrubbed* (`kde-usr-scrub`, sin `/opt/kde` en
`XDG_DATA_DIRS`), y el nombre del archivo de ksycoca es un hash de esos directorios: sin el
`env XDG_DATA_DIRS=…` de arriba, el comando **no toca la base que lee KWin** y en cambio
pisa la de las apps de Ubuntu. Es exactamente lo que rompió los "abrir con" el 2026-07-30.

Si ya pasó, se repara con el builder que corresponde a esas apps:

```bash
/usr/bin/kbuildsycoca6            # reescribe la base 0x132 de las apps 6.24
```

**Antes de instalar, `qmllint` Y el arnés de Xvfb.** El build **no** valida el QML (solo lo
empaqueta con rcc), así que un error recién aparece cuando el dock arranca — y si ya lo
instalaste, al usuario le queda la pantalla sin dock al reiniciar:

```bash
qmllint qml/*.qml previews/qml/*.qml 2>&1 | grep -iE "error|syntax"
```

(Las advertencias de *unqualified access* son esperables: todo el QML usa las context
properties `config`/`theme`/`dockModel`.)

**`qmllint` no reemplaza al arnés**: es análisis estático y **no ve los errores de scope de
ids**. Un `ReferenceError: dockRepeater is not defined` pasó el lint, se instaló, y dejó una
feature entera muerta en silencio (2026-07-30). Correr el arnés de abajo es lo único que
prueba que el QML se ejecuta.

**Los ids de QML no salen de su component scope.** Un id declarado dentro de un `delegate`
(p. ej. `dockRepeater`, que vive en el delegate de `sectionRepeater`) **no se ve desde la raíz**
— al revés sí: desde un delegate se ven los ids de los componentes que lo contienen. Si desde
la raíz necesitás datos que viven en un delegate, pedilos al modelo (`dockModel.labelStrings()`)
en vez de al `Repeater`.

**Verificación rápida sin molestar al dock en pantalla:**

```bash
rm -rf /tmp/kdock-t && mkdir -p /tmp/kdock-t/kdock
cp <config-de-prueba>.conf /tmp/kdock-t/kdock/kdock.conf
cp /tmp/kdock-t/kdock/kdock.conf /tmp/kdock-t/kdock/kdock-screen.conf   # ver abajo
timeout 7 xvfb-run -s "-screen 0 1920x1080x24" \
    env XDG_DATA_HOME=/tmp/kdock-t QT_QPA_PLATFORM=xcb ./build/kdock 2>&1 \
  | grep -vE "Failed to get image|QDBusArgument|^kdock: systray"
```

Esto **sí** carga el QML: los errores de QML y los *binding loops* salen por stderr y
silencio = limpio. `XDG_DATA_HOME` apunta la config a un directorio descartable, así que se
prueba cualquier combinación de opciones sin tocar la del usuario.

Ojo con **encadenar `timeout` a un pipe**: cuando llega el SIGTERM la salida se puede perder
en el `head`/`grep` y ves una corrida vacía que parece limpia (me pasó, 2026-07-30).
Redirigí a archivo (`> /tmp/x.log 2>&1`) y leelo después.

Dos trampas del arnés, ambas mordieron (2026-07-29):

- **`QT_QPA_PLATFORM=offscreen` no alcanza**: el monitor no tiene nombre, `migrateFirstRun()`
  corta por `primary.isEmpty()` y **no se crea ningún dock**, así que el QML nunca se
  instancia y la corrida sale limpia *diga lo que diga el código*. Bajo Xvfb la pantalla se
  llama `screen`, hay dock y hay QML. (Si igual querés `offscreen`, agregá una línea
  `enabledScreens=` vacía: QSettings la devuelve como `[""]`, que es justo el id de la
  pantalla sin nombre.)
- **La copia por pantalla gana**: en el primer arranque `migrateFirstRun()` copia
  `kdock.conf` a `kdock-screen.conf` y a partir de ahí lee **esa**. Si editás solo
  `kdock.conf` entre corridas, el cambio no se ve: escribí las dos, o borrá el directorio.

El tamaño de la pantalla importa para todo lo que dependa del largo del dock (auto-shrink,
springs): Xvfb a 1920x1080 da números realistas. Ojo que bajo X11 no hay layer-shell, así
que en `panelMode` la ventana no se estira sola — para probar largo real usá
`dockLength=100`, que sí sale de `Screen.width/height`.

**Verificá que el arnés no te esté mintiendo.** Silencio también es lo que ves cuando el QML
nunca se instanció (ver las dos trampas de arriba). Control positivo: rompé el `.qml` a
propósito (`Item { property int x: noExiste.nada }`), recompilá, corré, confirmá que aparece
el error, y revertí. Salió gratis y confirmó el arnés (2026-07-30).

**Y podés *ver* el dock renderizado** —el control positivo más barato, y lo único que prueba
que algo se dibuja donde va— capturando la ventana bajo Xvfb, sin tocar el dock del usuario
(2026-08-02, con esto se cerró la feature de separadores):

```bash
XDG_DATA_HOME=/tmp/kdock-t QT_QPA_PLATFORM=xcb QT_QUICK_BACKEND=software ./build/kdock & sleep 6
import -window $(xwininfo -root -children | awk '/"kdock": \("kdock"/ {print $1}') /tmp/dock.png
```

Tres detalles, los tres dan un PNG **negro** que parece "no renderiza":

- **`QT_QUICK_BACKEND=software`**: sin eso el contenido GL no aparece en la captura de X.
- **`autohide=false` en el `.conf`**: si no, el dock se esconde solo y capturás la franja vacía.
- **`import -window <id>`** (no `-window root`, que también sale negro). El id sale del
  `xwininfo`; ojo que hay tres ventanas "kdock" y la buena es la de `("kdock" "kdock")`.

Para un dock vertical (`edge=2/3`) la imagen sale alta y flaca: `convert … -rotate 90` para
leerla de un vistazo.

Bajo Xvfb el dock es una ventana X normal y su tamaño lo dicta el contenido
(`SizeViewToRootObject`), así que se puede medir el grosor sin GUI:

```bash
xvfb-run -s "-screen 0 1920x1080x24" bash -c '
  XDG_DATA_HOME=/tmp/kd-t QT_QPA_PLATFORM=xcb ./build/kdock & sleep 5
  xwininfo -root -children | grep -i kdock; kill %1'
```

**Arnés de `kdock-tilemenu`**: es el más barato de los tres, porque su ventana es un toplevel
normal. La captura sale de la raíz de X y se puede **manejar con `xdotool`**, así que el
arrastre se prueba de verdad, de punta a punta:

```bash
rm -rf /tmp/tm && mkdir -p /tmp/tm/kdock
ln -s ~/.local/share/applications      /tmp/tm/applications
ln -s ~/.local/share/desktop-directories /tmp/tm/desktop-directories
ln -s ~/.local/share/icons             /tmp/tm/icons
printf '[General]\nlastSection=cat:Development\n' > /tmp/tm/kdock/tilemenu.conf
xvfb-run -a -s "-screen 0 1920x1080x24" bash -c '
  XDG_DATA_HOME=/tmp/tm QT_QPA_PLATFORM=xcb QT_QUICK_BACKEND=software \
      ./build/tilemenu/kdock-tilemenu --show > /tmp/tm.log 2>&1 &
  APP=$!; sleep 8
  xdotool mousemove 310 168 mousedown 1
  for x in 340 400 450 500; do xdotool mousemove $x $((168 + x - 310)); sleep 0.08; done
  xdotool mousemove 475 500; sleep 0.4
  import -window root /tmp/ghost.png     # el fantasma del destino
  xdotool mouseup 1; sleep 1
  import -window root /tmp/after.png
  kill $APP'
XDG_DATA_HOME=/tmp/tm ./build/tilemenu/kdock-tilemenu --dump-layout "cat:Development"
```

Cinco cosas de este arnés:

- **Envolvelo en `dbus-run-session` o no estás probando tu build.** El candado de instancia
  única es el nombre en el bus de sesión, y `xvfb-run` **hereda el bus del usuario**: si la
  instancia real está corriendo, tu `--show` se le reenvía a *ella* y el proceso del build sale
  enseguida (`kill` falla con *"No existe el proceso"* y la captura sale vacía). Peor: le abre
  el menú **en la pantalla del usuario**. Me pasó el 2026-08-03. Lo mismo vale para el arnés
  del **dock** en cuanto vayas a hacerle clic al widget `tilemenu`, porque el clic termina en
  el mismo D-Bus.

- **Los tres enlaces son obligatorios.** `XDG_DATA_HOME` hay que aislarlo (si no, el arnés le
  escribe la config real), pero entonces no se ven ni los `.desktop`, ni los `.directory`, ni
  los íconos: el menú sale **vacío y parece un bug del código**.
- **`xdotool` sirve para el mouse pero no para el teclado.** Bajo Xvfb no hay gestor de
  ventanas, así que no hay foco: `windowactivate` falla y `type` no llega al buscador (se ve el
  placeholder intacto y parece que la búsqueda está rota). Los clics y los arrastres van por
  XTEST y sí llegan. Para probar la búsqueda, usá el modelo desde una sonda de consola
  (`TileModel::setQuery()`), no la UI.
- **`xdotool search --name` devuelve varias ventanas** (hay una auxiliar de 1x1); elegí por
  geometría (`getwindowgeometry --shell`, la de ancho > 1000) o le mandás los eventos a la que
  no es.
- **El `--dump-layout` después del arrastre es la prueba**: dice si el mosaico se movió de
  verdad y si quedó guardado, que es lo que la captura no muestra.

**Arnés de `kdock-previews`**: igual, pero la config es `previews.conf` + `previews-<screen>.conf`
y hace falta `enabled=true` en la compartida (el switch maestro), o no se crea ninguna tira
y la corrida sale limpia sin haber cargado el QML. Las capturas **no** se pueden probar así
(son un protocolo de Wayland + KWin): para eso está `--dump-captures`, abajo.

**El diálogo de Configuración es Qt Widgets, no QML** (`SettingsDialog`, `QTabWidget` con
una solapa por `create*Tab()`), y **no hay forma de abrirlo desde la CLI**: sale del menú
del dock. Así que ninguno de los arneses de arriba lo ejercita. Para ver un widget propio
del diálogo sin reiniciarle el dock al usuario, compilá **solo esa clase** contra Qt y
sacale una captura bajo Xvfb — no hace falta CMake ni el resto del proyecto:

```bash
/usr/lib/qt6/libexec/moc -I src src/coloredtabbar.h -o /tmp/p/moc_coloredtabbar.cpp
g++ -std=c++17 -fPIC /tmp/p/main.cpp /tmp/p/moc_*.cpp src/coloredtabbar.cpp -Isrc \
    $(pkg-config --cflags --libs Qt6Widgets) -o /tmp/p/probe
timeout 30 xvfb-run -s "-screen 0 1000x400x24" env QT_QPA_PLATFORM=xcb /tmp/p/probe
```

(Para un helper sin `QObject` —`xdgmenutree.cpp`, por ejemplo— es más corto todavía: no hace
falta `moc` ni GUI, alcanza con `g++ main.cpp src/xdgmenutree.cpp $(pkg-config --cflags --libs Qt6Core)`
y un `main` que imprima el resultado.)

**El `kdeglobals` de esta sesión NO es `~/.config/kdeglobals`.** `XDG_CONFIG_HOME` apunta a
`~/.kde-opt/config`, así que ese es el archivo que leen `Theme` y `AppearanceControl` (y el que
escriben `plasma-changeicons`/`plasma-apply-colorscheme`). El de `~/.config` existe, tiene otro
valor y **no lo usa nadie**: mirarlo para verificar un cambio de tema dice que "no pasó nada"
cuando sí pasó, y al revés (me pasó 2026-08-05, y por un rato creí que no le había tocado el
escritorio al usuario cuando sí). Confirmalo con
`QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)` desde una sonda, no de
memoria.

**`QSettings` mapea la sección `[General]` de un INI al nivel raíz**, así que
`value("General/ColorScheme")` **no lee nada** —sin error, sin advertencia— aunque el archivo
tenga la clave ahí. Es `value("ColorScheme")` a secas; las demás secciones sí se direccionan
normal (`Icons/Theme` anda). Costó caro: durante meses pareció que *"este `kdeglobals` no tiene
`General/ColorScheme`"* y hasta quedó documentado así, cuando el archivo la tenía y el picker
mostraba "(sin definir)" para siempre (2026-08-05). El mismo error estaba en el `Name=` de los
`.colors`, enmascarado por un fallback al nombre de archivo: la lista mostraba "BreezeDark" en
vez de "Breeze Dark" y parecía que los esquemas se llamaban así.
Igual **poné siempre una entrada "(no cambiar)" con id vacío al frente** de un combo de esquema:
un `kdeglobals` puede no tener la clave de verdad (escritorios cuyo esquema vino de un
look-and-feel), y ahí el combo cae en el primer ítem por orden alfabético y aplicarlo le cambia
el esquema al usuario sin que lo pida.
Para verificarlo, dos líneas de sonda que impriman `value()` por los dos caminos valen más que
leer el `.conf`: el archivo se ve perfecto en ambos casos.

El `main.cpp` de prueba instancia el widget, y con un `QTimer::singleShot` hace
`w.grab().save("/tmp/p/out.png")` y sale; el PNG se lee directo. Vale la pena correrlo dos
veces, una con `app.setPalette()` claro y otra oscuro: el diálogo hereda el esquema de KDE
(`Theme::applyAppPalette`) y un color que se lee bien en uno puede desaparecer en el otro.
Encontró el bug de que las solapas coloreadas se pasaban de los 1000 px del diálogo (las
diez ya piden ~940) y la barra caía en modo flechas de scroll.

**La barra de solapas ya no se desborda por el ancho: está en columna** (`QTabWidget::West`,
puesto en el ctor de `ColoredTabWidget`, 2026-08-04). Antes cada solapa nueva costaba ~90 px
de ancho y a la undécima el `QTabBar` caía en modo flechas **sin avisar** (1086 px de títulos
contra 1000 de diálogo); ahora cuesta ~30 px de alto y entran ~28. Dos cosas que hay que
saber igual:

- **Si tocás el pintado, acordate de que en vertical el label lo dibujamos nosotros.** Qt rota
  la etiqueta 90° para West/East, así que volver a `CE_TabBarTabLabel` reintroduce el
  desborde, solo que en el otro eje.
- **Antes de agregar una solapa, seguí midiendo**, ahora el alto: un `main` de diez líneas que
  arma un `ColoredTabWidget` con los títulos e imprime `coloredTabBar()->sizeHint()` (no hace
  falta ni GUI ni captura). Con doce títulos da 142×480.

### Sonda del diálogo *real* (linkeando los `.o` del proyecto)

Compilar una clase suelta sirve para un widget propio, pero no para algo que vive dentro de
`SettingsDialog` (una solapa nueva): recrearla en la sonda prueba una copia, no el código que
se instala. **No hace falta CMake: alcanza con linkear los objetos que el build ya dejó**,
salteando `main.cpp.o`:

```bash
OBJS=$(find build/CMakeFiles/kdock.dir -name '*.o' | grep -v '/main.cpp.o' | tr '\n' ' ')
g++ -std=c++17 -fPIC /tmp/p/dlg.cpp $OBJS -Isrc \
    $(pkg-config --cflags --libs Qt6Widgets Qt6Quick Qt6Qml Qt6DBus Qt6WaylandClient \
                                 Qt6Gui Qt6Core wayland-client) -o /tmp/p/dlgprobe
```

**Relinkeá la sonda después de cada `cmake --build`**: el `g++` de arriba no depende de nada,
así que si solo recompilás el proyecto la sonda sigue siendo la vieja y ves el bug que creías
recién arreglado (me pasó, 2026-08-02).

El `dlg.cpp` instancia `DockConfig` + `DesktopEntryIndex`, construye el `SettingsDialog` con
`nullptr` en el resto (manager, systray, audio…), selecciona la solapa con
`findChild<QTabWidget*>()->setCurrentIndex(n)` y hace `grab().save()`. Corré con
`XDG_DATA_HOME` a un directorio descartable, o **te escribe la config real**. Dos detalles del
link: `find` (no `ls`) para agarrar también los `.o` de los protocolos de Wayland y el
`mocs_compilation`, y `wayland-client` explícito en el `pkg-config` (si no, *"DSO missing from
command line"*).

Dos usos más de la misma sonda, los dos salieron gratis (2026-08-02, colores rápidos):

- **Probar una migración de config** sin esperar al reinicio del dock: el `dlg.cpp` imprime el
  valor con `qInfo()` apenas construye el `DockConfig`, así se ve exactamente qué leyó de
  cada archivo. Con una config de prueba que traiga el formato *viejo*, una corrida muestra la
  migración; una **segunda** corrida sobre el mismo `XDG_DATA_HOME` prueba que lo migrado
  persistió — es lo único que distingue "lo convirtió en memoria" de "lo guardó".
- **Verificar contra la config real del usuario, en modo lectura**: copiá
  `~/.local/share/kdock` a un `XDG_DATA_HOME` descartable y corré la sonda ahí. Ve los datos
  de verdad (todos los docks, sus claves viejas) y cualquier escritura cae en la copia. Es la
  forma de confirmar qué van a leer los docks instalados sin reiniciarle ninguno al usuario.
  El `dockId` va hardcodeado en el `dlg.cpp`, así que para mirar otro dock hay que recompilar.

Y un tercero, el que más rinde: **manejar el diálogo desde el código** en vez de mirarlo. La
sonda encuentra los widgets con `findChildren<QPushButton*>()` / `findChild<QTabWidget*>()`,
llama `->click()` / `setCurrentRow()` en el orden que haría el usuario e imprime lo que quedó
en el `DockConfig` después de cada paso. Es un test funcional de la solapa completa que corre
en dos segundos bajo Xvfb, y encontró en la primera corrida un bug que la captura no muestra:
una lista que se reconstruye con cada edición **pierde la selección**, así que el segundo clic
en *Bajar* no hacía nada (2026-08-02). Dos apuntes: no hace falta `app.exec()` si no vas a
capturar; y **no elijas el widget por índice**. `findChildren` recorre el *árbol* de padres,
no el orden en que los creaste: en el diálogo, `findChildren<ThemePickerButton*>()[0]` no es
el primero que se construyó (el de General) sino uno de DarkMode, que además está
**deshabilitado**, y `->click()` sobre un botón deshabilitado no hace nada — la sonda ve un
popup que nunca se creó y parece un bug del código (2026-08-05). Desambiguá por algo del
propio widget (el tooltip, la página que lo contiene) e imprimí `isEnabled()` antes de
concluir que el clic "no funcionó".

### Probar los efectos sobre el escritorio sin tocarlo: herramientas falsas en el `PATH`

`applyIconTheme()`/`applyColorScheme()` lanzan las herramientas de Plasma, que **le cambian la
apariencia a la sesión de verdad** — y aislar `XDG_CONFIG_HOME` no alcanza para el esquema de
color, porque `plasma-apply-colorscheme` además avisa por D-Bus. `AppearanceControl::findTool()`
busca **primero en el `PATH`**, así que un directorio propio al frente con dos scripts de una
línea que registren sus argumentos alcanza para probar el ciclo entero:

```bash
mkdir -p /tmp/fakebin
for t in plasma-apply-colorscheme plasma-changeicons kwriteconfig6; do
  printf '#!/bin/sh\necho "$(basename $0) <- $*" >> /tmp/fakebin/calls.log\n' > /tmp/fakebin/$t
  chmod +x /tmp/fakebin/$t
done
env -i HOME=$HOME PATH=/tmp/fakebin:/usr/bin:/bin LANG=C.UTF-8 \
    XDG_DATA_HOME=<copia-de-la-config> QT_QPA_PLATFORM=offscreen ./sonda
```

Prueba **la decisión** (qué id recibe cada herramienta en cada transición), que es justo lo que
suele estar mal, y sirve para correr el arnés **sobre la config real del usuario copiada** sin
riesgo. Con eso se cerró el bug de dark mode del 2026-08-05.

Y si en cambio dejás que las herramientas corran de verdad: **son `startDetached`**, o sea
asíncronas. Una sonda que lee `kdeglobals` inmediatamente después ve el valor **viejo** y parece
que la aplicación no hizo nada; hay que esperar (~900 ms) antes de leer.

### Arnés del portapapeles en la sesión real (klipper como control)

Bajo Xvfb **no hay data-control** (es X11), así que el backend nuevo del portapapeles solo se
puede probar en la sesión Wayland de verdad. Una sonda que linkee `waylandclipboard.cpp` (o
`clipboardhistory.cpp`) e imprima lo que captura alcanza, y **Klipper sirve de control desde la
CLI** en las dos direcciones:

```bash
qdbus6 org.kde.klipper /klipper setClipboardContents "kdock prueba"   # -> la sonda debe verlo
qdbus6 org.kde.klipper /klipper getClipboardContents                  # tras nuestro setText()
spectacle-custom -b -n -c                                             # copia una imagen
```

Lo que prueba de verdad: la sonda **no abre ninguna ventana ni toma foco**, así que si ve el
texto, la captura en segundo plano funciona (que es justo lo que no andaba). Con
`XDG_DATA_HOME` a un directorio descartable se ve además el JSON y los PNG que quedan en
disco. Para el caso de las contraseñas, una segunda sonda con `QMimeData` que traiga
`x-kde-passwordManagerHint` confirma que **no** se guarda.

### Arnés de los escritorios virtuales

**El arnés de Xvfb no ejercita nada de esto**: sin KWin, `VirtualDesktops::currentPosition()`
es 0 y `DockManager::wantedDocks()` devuelve el juego base, o sea el comportamiento de antes
de la feature. Silencio ahí prueba que **no rompiste lo viejo**, nada más.

Pero ojo con la otra mitad: un proceso bajo Xvfb **sí llega al bus de sesión del usuario**
(los escritorios virtuales salen de D-Bus, no de Wayland). O sea que una sonda del diálogo
corriendo en Xvfb ve los tres escritorios reales con sus nombres, y la lista de *Escritorios
virtuales* de la solapa Monitores se puede manejar desde código con `setCheckState()` y
verificar releyendo el `.conf`. Eso cubre la UI entera sin tocar la sesión.

Lo que **solo** se puede probar en la sesión real es el ciclo de vida (crear diferido,
ocultar, volver a mostrar con la superficie de layer-shell bien anclada). Segunda instancia
aislada, **en el bus real** (`dbus-run-session` acá **no sirve**: perdés la señal
`currentChanged` de KWin, que es todo el disparador), y el brillo tapado con el `PATH` falso
de más arriba porque `BrightnessControl` reaplica al arrancar:

```bash
setsid env PATH=/tmp/fakebin:$PATH XDG_DATA_HOME=/tmp/kdvd ./build/kdock > /tmp/kdvd/err.log 2>&1 &
busctl --user get-property org.kde.KWin /VirtualDesktopManager \
       org.kde.KWin.VirtualDesktopManager desktops     # los uuid, y anotá el actual
busctl --user set-property org.kde.KWin /VirtualDesktopManager \
       org.kde.KWin.VirtualDesktopManager current s "<uuid>"
grep VmRSS /proc/<pid>/status                          # sube una vez y se queda
```

Tres cosas de este arnés:

- **Poné `autohide=true` en los `.conf` de prueba** y no le movés ninguna ventana al usuario
  (zona exclusiva 0). El precio es que no se ve nada en una captura: el QML desliza el dock
  fuera de pantalla, así que para *ver* que la superficie vuelve bien hay que correr una
  segunda vuelta con `autohide=false` (ahí sí KWin re-acomoda las maximizadas, y vuelve solo
  al matar la instancia).
- **Distinguí los dos docks por `alignment`** (0 = principio del borde, 2 = final) o por
  `iconSize`: dos capturas del mismo borde en dos escritorios se leen de un vistazo, y una
  tercera de vuelta al primero prueba que el re-mapeo quedó igual que al arranque.
- **Volvé al escritorio original al terminar**, y matá la instancia por PID sacado de
  `pgrep -x kdock` **excluyendo el `/usr/local/bin/kdock` del usuario**. `pgrep -f
  './build/kdock'` también matchea el propio shell del comando y te mata la corrida a la
  mitad (exit 144, mismo problema que el `pkill -f` de más abajo).

### Manejar el diálogo desde código encuentra lo que la captura no muestra

Vale para cualquier solapa, y para la de Redes fue decisivo: una sonda que linkea los `.o`,
instancia el widget de la solapa, lo maneja con `findChildren` + `->click()` en el orden que
haría el usuario y después **verifica contra el sistema** (acá, releyendo la conexión de
NetworkManager con un `NetworkSettings` aparte). Dieciocho comprobaciones corren en dos
segundos bajo Xvfb, y encontró dos bugs que ninguna captura mostraba: el botón *Aplicar* que
no se armaba al editar la IP, y un `findChildren` de la propia sonda que agarraba el botón
equivocado (dos "Agregar" en la misma ventana — desambiguá por página, no por índice).

### Arnés de un componente QML suelto (popups del dock)

El arnés de Xvfb de arriba prueba que el QML **carga**, pero no deja *ver* nada: los popups
del dock (`AppMenuPopup`, `ClipboardPopup`, `DisksPopup`, `NetworkPopup`) solo se abren con
un clic. Para verlos con datos reales, un `QQuickView` que instancie el componente con los
backends de verdad como context properties, más `import -window root` bajo Xvfb. Con un
`CMakeLists.txt` descartable en `/tmp` que liste los `.cpp` que hagan falta (para
`AppMenuPopup`: `appmenu`, `xdgmenutree`, `desktopentry`, `dockconfig`, `theme`,
`iconprovider`) el `moc` sale gratis. Encontró todos los bugs de las dos rondas del menú.

Cinco trampas, las cinco mordieron (2026-07-31):

- **`XDG_DATA_HOME` aislado esconde los datos del usuario.** Hay que aislarlo, porque si no
  el arnés **le escribe la config real** al llamar a cualquier setter de `DockConfig`. Pero
  entonces no se ven ni los `.desktop` de `~/.local/share/applications`, ni los `.directory`,
  ni los íconos: los submenús salen **vacíos y parece un bug del código**. Enlazá los tres
  dentro del home descartable (`applications`, `desktop-directories`, `icons`), igual que la
  receta de diagnóstico de agrupación de más abajo.
- **La captura tiene que ser de la raíz de X, no del `QQuickView`.** Los popups son
  `popupType: Popup.Window`: viven en **otra** ventana, así que `view.grabWindow()` devuelve
  el fondo vacío. `import -window root` los agarra.
- **El proceso tiene que seguir vivo cuando sacás la captura.** Con un `singleShot` de salida
  más corto que el `sleep` del script, el PNG sale gris de dos colores — que se parece
  bastante a "el componente no renderizó".
- **`theme: theme` se auto-sombrea.** Un componente con `required property var theme`
  resuelve `theme` a su propia property, no a la context property, y todo queda `undefined`.
  Nombrá distinto las del arnés (`hostTheme`, `hostConfig`) — es la misma razón por la que
  `Dock.qml` usa los alias `_theme`/`_config`.
- **La ruta del repo tiene un `#`**, así que `import "/ruta/qml"` no es una URL válida para
  QML. Copiá `qml/` a `/tmp` y usá un import relativo. Y en el `CMakeLists.txt` descartable,
  **`#` arranca un comentario**: un `add_executable(... /home/…/#Apps/kdock/src/x.cpp)` se come
  el paréntesis de cierre y CMake dice *"Parse error. Function missing ending )"*, que no
  sugiere para nada la causa. Copiá `src/` a `/tmp` también. Ese CMake descartable necesita el
  mismo `env -u CC -u CXX` que el del proyecto: si no, falla en el AutoMoc (*"AutoMoc subprocess
  error"*), que tampoco menciona a ccache.

## Convenciones

- C++17, Qt 6 (≥ 6.5), `#pragma once`, `QStringLiteral`, `connect` con punteros a miembro,
  `Q_INVOKABLE` para lo que llame QML.
- QML: solo `QtQuick` + `QtQuick.Controls.Basic` (nada que dependa de estilos de KDE).
- Comentarios en inglés, en el estilo del archivo que estés tocando: explican *por qué*,
  no *qué* — la densidad actual es la referencia.
- Nada de KDE Frameworks como dependencia: los backends hablan D-Bus / CLI directo
  (UDisks2, NetworkManager, UPower, `pactl`, `wpctl`, `brightnessctl`, kglobalaccel).

## Trampas que muerden

- **Grosor del dock**: hay una sola fórmula, `DockConfig::dockThickness()`. `Dock.qml`
  la lee (`config.dockThickness`) y `DockWindow::thickness()` la devuelve para la
  *exclusive zone* de layer-shell. Cualquier opción nueva que cambie el tamaño
  transversal va dentro de `appCellThickness()` y tiene que emitir
  `dockThicknessChanged()`, o el dock tapa las ventanas maximizadas.
  Lo mismo vale para `kdock-previews`: su única fórmula es
  `PreviewConfig::stripThicknessPx()` (en px, no en %, para no depender del tamaño de
  pantalla), acotada por `PreviewConfig::kMinThickness`/`kMaxThickness` — **las mismas
  constantes las usa el spinbox del panel**, no repitas el rango a mano.
  Y desde 2026-08-04 la fórmula tiene **una rama**: con `showAppIcons=false` el
  `appCellThickness()` sale del `qMax`. Si agregás algo que dependa de las apps, acordate de
  las dos ramas — y de que `root.labelVisible` en `Dock.qml` es lo que corta la medición de
  los nombres de apps (si no, el dock reserva ancho para nombres que no dibuja).
- **Una entrada de esa fórmula se mide en QML**: `config.effectiveLabelWidth`, el ancho del
  nombre más largo que el dock dibuja (`iconLabelWidth` es solo el tope). `Dock.qml` lo mide
  off-screen y lo reporta con `config.setMeasuredLabelWidth()`; el detalle está en
  `AGENTS.md` → *App-icon labels*. Es la única realimentación QML→C++ del grosor y es segura
  porque el ancho natural de un texto no depende del grosor: **no le agregues un gancho de
  re-medición a `dockThicknessChanged`**, que es justo la señal que emite el setter.
  **Toda opción que cambie el peso o el tamaño del texto va en las dos puntas de esa
  medición**: la sonda (`appNameProbe`/`secNameProbe`, los `TextMetrics` del tope de
  `Dock.qml`) tiene que copiar la property, y el bloque `Connections { target: config }` tiene
  que traer su `on<X>Changed` para re-medir. Si te olvidás, el texto mide más que la caja
  reservada y **se elide**, que parece un bug de layout. Es lo que costó `config.labelBold`
  (2026-08-05): `appNameProbe` ya medía en negrita —mide siempre con el peso de la app
  activa—, así que el que faltaba era `secNameProbe`. Se verifica bajo Xvfb midiendo el
  grosor de un dock vertical antes y después de prender la opción (95 → 101 px).
- **Leer una propiedad D-Bus que es un struct: dos trampas, la segunda te tira el proceso.**
  Las dos mordieron el 2026-08-01 leyendo `desktops` (a(iss)) de
  `org.kde.KWin /VirtualDesktopManager` para el widget `closewindow`:
  1. **`QDBusInterface::property()` no alcanza**: se niega a demarshalar un tipo struct que no
     se registró con `qDBusRegisterMetaType`, avisa por stderr (*"type QDBusRawType<…> must be
     registered"*) y devuelve un `QVariant` **inválido** — o sea, la feature no hace nada y no
     falla. O registrás el metatipo (idiom en `src/systray.cpp`) o llamás a
     `org.freedesktop.DBus.Properties.Get` a mano y desenvolvés el `QDBusVariant`, que es lo
     que hace `ActiveWindowControl::desktopProperty()`.
  2. **El `QDBusArgument` local tiene que ser `const`.** `beginArray()`/`beginStructure()`
     tienen sobrecarga const (leer) y no-const (**escribir**); sobre un objeto no-const el
     compilador elige la de escritura, la lectura se desincroniza y **libdbus aborta el
     proceso entero** (*"type struct not a basic type"*, más un `QDBusArgument: write from a
     read-only object` antes). En el dock eso es un crash, no un valor raro.
  Las dos se destaparon **antes de instalar** con una sonda de 30 líneas
  (`g++ main.cpp $(pkg-config --cflags --libs Qt6DBus Qt6Core)`) que imprime lo que contesta el
  servicio: para cualquier D-Bus con tipos compuestos, vale la pena escribirla primero.
  Y una tercera, más barata todavía: **`busctl` imprime structs y `qdbus6` no**. `qdbus6`
  muestra `{D-Bus type "a(iss)"}` y se queda ahí; `busctl --user get-property org.kde.KWin
  /VirtualDesktopManager org.kde.KWin.VirtualDesktopManager desktops` escupe el contenido y de
  paso **la firma de verdad**, que en este caso es `a(uss)` (no `a(iss)`, como decía el
  comentario del código) y con las posiciones **0-based** (`0` = "Escritorio 1"). Dos segundos
  de `busctl` antes de escribir el demarshalling.
- **Un widget nuevo del dock se toca en SIETE archivos**, y el que se olvida no da error:
  el backend (`src/<x>control.{h,cpp}`) + su línea en `CMakeLists.txt`; `DockConfig`
  (`Q_PROPERTY`/getter/setter/señal/miembro en el `.h`, y `load()` + setter +
  **`knownWidgetTokens()`** + `defaultWidgetLabel()` en el `.cpp`); `main.cpp` (instancia +
  `shared.<x>`); `dockmanager.h` (campo en `Shared` + forward decl) y `dockmanager.cpp`
  (pasarlo al ctor); `dockwindow.{h,cpp}` (parámetro, miembro y `setContextProperty`);
  `Dock.qml` (`sectionVisible`, `componentFor`, `sectionTooltip`, `sectionClick` —más
  `sectionHasAltClick`/`sectionAltClick` si usa el clic derecho— y el `Component`); y el
  checkbox en `settingsdialog.cpp`.
  **El que muerde es `knownWidgetTokens()`**: `reconcileWidgetOrder()` **descarta** de
  `widgetOrder` todo token que no esté ahí, así que sin esa línea el widget **no aparece
  nunca** aunque su flag esté en true y el `Component` exista, y no se imprime nada. Al
  revés, si está, el token se **auto-agrega** al `widgetOrder` ya guardado de cada usuario:
  no hace falta migración.
  Un widget que **solo toca `DockConfig`** (el `darkmode`, por ejemplo) se salta cuatro de
  los siete: no lleva `src/<x>control.{h,cpp}`, ni `main.cpp`, ni `dockmanager.*`, ni
  `dockwindow.*` — no hay backend que instanciar ni context property que registrar. Quedan
  `DockConfig` (con su `knownWidgetTokens()`), `Dock.qml` y el checkbox del diálogo.
  Y un widget cuyo backend es un **lanzador de otro binario** (el `tilemenu`, con
  `TileMenuLauncher`) se salta `dockmanager.*`: el objeto es fino y sin estado, así que lo crea
  el propio `DockWindow` en su ctor —igual que `AppMenu`— en vez de viajar por `Shared`.
- **Una sección que se repite (`spring`, `sep`) NO va en `knownWidgetTokens()`**: esa lista
  se deduplica. Va en `DockConfig::isRepeatableToken()`, que es de donde
  `reconcileWidgetOrder()` saca el permiso para dejar pasar un token más de una vez. Si te
  olvidás, el token **desaparece del `widgetOrder` en el próximo load** sin imprimir nada.
  Y en `Dock.qml`, además del `Component` + `componentFor()` + `sectionVisible()`, hay que
  excluirla del `Binding` de `hovered` del delegate: un componente de sección que no declara
  esa property escupe *"Property 'hovered' does not exist on QQuickItem"* en cada arranque
  (lo agarró el arnés de Xvfb al primer intento, 2026-08-02).
- **Una clave nueva del grupo del menú se toca en CINCO lugares**: `Q_PROPERTY` + getter +
  señal, `load()`, `reloadMenuConfig()`, el `seedKey()` de `setMenuConfigShared()` y el setter
  vía `writeMenuConfigValue()` (no `m_settings.setValue()` directo). Si te salteás alguno
  compila igual y anda igual… hasta que el usuario prende *"Compartir configuración del menú
  entre docks"*: ahí la opción que falta se resetea al default o no se propaga. `menuColumns`
  es el ejemplo a copiar.
- **Los controles de las solapas Colores y Fuentes son copias, no atajos.** Desde 2026-08-05
  varias opciones existen **dos veces** en el diálogo (General/Widgets/DarkMode + Colores,
  Widgets + Fuentes). Cambiar de solapa **no** reconstruye el diálogo, así que cada copia
  necesita las dos direcciones: `connect` del widget al setter de `DockConfig`, y `connect` de
  la señal `*Changed` de vuelta al widget. Con una sola dirección el duplicado se ve viejo y
  parece que la opción "no se guardó". Lo mismo vale para el **gating**: si cambiás cuándo un
  control se deshabilita, cambialo en las dos solapas. Ejemplo a copiar: `labelBold` en
  `createGeneralTab()` y `createFuentesTab()` (`src/settingsdialog.cpp`) — y ojo con que el
  gating no siempre se hereda del vecino: el ancho y el tamaño de fuente del nombre se apagan
  si no hay ninguna etiqueta encendida, pero el casillero de negritas **queda activo siempre**,
  porque también pinta la fecha del reloj, que es texto fijo.
- **Nuevo `.qml`**: agregalo a `qt_add_resources` o no entra al qrc. Hay **tres** listas: la
  del `CMakeLists.txt` de arriba (para `qml/`), la de `previews/CMakeLists.txt` (para
  `previews/qml/`) y la de `tilemenu/CMakeLists.txt` (para `tilemenu/qml/`). Esta última tiene
  además un **segundo** `qt_add_resources` con `BASE ".."` que mete `qml/IconMenuItem.qml` de
  kdock **verbatim**: con ese `BASE` cae en `:/qml/`, o sea el mismo directorio del recurso que
  los `.qml` del menú, y se usa como tipo vecino sin ningún import.
- **Un componente de sección se instancia aunque la sección esté invisible.** El `Loader` del
  delegate de `sectionRepeater` no mira `sec.visible`: `visible: false` solo deja de
  *dibujarlo*. Para una sección cara (el bloque `apps`, que es un `Repeater` sobre todos los
  lanzadores y ventanas) hay que devolver **`null` desde `componentFor()`**, o sigue vivo y
  actualizándose detrás de un dock que no lo muestra (así se apaga `showAppIcons`).
- **La systray puede vivir en cualquier dock, pero en uno solo.** Cada dock tiene su
  `SystrayModel` (una vista filtrada sobre el `SystrayHost` del proceso) y el gate es su
  `config.showSystray`; la exclusividad es **política de la UI**, no una traba técnica. Dos
  cosas que muerden si tocás esto: al arrancar, `DockManager::normalizeSystrayOwner()` limpia
  las marcas de más —las configs viejas traen `showSystray=true` en varios docks porque solo el
  primario la dibujaba— y sin eso ves la bandeja duplicada; y la exclusividad se prueba con la
  sonda que maneja el diálogo desde código (dos docks *known* sobre una pantalla que no existe
  → `DockManager` no crea ninguna ventana y la sonda no necesita ningún backend real).
  Desde los docks por escritorio virtual la exclusividad es **por grupo**: dos docks chocan
  solo si sus conjuntos de escritorios se intersecan (`DockManager::canCoexist()`), y el
  diálogo pregunta con `systrayDockIdFor(dockId)`, no con `systrayDockId()`.
- **`m_instances` NO es "los docks visibles".** Un dock que el escritorio virtual actual no
  quiere sigue instanciado —con su `DockModel`, sus relojes y su `QQmlEngine` vivos— y solo
  tiene la superficie desmapeada (`Instance::onScreen`). Lo que decide qué se ve es
  `DockManager::wantedDocks(currentDesktop())`, y **cualquier opción nueva que cambie qué docks
  existen tiene que pasar por ahí**, que es el único lugar donde vive esa regla. Corolarios:
  un modelo de un dock oculto se sigue actualizando (barato, pero no es cero), y contar
  `m_instances` para saber cuántos docks hay en pantalla da de más.
- **Popups y menús**: siempre `popupType: Popup.Window`, si no quedan recortados dentro
  de la superficie del dock en Wayland.
- **QML no concatena literales adyacentes como C++.** Un `qsTr("primera parte "` seguido de
  `"segunda parte")` en la línea siguiente —el reflejo de cualquiera que venga del `.cpp`— es
  un **error de sintaxis** (*"Unexpected token `string literal`"*) que tira la carga del
  archivo entero. Va con `+`, o todo en una línea. Lo agarró el arnés al primer intento
  (2026-08-03), pero solo porque el arnés existe: `qmllint` **no lo reportó**.
- **Un `Menu` de QtQuick.Controls no se dimensiona solo a su ítem más largo.** Mordió tres
  veces en el menú de mosaicos: el ancho implícito sale corto y la última letra de la etiqueta
  más ancha queda **cortada por el borde del popup** — sin puntos suspensivos, así que no
  parece una elisión sino un bug de render. Pasa sobre todo cuando el menú mezcla ítems
  *checkable* (que reservan una columna para el tilde) con `IconMenuItem` (que dibuja su
  propio ícono). El arreglo es una línea por menú:
  `width: Math.max(implicitWidth + 16, <mínimo>)`. Vale también para los submenús.
- **Un `Menu` o un `ToolTip` declarados dentro del delegate se instancian por fila.** En el
  menú de mosaicos eso eran ~450 `Menu` (cada uno con su `Instantiator` de colores) y ~450
  `ToolTip` para "Todas las aplicaciones": **783 MB de RSS y 3,5 s** para abrir la sección,
  contra **245 MB y 0,9 s** con un solo menú en la raíz parametrizado por "qué mosaico lo
  abrió" y el `ToolTip` **adjunto** (`ToolTip.text`/`ToolTip.visible`), que ya comparte una
  instancia por ventana (medido 2026-08-03). Para el tooltip el arreglo es de una línea; vale
  la pena medir RSS con `grep VmRSS /proc/<pid>/status` antes de dar por buena una grilla
  grande.
- **`showMaximized()` no hace nada sin gestor de ventanas.** En la sesión real KWin lo
  respeta —y de paso resta los *struts* de los docks, que es todo el truco del menú de
  mosaicos—, pero bajo Xvfb no hay quién lo atienda y la `QQuickView` sale de **160x160**, que
  parece un bug de layout. Por eso `TileWindow::showOn()` también hace
  `setGeometry(screen()->availableGeometry())`: en Wayland lo pisa el `configure` del
  compositor, y bajo X pelado es lo único que llena la pantalla.
- **Un auto-placement que prueba cada celda contra la lista de lo ya puesto es O(n³).** Con
  "Todas las aplicaciones" (~450 mosaicos) eso no termina nunca. La grilla de ocupación
  (`QSet` de celdas) más un cursor que solo avanza lo deja lineal; ver
  `TileLayout::placement()`.
- **`QUrl` percent-codifica las llaves del uuid de KWin.** El uuid llega como
  `{461eb7ac-…}` y una URL `image://thumb/{461eb7ac-…}@3` le llega al provider como
  `%7B461eb7ac-…%7D@3`, así que la búsqueda en la caché falla y **la tarjeta queda vacía
  mientras todo el resto reporta éxito** (mordió 2026-07-30, costó dos rondas). Regla: nunca
  armes una URL con `uuid`; usá el rol `thumbId` (uuid sin llaves). `ThumbnailCache`
  normaliza igual las dos formas, y un *miss* del provider ahora avisa por stderr una vez.
- **`QIcon::setThemeName()` re-entra al `IconProvider`, así que un swap "temporal" del
  iconset global se filtra.** Para resolver *un* ícono contra otro set, el provider hacía
  `previous = QIcon::themeName()` → `setThemeName(override)` → renderizar →
  `setThemeName(previous)`. Parece seguro (un solo hilo, restauración incondicional) y no lo
  es: `setThemeName()` **notifica a las vistas de forma síncrona**, o sea que desde adentro
  del swap vuelve a entrar a `requestPixmap()`. La traza del arnés lo muestra sin lugar a
  dudas (`d=2` con el global puesto en el override de la llamada de afuera). Si la llamada
  anidada es un ícono **sin** override —un lanzador— se dibuja con el iconset del widget, y
  como su URL no lleva tema, **QML cachea ese pixmap y el ícono queda mal hasta el próximo
  bump de `theme.revision`**. Síntoma: al reiniciar, los íconos de las apps salen con el set
  de los widgets y "se arreglan solos" al tocar cualquier cosa del tema (mordió 2026-08-02,
  y parece un bug de render siendo estado global). Regla: **nunca toques `QIcon::setThemeName()`
  fuera de `Theme`**; resolvé por archivo (`IconProvider::resolveInTheme()`).
  Reproducirlo cuesta 40 líneas: `qInfo()` a la entrada y a la salida de `requestPixmap()`
  con un contador de profundidad, y el arnés de Xvfb con una **copia** de la config del
  usuario. Ojo con dos cosas del arnés acá: `QT_QPA_PLATFORM=offscreen` deja
  `QIcon::themeSearchPaths()` en solo `:/icons` (ningún ícono resuelve y todo sale nulo —
  usá `xcb` bajo Xvfb), y la config del usuario apunta a pantallas reales, así que hay que
  copiar `kdock-<screen>.conf` a `kdock-screen.conf` y poner `enabledScreens=screen`.
- **El portapapeles de Wayland es `ext-data-control-v1`, no `wlr-data-control`, y no pide
  privilegio.** Este KWin **no publica** el global `zwlr_data_control_manager_v1` (renombró su
  implementación; `wayland-info | grep data_control` lo confirma en dos segundos), y
  `ext_data_control_manager_v1` **no está** en el `interfacesBlackList` de
  `kwin/src/wayland_server.cpp` — o sea que, a diferencia de `org_kde_plasma_window_management`,
  no hay que tocar el `.desktop` ni refrescar ksycoca. Si te ponés a "arreglar" la
  autorización, estás persiguiendo un problema que no existe.
- **Cualquier transferencia del portapapeles va asíncrona o te cuelga el dock.** Los datos
  viajan por un pipe contra otro proceso: un `read()` bloqueante espera a una app que puede no
  escribir nunca, y un `write()` bloqueante se traba en cuanto el payload pasa los 64 KB del
  buffer del pipe (una captura de pantalla son megabytes). Va con `QSocketNotifier` +
  watchdog, y **`wl_display_flush()` después de `receive()`**: sin el flush el pedido no sale
  y el otro lado nunca escribe. Además `SIGPIPE` a `SIG_IGN`, porque el lector se puede ir a
  la mitad. Ojo también con leerse a uno mismo: el compositor te manda el evento `selection`
  **también cuando el dueño sos vos**.
- **Para escribir settings de NetworkManager hace falta `qDBusRegisterMetaType`.** Leer anda
  con los `operator>>` de `QDBusArgument`, así que la falta no se nota… hasta el `Update()` o
  el `AddConnection()`, que salen con la firma equivocada. Son dos: `NMSettingsMap`
  (`a{sa{sv}}`) y `QList<QVariantMap>` (`aa{sv}`, para `address-data`/`route-data`). Están en
  `NM::registerTypes()` (`src/nmdbus.cpp`); llamalo antes de tocar nada.
- **NM normaliza lo que le mandás, y hay que releerlo para saber qué guardó.** El caso que
  muerde: con `never-default=true` **borra la puerta de enlace**. Por eso el editor relee la
  conexión con `GetSettings` después de aplicar en vez de confiar en el formulario. Y las
  claves modernas (`address-data`, `route-data`, `dns-data`) **sí** se pueden escribir en NM
  1.54 — `dns-data` es `as`, así que no hace falta la conversión a uint32 en orden de red;
  mandá las nuevas y **borrá** las legacy (`addresses`/`routes`/`dns`) para no dejar dos
  fuentes de verdad.
- **Una prueba del editor de redes escribe en el NetworkManager de verdad.** Como el
  `BrightnessControl`, no hay caja de arena. Probá siempre sobre conexiones propias, sobre un
  dispositivo sin cable (acá `enp0s31f6`), y borralas al terminar. Y ojo con el arnés de
  `xdotool` sobre la lista de redes: un clic en una red **abierta** se conecta sin preguntar y
  te deja una conexión guardada nueva (me pasó, 2026-08-04). Para probar la fila de
  contraseña, seteá `pendingSsid` desde el QML del arnés en vez de tirar clics a ciegas.
- **`Qt::WA_TransparentForMouseEvents` deja sin eventos de mouse también a los hijos.** Se lo
  puse al widget de una fila (puesta con `setItemWidget()`) para que el clic llegara a
  `QListWidget::itemClicked`, dando por hecho que el `QCheckBox` hijo, sin el atributo, seguiría
  recibiendo los suyos. No: el checkbox quedó **muerto**, compilando, dibujándose bien y sin
  avisar nada (2026-08-05). Si una fila con widget propio tiene que ser clickeable *y* contener
  un control, hacé que la fila reporte su propio clic (`mouseReleaseEvent` → señal) y dejá el
  control como hijo normal, que lo recibe primero y lo acepta. De paso: el widget de la fila
  tapa al item, así que **el hover del `QListWidget` no se ve nunca** y el popup parece inerte;
  el hover hay que pintarlo en el `paintEvent` de la fila.
- **`QWidget` que aún no tiene padre no lo ve `findChildren()`.** Las páginas del editor de
  conexiones se crean sueltas y solo entran al árbol cuando `addTab()` las adopta, así que el
  barrido del constructor que conecta "cualquier edición arma el botón Aplicar" **no las
  agarraba**: editar una dirección IP dejaba *Aplicar* gris y no había forma de guardar. Compila,
  arranca y parece andar. Lo agarró en la primera corrida el test que maneja el diálogo desde
  código (`findChildren` + `->click()`), no la captura.
- **Nunca `setVisible(true)` sobre una página adentro de un `QTabWidget`.** La pinta encima de
  la actual: dos formularios superpuestos letra por letra, que se lee como un bug de fuente y
  no de layout (2026-08-04).
- **El arnés de Xvfb tiene efectos reales sobre la máquina.** No es una caja de arena: el
  `BrightnessControl` reaplica al arrancar el `Brightness/lastBrightness` de la config que le
  des (`brightnessctl set`), así que correr el arnés **te cambia el brillo de la pantalla** al
  valor que tenga ese `.conf`. Lo mismo vale para cualquier backend que escriba al sistema.
  Y ojo con `XDG_DATA_HOME`: aísla la config de kdock, pero **no** `kdeglobals` (que sale de
  `XDG_CONFIG_HOME`), así que `applyIconTheme()`/`applyColorScheme()` le cambian la apariencia
  al escritorio de verdad aunque el arnés parezca aislado.
- **Un clic por coordenadas contra una lista que se reordena apunta a otra fila.** Para probar
  sin efectos, la regla era "hacé clic en la fila del tema que ya está aplicado". Pero entre
  corrida y corrida el arnés había marcado un favorito, los favoritos van **arriba**, y esas
  mismas coordenadas cayeron en otra fila: le cambié el iconset al escritorio del usuario
  (2026-08-05). Si la lista se puede reordenar, no fijes el `xdotool mousemove`: leé el orden
  del modelo primero y calculá la fila, o mejor ejercitá el widget desde código
  (`->click()` sobre el hijo que buscaste), que no depende de la geometría. Y acordate de que
  el estado que el arnés persiste **sobrevive a la corrida**: el `XDG_DATA_HOME` descartable
  hay que borrarlo entre pruebas o la siguiente arranca con otra lista.
- **Clave de fila en `DockModel`**: con `groupWindows=false` una ventana se indexa como
  `win:<puntero>`. Todo camino que convierta esa fila en lanzador anclado tiene que
  **re-indexarla por la app**, o queda un lanzador huérfano que relanza en cada clic.
  Ya mordió una vez (`togglePinned`, 2026-07-29).
- **`enabledScreens=` vacío no es lista vacía**: QSettings lo devuelve como `[""]`, así que
  `migrateFirstRun()` cree que ya hay docks habilitados y **no crea ninguno**. Para una
  config de prueba, omití la clave por completo en vez de dejarla vacía.
- **`kdock-previews` necesita DOS privilegios de KWin**, no uno:
  `X-KDE-Wayland-Interfaces=org_kde_plasma_window_management` (ver las ventanas) y
  `X-KDE-DBUS-Restricted-Interfaces=org.kde.KWin.ScreenShot2` (capturarlas). KWin resuelve
  `/proc/<pid>/exe` contra el `Exec=` **absoluto** de un `.desktop` que encuentre por
  ksycoca. Sin el segundo, cada captura vuelve `org.kde.KWin.ScreenShot2.Error.NoAuthorized`
  y las tarjetas quedan con el ícono de la app — **parece un bug de render y es
  autorización**.
- **Correr `./build/previews/kdock-previews` no está autorizado.** Para desarrollar hace
  falta un `.desktop` temporal en `~/.local/share/applications/` con las dos claves y
  `Exec=` a la ruta absoluta del binario, más el `kbuildsycoca6` de arriba (con la ruta
  explícita y el `XDG_DATA_DIRS`, no el pelado). Y ojo: **la ruta del repo
  tiene un `#`, que KConfig no parsea en un `Exec=`** — copiá el binario a un directorio sin
  `#` (p. ej. `/tmp/kdock-prev-dev/`) y apuntá ahí. Borralo al terminar: mientras exista, le
  concede privilegio de captura a lo que haya en esa ruta.

## Depurar agrupación de ventanas

Los bugs de "el ícono anclado no captura su ventana" no se resuelven leyendo código: hay
que ver el `app_id` real. Receta que funcionó (2026-07-29):

**1. Qué reporta el compositor** — script temporal de KWin por D-Bus: `evaluateScript` con
un `workspace.windowList().forEach(w => print(w.resourceClass, w.resourceName, w.caption))`,
leído del journal. Ojo: **kdock recibe el `resourceClass`**, no el `resourceName`.

**2. Qué recibe kdock** — instrumentar `DockModel` (`placeWindow`, `windowChanged`,
`activate`) con `qInfo`, compilar **sin instalar** y correr una **segunda instancia** con
la config del dock real clonada y `edge=` distinto para no pisar la que está en pantalla:

```bash
cp ~/.local/share/kdock/kdock-DP-1.conf /tmp/kdiag-home/kdock/   # + edge=1, autohide=true
setsid env XDG_DATA_HOME=/tmp/kdiag-home ./build/kdock > /tmp/kdiag.log 2>&1 < /dev/null &
```

Enlazá `/tmp/kdiag-home/applications` → `~/.local/share/applications` o el índice de
`.desktop` queda vacío. Y **KWin solo concede `org_kde_plasma_window_management` si el path
del ejecutable figura en un `Exec=` de algún `.desktop` con `X-KDE-Wayland-Interfaces`**:
sin un `kdock-diag.desktop` temporal apuntando al binario del build, la instancia no ve
ninguna ventana (avisa por stderr: *"compositor offers neither…"*). Borralo al terminar.

La segunda instancia ve **todas** las ventanas, así que sirve para observar sin tocar el
dock del usuario. Cuidado con `pkill -f 'build/kdock'`: mata también al propio shell del
comando y te come los pasos siguientes.

**3. Probar resolución sin GUI** — arnés que enlaza `desktopentry.cpp` real (necesita
`moc` de Qt6, no el de Qt5) y consulta `forAppId()` con los `app_id` observados; sirve
además de test de regresión contra los `pinned=` de todos los `.conf`.

### Trampas conocidas del `app_id` de Chromium

- Una ventana **recién abierta** de Edge llega como `msedge` (nombre del **directorio de
  instalación** del binario real), no `microsoft-edge`, y **KWin nunca manda una
  corrección** después. Las que ya estaban abiertas al arrancar el dock sí traen el id
  completo — por eso "reiniciar y probar" esconde el bug.
- Las PWA llegan como `msedge-_<extid>-Default`, con un guion bajo de más respecto del
  nombre del `.desktop`.

Ambas resueltas por las heurísticas 5 y 6 de `forAppId()` (ver `AGENTS.md` → Dock Model).

## Depurar las vistas previas (`kdock-previews`)

Los bugs de "las tarjetas no muestran la ventana" tampoco se resuelven leyendo código:
hay que ver qué contesta KWin. El binario trae el diagnóstico adentro:

```bash
kdock-previews --dump-captures /tmp/kdock-thumbs
```

Enumera las ventanas que reporta el compositor (uuid, `app_id`, flags `MIN`/`ACTIVE`/
`SKIPTASKBAR`, geometría, título), pide una captura de cada una, las escribe como PNG y
sale. **No abre ninguna ventana**, así que sirve en la sesión real sin molestar al usuario.
Lecturas:

- 0 ventanas → falta `X-KDE-Wayland-Interfaces` (o no estás en Wayland).
- ventanas sí, capturas con `NoAuthorized` → falta `X-KDE-DBUS-Restricted-Interfaces`.
- PNGs con colores raros o corridos → mal el `stride`/`format` del reply, no el pipe.

Y si el `--dump-captures` anda pero **la tarjeta se queda con el ícono de la app**, el proceso
avisa por stderr una vez por ventana (`capture of <uuid> failed…`); si lo lanzó kdock, eso va
al journal:

```bash
journalctl --user -f | grep -i previews
```

**Pero no des por sentado el journal**: si la instancia se relanzó a mano (`setsid … 2> archivo`,
que es lo normal después de instalar), su stderr **no está ahí** y `journalctl` sale vacío
como si el proceso no dijera nada. Averiguá a dónde escribe antes de concluir nada — se
perdió una ronda entera de diagnóstico buscando unos `console.log` que sí se estaban
imprimiendo (2026-07-31):

```bash
ls -l /proc/$(pgrep -x kdock-previews)/fd/2     # p. ej. → /tmp/previews-err.log
```

Lo mismo vale para **`kdock`**: el que arranca con la sesión sí manda su stderr al journal,
pero uno relanzado a mano después de instalar (`setsid /usr/local/bin/kdock 2> archivo`, que
es la única forma de reiniciarlo desde la CLI —el `restart()` solo se puede invocar desde su
menú—) escribe donde lo hayas mandado. Mismo `ls -l /proc/$(pgrep -x kdock)/fd/2`.

Ojo que en el modo por defecto (`captureMode=0`) que una tarjeta muestre el ícono **es lo
esperado** hasta que esa ventana pase a primer plano por primera vez.

Verificado 2026-07-30 en la sesión real: 12 ventanas, 12 capturas, y **las 2 minimizadas
salieron con contenido completo** — KWin re-renderiza la ventana aunque no esté visible, así
que no hace falta volver a discutir ese riesgo.

### Ver una tira de verdad sin tocar la del usuario

Todo lo que depende del **layer-shell** —dónde queda anclada la superficie, la alineación, el
grosor real, la zona exclusiva— **no se puede probar bajo Xvfb**: ahí es una ventana X normal.
La receta que funcionó (2026-07-31, con la que se cerró el bug de alineación) es una **segunda
instancia aislada en la sesión Wayland real**:

```bash
mkdir -p /tmp/kdp-live/kdock
printf '[General]\nenabled=true\nenabledScreens=eDP-1\nknownScreens=eDP-1\n' > /tmp/kdp-live/kdock/previews.conf
printf '[General]\nedge=1\nalignment=1\nstripLength=0\nreserveSpace=false\n'  > /tmp/kdp-live/kdock/previews-eDP-1.conf
setsid env XDG_DATA_HOME=/tmp/kdp-live dbus-run-session -- /usr/local/bin/kdock-previews &
```

Las tres piezas importan:

- **`dbus-run-session`** le da un bus propio, así el chequeo de instancia única
  (`PreviewsService::alreadyRunning()`) no la manda a salir. Pierde el D-Bus de KWin (no hay
  capturas), pero **la lista de ventanas sí funciona**: sale de un protocolo de Wayland.
- **La ruta tiene que ser la instalada** (`/usr/local/bin/kdock-previews`). Desde
  `build/previews/` KWin no concede el privilegio y la tira sale con *"Sin ventanas"*, que
  parece un bug de filtros y es autorización.
- **`reserveSpace=false`** y un borde distinto al de la tira real, para no moverle las
  ventanas al usuario.
- No hay `reload()` que sirva: `PreviewManager::reload()` solo crea/destruye tiras, **no
  relee los `.conf`**. Entre variante y variante hay que matar y relanzar el proceso.

Para *ver* el resultado, capturá la pantalla con **`spectacle-custom`**: el `spectacle` de
`/usr/bin` no está autorizado en esta máquina y falla con *"The process is not authorized to
take a screenshot"*.

```bash
spectacle-custom -b -n -f -o /tmp/shot.png
```

Y al terminar, **matá la instancia por PID**: `pkill -f kdock-previews` (o un `grep` sobre
`ps aux`) también mata al shell del propio comando, porque la línea de comando del shell
contiene el patrón. Filtrá con `pgrep -x kdock-previews` y excluí el PID de la instancia real.

### El arnés de Xvfb no ve todos los binding loops

El arnés carga el QML, pero en Xvfb no hay compositor que reporte ventanas: **el modelo queda
vacío**. Cualquier bucle que necesite tarjetas para cerrarse —el de la alineación, que iba
`alignOffset → leftMargin → contentWidth → alignOffset`— sale **limpio** ahí y recién aparece
al reiniciar la instancia real (2026-07-31). Después de tocar geometría del `ListView`,
reiniciá el proceso real y leé su stderr antes de cantar victoria.

## Al terminar una feature

Documentala en `AGENTS.md` (sección temática + tabla QML↔C++ si expone algo nuevo a QML).
Si además cambia cómo se compila, se prueba o se instala, o si agrega una trampa que muerde,
va acá en `CLAUDE.md`.
