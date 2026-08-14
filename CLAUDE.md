# kdock — instrucciones para Claude Code

**Leé `AGENTS.md` antes de tocar código.** Es el documento de contexto del proyecto
(arquitectura, cada widget, trampas de Wayland/layer-shell, tabla QML↔C++) y se mantiene
al día con cada feature; este archivo solo tiene lo que hay que saber sí o sí.

## Tests: corré `tests/run.sh` antes de instalar

Desde 2026-08-09 las recetas manuales de este archivo están **congeladas en una suite** (ver
`tests/README.md`). Lo que antes era media hora de arneses a mano ahora es un comando:

```bash
tests/run.sh                 # static + unit + qml (~1 min) — lo mismo que corre CI
tests/run.sh --tier live     # dodge y multi-monitor contra KWin, en la sesión real
```

Todo está en ctest con labels (`static`, `unit`, `qml`, `live`) y el workflow de GitHub
Actions (`.github/workflows/ci.yml`) corre `ctest -LE live` en un container de Arch, porque
el runner de Ubuntu trae Qt 6.4 y el proyecto pide ≥ 6.5.

**Las recetas de más abajo siguen valiendo como el *porqué* de cada test y para diagnosticar a
mano**, pero para verificar un cambio empezá por la suite. Tres cosas que importan:

- **Un test que no puede correr se saltea (código 77), no falla.** `Skipped` no es verde:
  sin `xvfb-run` no hay tier `qml`, sin `lupdate` no se valida el catálogo, con un solo
  monitor `live.multimonitor` no prueba nada.
- **Toda corrida es sandbox**: `XDG_DATA_HOME` descartable + herramientas falsas en el `PATH`
  (`tests/lib/`). Si escribís un test nuevo que arranca un binario, pasá por ahí o le vas a
  cambiar el brillo y el tema al usuario.
- **Hay cuatro costuras de test en producción**, las cuatro apagadas por defecto y documentadas
  en el código: `KDOCK_TEST_SCREENS` (lista de monitores, `src/dockmanager.cpp`),
  `KDOCK_DEBUG_DODGE` (transiciones de `windowsOverlap`, `src/dockwindow.cpp`),
  `KDOCK_WEATHER_FIXTURE` (una respuesta grabada del proveedor en vez de la red,
  `src/weathercontrol.cpp`) — esta última es lo que hace que `tst_weather` corra en CI, que no
  tiene internet — y `KDOCK_TEST_DISPLAYS` (la lista de monitores de PowerDevil como JSON,
  `src/screenbrightness.cpp`), que es lo que deja probar a qué monitor le habla la rueda del
  brillo sin PowerDevil y sin atenuarle la pantalla a nadie. En esa última **manda que la
  variable esté puesta, no su contenido**: `[]` es como se dice "acá no hay PowerDevil", y
  decidir por la lista parseada mandaría ese caso al bus de sesión, donde contesta el PowerDevil
  de verdad y el test deja de ser un test.

## Build

```bash
env -u CC -u CXX cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/kdock
```

`env -u CC -u CXX` es obligatorio: las variables apuntan a ccache y CMake falla si están
seteadas.

### Qt style (widgets + QML)

Desde RELEASE 0.1.12, Configuración → General tiene un combo que elige el estilo de Qt
para **todo el proceso** (`QApplication::setStyle`) y para los menús QML del dock
(`QQuickStyle::setStyle`). La clave `qtStyle` en `kdock.conf` (compartida, vacío = Fusion).

Aplicarlo correctamente llevó cuatro iteraciones (matchean los commits):

1. **`app.setStyle` no alcanza**: los menús y popups del dock son QML (`Popup.Window`) y
   usan su propio sistema de estilos (Qt Quick Controls 2), independiente de Qt Widgets.
2. **`QQuickStyle::setStyle` no surte efecto** mientras los `.qml` importen
   `QtQuick.Controls.Basic` explícitamente. Ni la API ni la variable de entorno
   `QT_QUICK_CONTROLS_STYLE` lo overridean: el import tiene precedencia absoluta.
3. **Fix**: cambiar los ~40 `.qml` de `import QtQuick.Controls.Basic` a
   `import QtQuick.Controls` (sin sufijo). Sin el import forzado, `QQuickStyle::setStyle`
   funciona.
4. **Solo los nombres QQC2 conocidos** (`Basic`, `Fusion`, `Material`, `Universal`,
   `Imagine`) se pasan a `setStyle`; cualquier otro (Breeze, Oxygen, Windows…) cae a
   Fusion, o kdock no arranca.

**Son seis binarios**, los seis los compila el mismo `cmake --build build`:

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
- `kdock-controlmanager` (`build/controlmanager/kdock-controlmanager`, árbol
  `controlmanager/`): el panel de control con solapas (audio, brillo por monitor, energía,
  calendario, reproducción, red, wallpaper, sistema) y su grilla de tarjetas. Lo prende el
  widget `controlmanager` del dock. Detalles en `AGENTS.md` → *Control Manager*.
- `kdock-weather` (`build/weather/kdock-weather`, árbol `weather/`): el clima —condición
  actual, pronóstico y detalles— con su propia configuración de ciudades. Lo prende el widget
  `weather` del dock y el botón de la tarjeta *Clima* del panel. **Instancia única pero NO
  residente**: cerrar la ventana termina el proceso, así que nunca se queda con el binario
  viejo mapeado después de un `install`. Detalles en `AGENTS.md` → *Clima*.

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

Los accesorios se reinician distinto y sin riesgo de dejar la pantalla pelada: cada uno sale
por D-Bus (`org.kdock.Previews` / `org.kdock.TileMenu` / `org.kdock.ControlManager`, método
`quit`) y vuelve a levantarse solo — el de previews al prender su casilla, los otros dos en el
próximo clic de su widget (o enseguida, si tienen *Precargar*). O sea que actualizar
`kdock-tilemenu` es `install` + matarlo; no hace falta tocar el dock.

**Desde 2026-08-10 *Dock → Reiniciar* ya los baja a los tres** (`src/apprestart.cpp`), así que
para el usuario alcanza con eso. Pero **un accesorio que quedó corriendo no muestra el arreglo
que acabás de instalar**, y desde afuera se ve idéntico a "el bug sigue": el proceso tiene
mapeado el binario que el `install` reemplazó. La comprobación son dos segundos y no hay que
saltearla antes de volver a diagnosticar:

```bash
ls -l /proc/$(pgrep -f /usr/local/bin/kdock-controlmanager | head -1)/exe   # "(deleted)" = es el VIEJO
```

Pasó tal cual: se probó un arreglo del panel de control contra una instancia del día anterior
y el bug "seguía" (2026-08-10).

El install escribe **doce** cosas: los seis binarios y sus seis `.desktop`. Después de
instalar hay que refrescar ksycoca, pero **solo por los dos primeros**: KWin busca ahí los
`.desktop` para conceder los privilegios (sin eso `kdock-previews` se queda sin capturas y las
tarjetas caen a ícono). **`kdock-tilemenu`, `kdock-calendar`, `kdock-controlmanager` y
`kdock-weather` no necesitan el refresco** —no piden ningún privilegio, y su `.desktop` es solo para el nombre y
el ícono del gestor de tareas—, así que si lo único que tocaste fue uno de esos tres, saltealo
y evitás el riesgo de abajo. (El panel de control **es** una superficie layer-shell, pero
`zwlr_layer_shell_v1` no está en la lista restringida de KWin: se lo anuncia a cualquier
cliente, verificado con `wayland-info`.) Y ojo:
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

### Arnés de `kdock-controlmanager`

Dos herramientas, y conviene saber cuál contesta qué. **`--dump-sections` es la barata y la
única inocua**: imprime la grilla resuelta en ASCII, la tabla de secciones, los monitores con
brillo, los reproductores MPRIS y lo que contesta `org.kdock.Dock`, y sale sin abrir ninguna
ventana. **Solo lee**, así que sirve con el panel corriendo y sobre la config del usuario.

```bash
./build/controlmanager/kdock-controlmanager --dump-sections
XDG_DATA_HOME=/tmp/cm ./build/controlmanager/kdock-controlmanager --dump-sections   # la de prueba
```

Y el arnés de Xvfb, que sí carga el QML y se puede capturar. `--section <id>` abre derecho en
esa solapa, que es lo que hace innecesario andar tirando clics para ver una sección:

```bash
rm -rf /tmp/cm && mkdir -p /tmp/cm/kdock
ln -s ~/.local/share/applications /tmp/cm/applications
ln -s ~/.local/share/icons        /tmp/cm/icons
printf '[General]\nkeepOpen=true\npanelWidth=1000\npanelHeight=520\n' > /tmp/cm/kdock/controlmanager.conf
xvfb-run -a -s "-screen 0 1920x1080x24" dbus-run-session -- bash -c '
  env -u WAYLAND_DISPLAY XDG_DATA_HOME=/tmp/cm QT_QPA_PLATFORM=xcb QT_QUICK_BACKEND=software \
      PATH=/tmp/fakebin:$PATH ./build/controlmanager/kdock-controlmanager --show --section audio \
      > /tmp/cm.log 2>&1 &
  APP=$!; sleep 6; import -window root /tmp/cm.png; kill $APP'
```

Seis cosas de este arnés, todas costaron una corrida:

- **`env -u WAYLAND_DISPLAY` es obligatorio.** Sin eso el proceso ve el Wayland del usuario,
  se pone `QT_WAYLAND_SHELL_INTEGRATION=kdock-layershell` y **le abre el panel en la pantalla
  de verdad** en vez de en el Xvfb.
- **`dbus-run-session` para probar el QML**, igual que con el menú de mosaicos: el candado de
  instancia única es el nombre en el bus, y `xvfb-run` hereda el del usuario. El precio es que
  **los backends de D-Bus se quedan sin datos** (PowerDevil, MPRIS y `org.kdock.Dock` no están
  en un bus propio), así que las secciones salen con sus mensajes de "no responde" — que es lo
  correcto, no un bug. Para *ver* datos reales hay que correr **sin** `dbus-run-session`, y ahí
  vale lo de siempre: que no haya otra instancia.
- **Bajo X no hay layer-shell**: la ventana es un X normal, así que el arnés **no** prueba
  anclaje, alineación, margen ni el ida y vuelta del teclado. Silencio ahí prueba que el QML
  carga y que las tarjetas dibujan, nada más. Eso solo se prueba en la sesión Wayland real.
- **El `PATH` falso importa más acá que en ningún otro arnés del proyecto.** Este binario
  maneja brillo, volumen y modo oscuro; sin los scripts de `/tmp/fakebin` (receta de las
  herramientas falsas, más abajo) una corrida **le cambia el brillo al usuario**. El brillo por
  D-Bus tiene otra reja: `ScreenBrightness` solo escribe desde un clic.
- **La captura es de la raíz de X** (`import -window root`), no de la `QQuickView`: los menús
  son `Popup.Window` y viven en otra ventana.
- **`rm -rf` del `XDG_DATA_HOME` entre corridas**, o la siguiente arranca con la disposición
  que dejó la anterior y cualquier aserción sobre "la tarjeta está en (3,0)" falla por una
  razón que no tiene nada que ver.

**El arrastre de una tarjeta se prueba de punta a punta**, y la prueba es el volcado, no la
captura. Ojo con dos cosas al calcular las coordenadas: **solo la cabecera arrastra** (22 px,
o 10 si el título está apagado — un clic un píxel más abajo cae en el contenido y no pasa
nada, me pasó), y el origen de la grilla es `pad(12) + barra de solapas(38) + 6`:

```bash
XDG_DATA_HOME=/tmp/cm ./build/controlmanager/kdock-controlmanager --dump-sections | head -12
# ... arnés con xdotool mousemove <x> <y> mousedown 1 / mousemove / mouseup 1 ...
XDG_DATA_HOME=/tmp/cm ./build/controlmanager/kdock-controlmanager --dump-sections | head -12
```

**La sección *Escritorios* del panel sí se prueba entera bajo Xvfb**, igual que el widget
paginador del dock: el clic va por XTEST y el cambio de escritorio va por D-Bus al KWin **de
verdad**. Pero hay que correr **sin** `dbus-run-session` (con un bus propio la sección sale con
su "KWin no informa escritorios virtuales", que es lo correcto y no un bug), anotar el
escritorio original y restaurarlo:

```bash
ORIG=$(busctl --user get-property org.kde.KWin /VirtualDesktopManager \
       org.kde.KWin.VirtualDesktopManager current | sed 's/^s "//;s/"$//')
# ... arnés + xdotool mousemove <x> <y> click 1 ...
busctl --user set-property org.kde.KWin /VirtualDesktopManager \
       org.kde.KWin.VirtualDesktopManager current s "$ORIG"
```

Y ojo con la otra mitad: sin `dbus-run-session` el proceso **toma el nombre en el bus real**,
así que si el panel del usuario está corriendo tu `--show` se le reenvía a *él* (y le abre el
panel en la pantalla), y si no está, el próximo clic del widget abre **el tuyo, dentro del
Xvfb**. Bajalo con `qdbus6 org.kdock.ControlManager /ControlManager quit` antes y matá el del
arnés al terminar.

**Y la sección *Apariencia* le cambia el iconset y el esquema al escritorio de verdad**: es
`AppearanceControl`, o sea las herramientas de Plasma, y `XDG_DATA_HOME` no aísla nada de eso.
En el arnés, o no le hacés clic a ninguna fila, o corrés con el `PATH` falso de más abajo — que
para este binario ya hacía falta por el brillo.

**El panel de configuración se maneja desde código**, como el del dock: linkeá los `.o` del
target (menos `main.cpp.o`), instanciá `CmConfig` + `CmLayout` + `CmSettingsDialog`, buscá los
controles y hacéles `->click()`. **Desambiguá por el `QGroupBox` que los contiene**, nunca por
índice: el diálogo tiene casilleros en Ventana, en Apariencia y en Secciones, y
`findChildren<QCheckBox*>()[0]` no es el que creés.

**Y para probar `org.kdock.Dock` hace falta un kdock corriendo en el bus real**: el del build,
bajo Xvfb y **sin** `dbus-run-session`, es lo que cierra el círculo sin tocarle el dock al
usuario. Con eso se verifica lo único que el `.conf` no muestra —que el modo oscuro repinta—:

```bash
xvfb-run -a -s "-screen 0 1920x1080x24" bash -c '
  env -u WAYLAND_DISPLAY XDG_DATA_HOME=/tmp/kd-cm QT_QPA_PLATFORM=xcb QT_QUICK_BACKEND=software \
      PATH=/tmp/fakebin:$PATH ./build/kdock > /tmp/kd.log 2>&1 &
  sleep 6
  busctl --user call org.kdock.Dock /Dock org.kdock.Dock setDarkMode b true
  qdbus6 org.kde.kglobalaccel /component/kdock invokeShortcut toggle-dark-mode
  import -window $(xwininfo -root -children | awk "/\"kdock\": \(\"kdock\"/ {print \$1}") /tmp/dock.png'
```

Ojo con la otra mitad de eso: **esa corrida registra el atajo global en el kglobalaccel de
verdad** (componente `kdock`, acción `toggle-dark-mode`, sin tecla asignada). Es lo que
queremos que quede, pero conviene saber que pasa, y se comprueba con
`busctl --user call org.kde.kglobalaccel /component/kdock org.kde.kglobalaccel.Component shortcutNames`.

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

Y el corolario para escribir: **`QSettings` mapea a la sección solo el PRIMER `/`**, el resto
queda dentro del nombre de la clave. Una clave `Wallpapers/desktop2/eDP-1` no da
`[Wallpapers2] eDP-1`, da `[Wallpapers] desktop2/eDP-1`, y ahí los caracteres raros del resto
entran en las reglas de escapado. Si la segunda mitad es un dato (un nombre de conector, una
ruta), usá **una sección por valor** (`[Wallpapers2]`, `[Wallpapers3]`) o metelo todo en una
sola clave como JSON — que es lo que hace el snapshot de `DesktopWallpapers`.

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
OBJS=$(find build/CMakeFiles/kdock_core.dir -name '*.o' | tr '\n' ' ')
g++ -std=c++17 -fPIC /tmp/p/dlg.cpp $OBJS -Isrc \
    $(pkg-config --cflags --libs Qt6Widgets Qt6Quick Qt6Qml Qt6DBus Qt6WaylandClient \
                                 Qt6Gui Qt6Core wayland-client) -o /tmp/p/dlgprobe
```

**Es `kdock_core.dir`, no `kdock.dir`** (desde que existe la suite de tests: las clases viven en
la librería que comparten `kdock` y los tests). Linkear `kdock.dir` da *"vtable for X sin
definir"* para cualquier `QObject` — su `mocs_compilation.cpp` está **vacío** y los `moc_*.cpp`
se compilan del otro lado. Y no mezcles las dos: `qrc_translations.cpp` está en ambas y salen
*"definiciones múltiples"*.

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

### Probar el brillo sin atenuarle la pantalla al usuario

Tres cosas que no son obvias y costaron una corrida cada una (2026-08-12):

- **`brightnessctl` no es el brillo que el usuario ve.** Maneja el backlight interno y nada más.
  En esta sesión el panel es `intel_backlight` y el monitor que se mira es un Samsung por DDC que
  solo conoce PowerDevil (`busctl --user get-property org.kde.ScreenBrightness
  /org/kde/ScreenBrightness DisplaysDBusNames`, y `GetAll` sobre cada display). O sea que un
  arnés que verifica con `brightnessctl -m info` puede dar verde sobre una feature que no mueve
  nada de lo que se ve.
- **El `PATH` falso tapa `brightnessctl` pero NO PowerDevil**: es D-Bus al bus de sesión, y desde
  adentro de Xvfb se llega igual. Un slider de la solapa VideoEnergía movido en una sonda le
  cambia el brillo al monitor de verdad. Anotá y devolvé:

  ```bash
  busctl --user get-property org.kde.ScreenBrightness /org/kde/ScreenBrightness/<display> \
         org.kde.ScreenBrightness.Display Brightness
  busctl --user call org.kde.ScreenBrightness /org/kde/ScreenBrightness/<display> \
         org.kde.ScreenBrightness.Display SetBrightness iu <valor> 0
  ```

  Para probar la *lógica* (qué monitor elige la rueda) no hace falta nada de eso: está la costura
  `KDOCK_TEST_DISPLAYS` de más arriba.
- **La rueda del widget sí se prueba de punta a punta bajo Xvfb**, porque el clic va por XTEST y
  el brillo va por D-Bus al PowerDevil de verdad: dock del build con `showBrightness=true`,
  `xdotool mousemove <x> <y> click 5`, y el `busctl` de arriba dice si bajó **ese** display y
  ninguno más. Ojo con las coordenadas: bajo X no hay layer-shell, así que el dock queda en
  `+0+0` con su grosor (1920x68) aunque el borde configurado sea el de abajo — sacá la posición
  del ícono de una captura previa, no del `edge`.

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

**Y esa lectura vieja no es solo un problema del arnés: guardarla rompe el modo normal para
siempre** (2026-08-10, bug reportado por el usuario). `DarkModeAppearance` capturaba el esquema
de color vivo al entrar en oscuro, para restaurarlo al salir. Salir del modo y volver a entrar
dentro de esos ~900 ms lee un `kdeglobals` que **todavía tiene el esquema oscuro que kdock
acababa de escribir**, y lo guarda como "lo que había antes": desde ahí, salir del modo oscuro
*restaura oscuro*, y no hay forma de que se recupere solo. Se destapó con el
`darkModeColorSchemePrev` del usuario apuntando a un `.colors` con `BackgroundNormal=24,25,38`.
Regla general: **antes de guardar un valor leído del sistema como "el del usuario", verificá que
no sea uno que vos mismo escribiste** — kdock lo hace con `DockConfig::darkAppearanceSelfApplied()`.
La sonda que lo prueba son 60 líneas contra `kdock_core` con las herramientas falsas en el `PATH`
y un `XDG_CONFIG_HOME` descartable (que es lo que aísla `kdeglobals`; `XDG_DATA_HOME` **no**
alcanza), simulando la carrera a mano: escribir el valor oscuro en el `kdeglobals` falso entre
paso y paso. El control positivo —sacar la guarda, recompilar, correr— pasa de `PASS` a
`prev=DarkTest` en una corrida.

### Arnés de `kdock-weather`

El más barato de los seis: la ventana es un toplevel común, así que se corre desde `build/`, se
maneja con `xdotool` y se captura entera. Dos herramientas, como en el panel de control.

**`--dump` es la inocua**: imprime la ciudad, la condición actual, los N días y las filas de
detalles, y sale sin abrir ninguna ventana. Sirve con una instancia corriendo (solo lee) y es
la forma de comparar contra el proveedor:

```bash
./build/weather/kdock-weather --dump
KDOCK_WEATHER_FIXTURE=tests/fixtures/weather/corrientes.json ./build/weather/kdock-weather --dump
```

**`KDOCK_WEATHER_FIXTURE` apunta a una respuesta grabada de Open-Meteo** y reemplaza a la red:
sin ella, un arnés depende de que haya internet y de que el clima no cambie entre corridas, y
las aserciones sobre "12 °C" fallan al día siguiente por una razón que no tiene nada que ver.
Con ella no se toca ni la caché ni el timer, así que una corrida de prueba no deja nada.

Y el arnés gráfico:

```bash
timeout 100 xvfb-run -a -s "-screen 0 1280x800x24" dbus-run-session -- bash -c '
  env -u WAYLAND_DISPLAY XDG_DATA_HOME=/tmp/w-home \
      KDOCK_WEATHER_FIXTURE=$PWD/tests/fixtures/weather/corrientes.json \
      QT_QPA_PLATFORM=xcb QT_QUICK_BACKEND=software ./build/weather/kdock-weather --show \
      > /tmp/w.log 2>&1 &
  APP=$!; sleep 7; import -window root /tmp/weather.png; kill $APP'
```

Cuatro cosas:

- **`dbus-run-session` es obligatorio**, igual que con el menú de mosaicos y el panel: el
  candado de instancia única es el nombre en el bus, y `xvfb-run` hereda el del usuario. Sin
  eso el `--show` se le reenvía a la instancia real y **le abre la ventana en la pantalla**.
- **Enlazá `icons` dentro del `XDG_DATA_HOME` descartable** o los íconos del clima salen vacíos
  y parece un bug de render.
- **La ventana se puede manejar con `xdotool`** (bajo X los clics llegan): las dos solapas se
  prueban con un clic y una captura. Ojo con las coordenadas — la ventana la ubica el WM, así
  que leelas de la captura anterior en vez de fijarlas.
- **El clic del widget del dock también se prueba de punta a punta** bajo Xvfb: dock con
  `showWeather=true`, `xdotool click` sobre el ícono, y la ventana del clima aparece en la
  misma captura (verificado 2026-08-11). También necesita `dbus-run-session`, por lo mismo.

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
virtuales* de la solapa Docks se puede manejar desde código con `setCheckState()` y
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

**Y ojo con la otra mitad de eso**: como el arnés llega al bus real, **cualquier feature que
reaccione a `VirtualDesktops::currentChanged` actúa sobre la sesión de verdad desde adentro de
Xvfb**. Los wallpapers por escritorio le cambiarían el fondo al usuario en cada corrida; la
única reja es que su `enabled` vive en el `kdock.conf` compartido y **arranca en false**, así
que con un `XDG_DATA_HOME` descartable la clase es inerte. Si agregás algo parecido, dale el
mismo interruptor apagado por defecto en vez de confiar en el aislamiento.

**El widget paginador sí se prueba entero bajo Xvfb**, y de punta a punta, porque el clic va
por XTEST y el cambio de escritorio va por D-Bus al KWin **de verdad**: dock bajo Xvfb →
`xdotool click` sobre el número → `busctl get-property … current` confirma el cambio. Anotá
el escritorio original y restauralo, porque le estás cambiando el escritorio al usuario. La
misma corrida sirve para ver que el resaltado sigue al escritorio actual, capturando el dock
después del clic.

**Los menús contextuales también se abren con `xdotool` bajo Xvfb** (clic derecho sobre el
ícono, y el submenú con un clic sobre su fila), y `import -window root` los agarra porque son
`Popup.Window`. Es la única forma de ver que un `Menu` no le está cortando la última letra al
ítem más ancho. Pero ojo con la trampa: bajo Xvfb **el dock no tiene ninguna ventana** (no hay
plasma-window en X11, `WindowMonitor::create()` devuelve nullptr), así que todo ítem con
`enabled: delegateRoot.windowCount > 0` sale gris y parece roto. Para verlo, abrí la reja a
propósito (`enabled: true`), capturá, y **revertí** — control positivo barato, y el `grep -c
TEMP` te avisa si te olvidaste de volver atrás.

**Una sonda que construye un `DockManager` con el `Shared` vacío se cae si el dock que crea
cae en un monitor CONECTADO.** `sync()` arma un `DockWindow` de verdad con todos los servicios
en `nullptr` y el proceso se va en segmentation fault, que parece un bug del código que estás
probando. Dos rejas, las dos hacen falta: sembrá el `kdock.conf` con `enabledScreens=` de una
pantalla **que no existe** (si no, `migrateFirstRun()` habilita la primaria y la sonda ni
arranca) y usá nombres de monitor inventados (`VIRT-1`, `VIRT-9`) para todo lo que la sonda
cree. Con eso se ejercita `DockManager` entero —crear, duplicar, atar a escritorios, la solapa
Docks— sin instanciar una sola ventana. Y si además le pasás un `VirtualDesktops` en el
`Shared`, la sonda ve los escritorios **reales** por D-Bus (`currentDesktop()` devuelve el del
usuario), así que se puede probar la lógica que depende del escritorio actual sin tocar la
sesión.

**Nada que dependa de `connectedScreens()` se puede probar acá, y hay dos formas de sortearlo**
(2026-08-09, probando *Copiar a Sig. Monitor*). Esa lista sale de `QGuiApplication::screens()`,
o sea de la plataforma: bajo Xvfb hay **una sola** pantalla (`screen`) y `xrandr --setmonitor`
**no sirve** —este Xvfb no implementa monitores de RandR 1.5, el comando sale con 0 y
`--listmonitors` sigue mostrando uno—, así que un move/copy "al siguiente monitor" corta
temprano y la sonda no prueba nada. Lo que funcionó: un parche **temporal** de una línea en
`connectedScreens()` que devuelva la lista de una variable de entorno, correr la sonda, y
revertirlo (mismo método que el `qInfo()` del dodge). Y la segunda reja: con la lista falsa,
los docks que la sonda crea caen en monitores "conectados" y `sync()` intenta armarles ventana
→ **segfault**. Atalos a un escritorio virtual que no sea el actual (`setDockDesktops({2})` con
un `Shared` sin `VirtualDesktops`, donde `currentDesktop()` es 0): `wantedDocks()` los deja
afuera y no se instancia ninguna ventana.

**Y la sonda de `DockManager` miente sobre los archivos en la PRIMERA corrida**: `QSettings`
escribe diferido, así que un `QFile::exists()` sobre el `.conf` de un dock que la propia sonda
acaba de crear da **false**, y el `QFile::rename()` a `.conf.tmp` del move no renombra nada
—sin fallar—. Los archivos aparecen recién cuando los `DockConfig` se destruyen al salir. O
sea que una aserción sobre archivos hay que hacerla en una **segunda** corrida sobre el mismo
`XDG_DATA_HOME` (ahí sí: `.conf` intacto en la copia, `.conf.tmp` en el move). No es un bug del
código: en la app real el archivo existe desde que el dock se creó.

**Y ojo con el `XDG_DATA_HOME` descartable entre corridas de la misma sonda**: los docks que
creó la corrida anterior siguen ahí, así que la siguiente empieza en otro slot y cualquier
`assert` sobre "Dock 2" falla por una razón que no tiene nada que ver. `rm -rf` del directorio
en cada corrida (me pasó, 2026-08-06).

### Probar un cambio de fondo: la config miente, hay que mirar la pantalla

**Que la clave cambie no quiere decir que el escritorio cambie**, y es exactamente el modo en
que este bug se esconde (2026-08-10, *"el fondo cambia una vez y después ya no"*): el motor
escribía la clave `Image` del slideshow, la releía distinta en cada clic y **el fondo no se
movía**. Una verificación que lea el `.conf` o el `readConfig` da verde sobre una feature
completamente rota.

La medición son tres comandos, y ninguno necesita cambiar de escritorio ni cerrar ventanas
(el escritorio está tapado, pero el fondo visible alcanza y sobra si contás píxeles):

```bash
spectacle-custom -b -n -f -o /tmp/a0.png     # (el `spectacle` de /usr/bin no está autorizado)
<la acción>
spectacle-custom -b -n -f -o /tmp/a1.png
python3 -c "
from PIL import Image
a=Image.open('/tmp/a0.png').convert('RGB').crop(BOX); b=Image.open('/tmp/a1.png').convert('RGB').crop(BOX)
print(sum(1 for p,q in zip(a.getdata(),b.getdata()) if p!=q))"
```

`BOX` es la geometría del monitor dentro de la captura, y **el mapeo conector → geometría sale
de una sonda Qt de diez líneas** (`QGuiApplication::screens()`, `QT_QPA_PLATFORM=wayland`), no
de `kscreen-doctor`, que en esta sesión no imprime nada. Los números se leen solos: un repintado
son ~2,03 M de los 2,07 M píxeles de un 1920x1080; el fondo **sin** cambiar da ~31 k, que es el
reloj del dock. Los índices de Plasma (`d.screen`) son los de `screenGeometry(i)`, así que el
mismo mapeo sirve para el JS.

Y dos cosas del dominio que costaron todo el diagnóstico:

- **Los dos plugins de fondo se comportan al revés.** `org.kde.slideshow` **ignora** un `Image`
  escrito + `reloadConfig()` (gestiona su propia rotación): lo único que lo hace avanzar es
  ciclar el plugin —a `org.kde.image` y, en una llamada **separada**, de vuelta— sin
  `reloadConfig()` en la ida, o repinta antes con la imagen vieja del otro grupo y se ve un
  fondo de más. `org.kde.image` es lo contrario: respeta `writeConfig("Image")` +
  `reloadConfig()` y repinta. Los dos flips dentro de **un** script no repintan nada.
- **`busctl` no sirve acá y `qdbus6` sí**: el servicio es `org.kde.plasmashell` (minúscula) en
  `/PlasmaShell`, y `busctl` te devuelve la salida con los `\n` escapados, ilegible para un
  volcado de varias líneas.

**Probar el backend sin UI son 20 líneas**: `moc` sobre `wallpapercontrol.h`, `g++` con
`wallpapercontrol.cpp` y `Qt6Gui Qt6DBus`, un `main` que llame al método y un `singleShot` de
4 s para que las llamadas encadenadas lleguen (son asíncronas: salir antes deja el trabajo a
medias). Con `QT_QPA_PLATFORM=wayland` ve los monitores reales.

**Y esto le cambia el escritorio al usuario de verdad**: no hay caja de arena posible, porque
la feature *es* escribirle a plasmashell. Anotá el `Image` de cada containment **antes** de
empezar y devolvelos al terminar — un `readConfig` por pantalla y un `writeConfig` +
`reloadConfig` de vuelta.

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

**Variante de medición, sin ventana**: para verificar *geometría* (un tamaño mínimo que un
control debe honrar, un espaciado, un ancho natural) no hace falta capturar nada — basta
instanciar el `.qml` suelto con context properties falsas (`QQuickItem *btn = …comp.create()`,
`setProperty()` para los props) e imprimir `width`/`height` con cada valor configurado.
Es como se verificó que `CmButton` cumple `cmConfig.buttonWidth`/`buttonHeight` (90×34
natural → 170×48 configurado → vuelve a natural con 0, 2026-08-08): determinístico, sin
render y sin depender de leer una captura. Los objetos falsos van con `Q_PROPERTY` + `moc`
(`/usr/lib/qt6/libexec/moc archivo.cpp -o probe.moc`, o el `moc` del PATH — el de 5.15
rompe el include).

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

## Traducciones (`translations/*.md`)

Los textos del código son la capa nativa **capabase**; encima va un `.md` por idioma. El
detalle está en `AGENTS.md` → *Capa de traducciones*. Lo que hay que saber para tocar código:

- **Toda cadena nueva en `tr()`/`qsTr()` queda fuera de las traducciones hasta que
  regeneres el catálogo.** No falla ni avisa: simplemente sale en capabase. Después de
  agregar textos:

  ```bash
  python3 tools/gen-capabase.py        # relee el código con lupdate -> capabase.md
  python3 tools/sync-translations.py   # propaga las claves nuevas a english/spanish
  ```

  El segundo **conserva** lo ya traducido y te dice cuántas entradas siguen iguales a
  capabase (o sea, cuánto falta traducir). Las dos herramientas son idempotentes.
- **`defaultWidgetLabel()` no lleva `tr()`** a propósito: esa tabla *es* capabase, y las
  traducciones se indexan por token en la sección `Widgets`. Si le ponés `tr()`, el nombre del
  widget queda traducido dos veces por dos caminos distintos.
- **Los tres `.md` están en el qrc** (`qt_add_resources(kdock "translations")`): un archivo
  nuevo hay que agregarlo ahí, igual que un `.qml`.
- **`Translations` se instancia primero en `main.cpp`**, antes que cualquier otro singleton:
  instala el `QTranslator`, y lo que se haya construido antes ya tiene sus cadenas en capabase.
- **Las cadenas de `kdock-previews`, `kdock-tilemenu` y `kdock-controlmanager` están en el
  mismo catálogo** (el `gen-capabase.py` escanea los cuatro árboles, y para el panel de control
  **dos** directorios de QML: `qml/` y `qml/cards/`), y los tres binarios cargan **la base** del
  idioma que eligió el dock: `english-ALT-hacker` les da `english`. Si agregás una cadena en
  un accesorio, se regenera igual que las del dock.
- **Trampa del arnés: un `XDG_DATA_HOME` de prueba reusado esconde las traducciones nuevas.**
  `seedFiles()` copia del qrc **solo lo que falta** y nunca pisa un archivo existente — que es
  lo correcto para el usuario, porque esos archivos son suyos. Pero en una corrida de prueba
  significa que seguís leyendo el `.md` que sembró la corrida anterior, con lo cual una
  traducción recién agregada "no aparece" y parece un bug del código. `rm -rf
  <XDG_DATA_HOME>/kdock/translations` entre corridas (me pasó, 2026-08-07: las categorías del
  menú salían en inglés con `language=spanish` y estaban perfectamente traducidas en el repo).
- **Y esa misma trampa mordía de verdad, no solo al arnés: un `.md` de usuario sembrado ANTES
  de una feature nueva se queda sin sus claves para siempre.** `seedFiles()` copia del qrc
  "solo lo que falta" a nivel de *archivo completo* — si `english.md` ya existe, ni una línea
  nueva entra, aunque el catálogo del binario haya crecido. Costó un bug real (2026-08-07,
  reportado por el usuario): los paneles de `kdock-previews` y `kdock-tilemenu` no traducían
  nada, pese a que el catálogo del repo sí tenía sus cadenas — el `.md` de
  `~/.local/share/kdock/translations/` databa de antes de que existieran esos paneles y nunca
  se actualizó. Arreglado agregando un merge por clave (`mergeSection()`, en
  `src/translations.cpp`): en cada arranque, para cada archivo que YA existe, copia adentro
  las entradas de `Configuracion`/`UIdock`/`Widgets` que el qrc tiene y el archivo no —
  dejando intacto todo lo demás (`Apps` no se toca ahí, es dominio de *Actualizar apps*). O
  sea que a partir de ahora un catálogo de usuario se pone al día solo, sin perder ediciones.
  De paso: el mismo patrón (`QDialogButtonBox::Close` sin `setText()`) que el diálogo
  principal ya sorteaba (`src/settingsdialog.cpp`, comentario ahí) estaba sin aplicar en
  `previews/src/previewsettingsdialog.cpp` y `tilemenu/src/tilesettingsdialog.cpp`: el botón
  *Cerrar* salía en el locale del sistema en vez de en el idioma elegido, aun con el catálogo
  al día. Un `QDialogButtonBox` con botones estándar SIEMPRE necesita ese `setText(tr(...))`
  por cada instancia — no es algo que se herede.
- **Probar un idioma no necesita GUI**: sembrá `language=spanish` en el `kdock.conf` de
  prueba y corré el arnés de Xvfb; para el diálogo, la sonda que linkea los `.o` imprime los
  títulos de las solapas en cada idioma (`capabase` → "Layout", `spanish` → "Diseño"), que es
  la prueba de que el `QTranslator` llega a Qt Widgets. Para el QML, el clic derecho con
  `xdotool` sobre un ícono muestra el menú traducido.

**Dos trampas que aparecieron traduciendo, las dos son de layout y estaban latentes:**

- **`TextMetrics.width` puede quedar 1-2 px por debajo de `Text.implicitWidth`** para la misma
  cadena y la misma fuente, así que una caja de nombre construida con ese valor **elide un
  nombre que entra** ("Reloj" dibujado como "Re…", medido 2026-08-07). Con `advanceWidth` los
  dos números coinciden exacto. `Dock.qml` mide **siempre** con `advanceWidth` (los dos
  `labelW` y las dos sondas de `measureLabels()`); si agregás otra medición de texto, copiá eso.
- **El `+ 16` de la fórmula `width: Math.max(implicitWidth + 16, N)` de los `Menu` no alcanza**
  cuando la etiqueta se alarga al traducirla: en español seguía cortando la última letra. Ahora
  todos los menús del dock usan **`+ 64`**, medido. Un menú nuevo va con esa holgura.

## Convenciones

- C++17, Qt 6 (≥ 6.5), `#pragma once`, `QStringLiteral`, `connect` con punteros a miembro,
  `Q_INVOKABLE` para lo que llame QML.
- QML: solo `QtQuick` + `QtQuick.Controls.Basic` (nada que dependa de estilos de KDE).
- Comentarios en inglés, en el estilo del archivo que estés tocando: explican *por qué*,
  no *qué* — la densidad actual es la referencia.
- Nada de KDE Frameworks como dependencia: los backends hablan D-Bus / CLI directo
  (UDisks2, NetworkManager, UPower, `pactl`, `wpctl`, `brightnessctl`, kglobalaccel).

### Probar ColorAuto (esquema de color desde el wallpaper)

El motor (`src/wallpapercolors.cpp`) es **puro** y se prueba entero en CI:
`tests/unit/tst_wallpapercolors.cpp` genera sus propias imágenes con `QImage` en vez de
traer PNG de fixture —el test afirma sobre el color que sale, así que un blob binario
escondería justo el dato— y verifica el color dominante, la decisión claro/oscuro y que
**todas** las razones de contraste generadas cumplan su objetivo sobre cuatro imágenes por
tres claridades. `tests/unit/tst_autocolorscheme.cpp` cubre la máquina de estados sin D-Bus.

Lo que esos dos no pueden probar, y cómo se probó:

- **Que la solapa funcione**: la sonda que linkea los `.o` de `kdock_core.dir` (receta de más
  arriba), con `XDG_DATA_HOME` y `XDG_CONFIG_HOME` descartables **y el `PATH` falso**, que acá
  no es opcional: la solapa aplica esquemas e iconsets de verdad. Desambiguá los controles
  **por el `QGroupBox` que los contiene**; el interruptor maestro es el único `QCheckBox` que
  no está dentro de ninguno. Con `enabledScreens=` de una pantalla inexistente el
  `DockManager` no arma ninguna ventana y la sonda no necesita ningún backend real.
- **Que el dock se pinte de verdad**: el arnés de Xvfb con `enabled=true`, `colorDocks=true` y
  **`systemScheme=false`** en el `[ColorAuto]` del `.conf` de prueba, y el `import -window` de
  siempre. El control es correr dos veces, con la feature prendida y apagada, y comparar el
  píxel del panel: acá dio `(28,32,34)` contra `(203,204,205)`. Una sola corrida no prueba
  nada — un dock oscuro puede serlo por el tema.
- **Ojo con que el arnés llega al bus real.** Sin `dbus-run-session`, un kdock (o una sonda)
  bajo Xvfb le lee los wallpapers al plasmashell **de verdad** y genera el esquema con ellos.
  Es inofensivo porque solo lee, y el `.colors` cae en el `XDG_DATA_HOME` descartable, pero
  significa que la corrida **depende del fondo que el usuario tenga puesto**: no afirmes sobre
  un color concreto, afirmá sobre "cambió respecto de la corrida con la feature apagada".
- **Comprobá que no le tocaste el escritorio al usuario**: `grep -m1 "^ColorScheme="
  ~/.kde-opt/config/kdeglobals` y `ls ~/.local/share/color-schemes/ | grep -i kdock` (tiene que
  salir vacío). El `PATH` falso es lo que lo garantiza.
- **El watcher del wallpaper solo se prueba en la sesión real**, y la prueba es una segunda
  instancia aislada (`XDG_DATA_HOME` descartable, `autohide=true` para no moverle ninguna
  ventana al usuario, `PATH` falso) más **el script del usuario de verdad**. Lo que se mira no
  es el color sino el **ping-pong de los dos `.colors`**: si el archivo pasó de
  `KdockColorAuto1` a `KdockColorAuto2`, el watcher disparó y hubo una aplicación nueva.
  `grep colorscheme /tmp/fakebin/calls.log` lo cuenta de una: tres líneas donde antes había
  una. Acordate de que esa instancia **no puede tomar `org.kdock.Dock`** (lo tiene el dock
  real), así que la tarjeta del panel no se prueba por ahí.
- **Una sonda de `generateNow()` con `QT_QPA_PLATFORM=offscreen` no hace nada y no falla.**
  El mapeo containment → conector sale de `QGuiApplication::screens()`, y offscreen no le pone
  nombre a la pantalla: `imageByScreen` queda vacío y `applyPalettes()` vuelve sin escribir
  nada. Se ve igual que "la generación está rota". Bajo Xvfb con `xcb` la pantalla se llama
  `screen` y está en 0,0, que matchea el containment del monitor principal.

## Trampas que muerden

- **Vigilar un DIRECTORIO donde uno mismo escribe es un bucle garantizado** (2026-08-14,
  reportado como *"flickeo en la solapa Widgets del diálogo"* + el sistema pesado). `AutoColorScheme`
  vigilaba `plasma-org.kde.plasma.desktop-appletsrc` **y su directorio** —que es `~/.config`— y
  refrescaba con cualquier evento; su propio `applySystem()` lanza `plasma-apply-colorscheme`,
  que escribe `kdeglobals` ahí adentro. Resultado: un cambio de esquema de color **cada ~2 s,
  indefinidamente** (el ping-pong `KdockColorAuto1/2` garantiza que ninguna vuelta sea no-op).
  El watch del directorio hace falta —es lo que recupera el inodo tras el `rename` de KConfig—,
  así que la cura es que el evento **re-arme y nada más**: el disparador es una marca
  `(mtime, size)` del archivo propio (`appletsrcChanged()`), más una guarda de idempotencia en
  `applyPalettes()` que no re-aplica el mismo resultado.
  **Diagnóstico en diez segundos**, y sirve para cualquier sospecha de bucle de apariencia:
  ```bash
  for i in $(seq 10); do ls --time-style=+%H:%M:%S -l ~/.local/share/color-schemes/KdockColorAuto*.colors \
      ~/.kde-opt/config/kdeglobals | awk '{print $6,$7}'; echo ---; sleep 2; done
  ```
  Si los mtimes avanzan solos, hay bucle. Y para leer el `appletsrc` sin sacar conclusiones de
  más: **contá los containments, no los grupos**. `grep -c '^\[Containments\]\[[0-9]*\]$'` da
  los de verdad (acá **6**: cuatro escritorios y dos paneles); `grep -c '^\['` da 76, y llamarle
  a eso "75 containments" fue lo que mandó el primer diagnóstico a culpar a la cantidad de
  applets. Dos de los cuatro escritorios son de monitores desconectados (`d.screen == -1`) y el
  script de ColorAuto los saltea, así que su lectura toca **dos**.
  **Y ojo con el síntoma**: lo que el usuario ve no es
  "el color cambia" (los dos esquemas son casi iguales) sino **el diálogo de Configuración
  repintándose**, porque `Theme` vigila `kdeglobals` y llama a `QApplication::setPalette()`. Se
  lee como un bug de layout de la solapa que esté abierta.
  Se reproduce y se verifica **sin tocar la sesión del usuario**: el watcher sale de
  `QStandardPaths::GenericConfigLocation`, o sea que con un `XDG_CONFIG_HOME` descartable el
  directorio vigilado es tuyo. `appletsrc` falso adentro, kdock del build bajo Xvfb (**sin**
  `dbus-run-session`: la lectura del fondo necesita el plasmashell real, y solo lee) y el `PATH`
  falso para que `plasma-apply-colorscheme` no le toque el escritorio a nadie. Cinco escrituras
  del `kdeglobals` falso tienen que dar **cero** aplicaciones nuevas en `calls.log`, y un cambio
  real del `appletsrc` con otra opción, exactamente una.
- **`QZipWriter` AGREGA al archivo que encuentra, no lo pisa** (2026-08-13). Guardar dos veces
  el mismo preset (`ConfigArchive::savePreset()`) deja las dos copias de cada `.conf` adentro
  del `.zip`, y el import restaura **la primera**, o sea la vieja: se lee como "sobrescribir el
  preset no hace nada". Por eso `savePreset()` hace `QFile::remove()` antes de exportar. Lo
  cubre `tests/unit/tst_configarchive.cpp`.
- **Un archivo que se exporta y el import descarta no avisa de nada** (2026-08-13). Las dos
  puntas de `ConfigArchive` son listas distintas —`configGlobs()` para escribir e
  `isConfigEntry()` para leer— y `controlmanager.conf` estaba solo en la primera: viajaba en el
  `.zip` y se perdía al restaurar, con el panel de control quedándose con la configuración
  vieja. Si agregás una familia de `.conf`, tocá **las dos**.
- **No apliques una configuración desde el proceso que la está usando** (2026-08-13). `QSettings`
  batchea las escrituras y las suelta en su destructor, así que copiar los `.conf` y después
  salir deja que los `DockConfig` vivos escriban sus claves sucias **encima** de lo que acabás de
  instalar. La solapa Presets no los toca: `DockConfig::syncAll()` y después
  `kdock::restartAll({"--apply-preset", <zip>})`, y el `importFrom()` lo hace el proceso
  **nuevo** en `main()` antes de la primera lectura de config. Y `restartAll()` **borra** ese
  par de la línea de comandos heredada, o cada *Dock → Reiniciar* posterior volvería a aplicar
  el preset y se comería todo lo que el usuario cambió desde entonces.
  Se prueba entero bajo Xvfb en diez segundos, sin GUI: un `.zip` armado con `python3 -m
  zipfile` (o a mano con `zipfile`) que traiga un `kdock.conf` con una clave marcadora,
  `./build/kdock --apply-preset <zip>` con `XDG_DATA_HOME` descartable, y `grep marker` sobre
  el `.conf` después.
- **`plasma-apply-colorscheme` no hace NADA si el nombre pedido ya es el que está puesto**
  (2026-08-12). Está en su fuente (`kcms/colors/plasma-apply-colorscheme.cpp`): compara contra
  el `ColorScheme` de kdeglobals, imprime *"ya está aplicado"* y vuelve con éxito. O sea que
  reescribir un `.colors` en el mismo archivo y volver a aplicarlo es un **no-op silencioso** —
  la salida dice que salió bien. Cualquier feature que genere un esquema tiene que **alternar
  entre dos nombres**; es lo que hace ColorAuto (`KdockColorAuto1`/`2`) y lo mismo que hace
  kde-material-you-colors, cuyo quirk documentado ("crea dos esquemas y los aplica uno tras
  otro") es exactamente este.
- **`QColor` guarda más de 8 bits por canal y `operator==` compara ESO, no `red()/green()/blue()`**
  (2026-08-12). Un color salido de `QColor::fromHsv()` y el mismo color escrito como `"33,36,40"`
  y releído comparan **distinto** mientras se imprimen idénticos (`#ff212428` contra
  `#ff212428`); medido, 8627 contra 8738 en el canal rojo, los dos redondeando a `0x21`.
  `toRgb()` **no** alcanza (eso arregla el `spec`, no el valor). Si un color va a compararse
  contra otro leído de un archivo o de otra fuente, normalizalo reconstruyéndolo desde sus
  canales de 8 bits (`QColor(c.red(), c.green(), c.blue())`). Sin eso, un `if (nuevo == viejo)
  return;` no corta nunca y el objeto re-emite en cada refresco.
- **Una señal no puede llamarse igual que un método estático de la misma clase.**
  `DesktopWallpapers` ya tenía `static bool applied()`, así que agregarle una señal
  `applied(int)` hace que `&DesktopWallpapers::applied` no resuelva: el error de `connect` son
  cinco candidatos de plantilla y no menciona la colisión. Se llama `wallpapersApplied(int)`.
- **`DockConfig::anyDarkModeActive()` recorre las instancias VIVAS de `DockConfig`.** Un test
  que setea `setDarkModeAllDocks(true)` + `setDarkModeGlobal(true)` sin construir ningún
  `DockConfig` obtiene `false`: el modo oscuro nunca "se activa" y todo lo que dependa de él
  da verde **sin haber probado nada**. Un `DockConfig cfg(QStringLiteral("VIRT-9"))` (un
  monitor inventado, que no crea ventana) es lo que hace falta.
- **Los triggers "en proceso" para un cambio de fondo no alcanzan, y el agujero es el caso
  normal** (2026-08-12). Cualquier feature que reaccione al wallpaper tiene que vigilar
  `plasma-org.kde.plasma.desktop-appletsrc`: los scripts del Script Runner hablan `qdbus6`
  derecho a plasmashell, el pase de diapositivas de KDE avanza solo y Preferencias del Sistema
  no consulta a nadie — kdock no participa en ninguno de los tres. **Y `kdock --next-wallpaper`
  tampoco sirve como aviso**: corre en un proceso aparte que emite su señal y sale, así que el
  dock que está corriendo nunca se entera. Costó entregar ColorAuto con la feature muerta en
  el flujo real del usuario.
- **`QFileSystemWatcher` suelta el watch cuando el archivo se reemplaza por `rename`.**
  KConfig (y casi todo lo que guarda de forma atómica) escribe un temporal y lo renombra
  encima, así que el inodo vigilado deja de existir: el watcher dispara **una sola vez** y
  después nunca más, sin un error. Hay que re-agregar el path en cada señal y vigilar también
  el directorio, porque entre el `unlink` y el `rename` el archivo no existe.
- **Cuidado con las respuestas asíncronas que llegan después de que el usuario canceló.** Un
  `evaluateScript` en vuelo cuando se apaga la casilla vuelve igual, y aplicar ahí pone el
  esquema **después** de que la restauración ya devolvió el del usuario: queda puesto justo lo
  que se acaba de apagar. La guarda va al principio del handler, releyendo el estado en vez de
  confiar en el que había cuando se lanzó la llamada.
- **Un override de tiempo de lectura tapa la opción del usuario, así que limpiarlo mal deja el
  control muerto** (2026-08-12, reportado). Los colores de ColorAuto se ponían recorriendo
  `enabledDocks()` y se sacaban recorriendo `knownDocks()`; en la config real esas listas
  diferían en **cuatro docks**, que quedaban con un color que después no se podía cambiar por
  ningún lado — el menú contextual y el diálogo escriben `panelColor`, y la rama del override
  va antes en el binding. **El conjunto que se limpia tiene que derivarse del que se pinta**:
  acá, los `DockConfig` que el manager repartió (`liveConfigs()`), porque los colores solo
  llegan por `configFor()`. Dos listas que "deberían" coincidir es lo que hay que evitar.
  Se comprueba con `grep -m1 '^enabledScreens=' ~/.local/share/kdock/kdock.conf` contra
  `grep -m1 '^knownDocks='`: no son la misma lista y nunca lo fueron.
- **Deshabilitar el cuerpo de una solapa detrás de su casilla maestra es una trampa cuando esos
  ajustes los usa alguien más.** En ColorAuto la casilla decide *cuándo* se regenera, pero los
  ajustes los lee también el botón manual, que anda con la casilla apagada: grisarlos dejaba la
  solapa inservible para quien solo quería el botón. Antes de gatear un `QWidget` contenedor,
  preguntá si esos controles tienen un segundo consumidor.
- **Una sección nueva del panel de control se toca en TRES lugares, y el que se olvida no da
  error** (2026-08-12): la fila en `CmSections::all()`, la entrada del `.qml` en
  `controlmanager/CMakeLists.txt`, y **el `case` en `CmSectionView.qml`**. Sin ese último la
  solapa aparece en la barra, se selecciona, y se abre **completamente vacía**: el `Loader` se
  queda sin `source` y no hay una sola línea en el log. Lo agarra
  `tests/static/check-cm-sections.py`, que cruza las tres puntas.
- **`QColor` guarda más de 8 bits, así que un contraste que "cumple" puede dejar de cumplir al
  escribirlo.** `ensureContrast()` alcanzaba exactamente 1.200 y el redondeo a 8 bits —que es
  lo que va al `.colors` y lo que el usuario ve— lo dejaba en 1.193, con el test del contrato
  fallando sobre un color que la propia función había dado por bueno. La medición tiene que
  hacerse **sobre la forma de 8 bits**, no sobre la interna.
- **Un fondo monocromático rompe cualquier "probá otro color" basado en el tono.** Medido: una
  captura de pantalla con **56 cubetas vívidas, todas en el tono 202°**. Dos errores se suman
  ahí — mirar solo las N cubetas más votadas para elegir candidatas (todas del mismo tono) y no
  tener plan B cuando la imagen de verdad tiene un solo color. El tope va sobre lo que **sale**,
  no sobre lo que se mira, y la lista se completa con armonías del dominante. Sin eso, ocho
  clics dan ocho veces el mismo esquema y se lee como "el botón dejó de funcionar" — y ojo,
  **el mecanismo sí anda**: el ping-pong de los dos `.colors` avanza y se aplica ocho veces. Si
  vas a diagnosticar esto, mirá el **color** que sale, no si hubo aplicación.
- **Los colores dominantes de una foto suelen ser el mismo tono**, y eso hace invisible
  cualquier "probá otro color" ingenuo. Las tres cubetas más votadas de un fondo real fueron
  (25,39,47), (71,116,143) y (51,87,109) — un solo azul en tres matices — así que un esquema
  construido con el *tono* daba fondos separados por un punto (`33,37,40` contra `33,38,40`):
  el botón parecía roto. Hay que descartar candidatas por **distancia de tono**, no por color.
  Y ojo con el modelo mental: la dominante es la más votada entre los píxeles **vívidos**, no
  el promedio — una foto de media cálida (146,93,63) puede tener como dominante un azul oscuro
  (1,22,48), porque lo cálido estaba desaturado y el portón lo descarta.

- **Un `import` de QML que falta no imprime NADA: el dock arranca con la ventana vacía**
  (2026-08-09). `Dock.qml` y `previews/qml/PreviewCard.qml` importan
  `Qt5Compat.GraphicalEffects` (paquete `qml6-module-qt5compat-graphicaleffects` en
  Debian/Ubuntu, `qt6-5compat` en Arch) para las sombras. Sin ese módulo el componente no
  compila, la `QQuickView` se queda en su tamaño por defecto de **160x160** y el proceso corre
  feliz **sin una sola línea en stderr** — ni con `QT_LOGGING_RULES`. Desde afuera se ve igual
  que un arnés que muestrea demasiado pronto, y por eso costó cuatro corridas de CI a ciegas.
  El que lo agarra en milisegundos es `tests/unit/tst_qmlload.cpp`, que instancia cada `.qml`
  del qrc con un `QQmlComponent` e imprime los errores del motor: **si tocás imports de QML,
  ese es el test que tiene que quedar verde**, no el smoke.
- **`... | grep -q` bajo `set -o pipefail` devuelve 141**, no 0: `grep -q` corta apenas
  matchea, el productor se muere con SIGPIPE y el pipeline "falla" **aunque el patrón esté**.
  Mordió en el chequeo de `--json` de qmllint, que se salteaba solo. Capturá la salida en una
  variable y después mirala.
- **`qmllint` a secas puede ser el de Qt5.** En esta máquina `/usr/bin/qmllint` es Qt 5.15:
  solo mira sintaxis y no conoce ni `--json`; el de Qt6 (`/usr/lib/qt6/bin/qmllint`) reporta
  1443 diagnósticos en el mismo árbol. Un "todo limpio" con el equivocado no significa nada.
  Resolvelas con `kdock_qt_tool` (`tests/lib/sandbox.sh`), que pregunta por `qmake6` **antes**
  de mirar el `PATH`.
- **Un arnés gráfico con `sleep` fijo miente en la máquina de otro.** Seis segundos alcanzan
  acá y no en un runner sin GPU, que arranca ~3× más lento; y muestrear temprano ve una ventana
  que existe pero que su QML todavía no dimensionó, o sea **exactamente el mismo síntoma que un
  QML roto**. Esperá por condición (`xvfb-app.sh --ready`), con el tiempo como presupuesto
  máximo. De paso el smoke bajó de 50 s a 18 s.

- **Un cliente Wayland no sabe dónde quedó su superficie, y para una centrada no se puede
  deducir** (2026-08-09): la posición de una layer surface anclada a un solo borde la elige el
  compositor **dentro de lo que le dejan libre las otras zonas exclusivas**, no dentro de la
  pantalla. Medido en la sesión real: un dock flotante centrado abajo cayó 92 px a la derecha
  del centro geométrico (había otro dock a la izquierda). `QScreen::availableGeometry()` **no
  ayuda**: bajo este KWin es idéntica a `geometry()` (sonda de diez líneas con `Qt6Gui`). Por eso
  `DockWindow::dockRect()` —la entrada del modo *intelligent hide*— solo es exacta para
  alineación Start/End y borde completo, y para Center usa la franja del borde entero a
  propósito. Si escribís algo que necesite la posición real de la superficie, no la calcules:
  cambiá el diseño para no necesitarla.
- **Un lanzador de PWA de Chromium cuya app ya no está instalada en el perfil es un no-op
  silencioso, y no es un bug de kdock** (2026-08-10, diagnosticado en una segunda máquina). El
  `.desktop` sobrevive a la desinstalación de la PWA. kdock ejecuta el `Exec`, el wrapper del
  navegador encuentra la instancia viva, le relaya la línea de comandos, imprime *"Abriendo en
  sesión de explorador existente"* y sale; el navegador **ignora un `--app-id` que no conoce**.
  Desde afuera se ve exactamente como *"kdock no lanza esta app"*.
  **Diagnóstico en dos minutos**: `journalctl --user | grep -i "sesión de explorador"` — si el
  mensaje aparece con un **pid hijo** de kdock (distinto del pid del dock), kdock ya hizo su
  parte y el problema es del navegador. Se confirma comparando el extId del `.desktop`
  (`[a-p]{32}`) contra `~/.config/<navegador>/Default/Web Applications/Manifest Resources/`,
  que es la lista de apps realmente instaladas en ese perfil.
  **Dos pistas falsas que cuestan horas, las dos mordieron:**
  - *"Desde el ícono anclado sí funciona, desde el menú no"* **no** significa que el camino del
    menú esté roto. `DockModel::launch()`, `AppMenu::launch()` y `TileWindow::launch()`
    terminan los tres en el mismo `DesktopEntryIndex::byId()` + `DesktopEntryIndex::launch()`:
    con el mismo id hacen lo mismo. Si uno anda y el otro no, es que **son `.desktop` distintos
    para la misma app** — varias PWA tienen dos o tres lanzadores (Edge, Chrome, y las copias
    hechas a mano con sufijo "ED") con nombres casi iguales en el menú, y lo que está anclado
    suele ser justo el que sí funciona. El caso real: anclado `msedge-hnpfjng…` "WhatsApp Web"
    (instalada, lanza) contra `msedge-ffllmad…` "WhatsApp" del menú (desinstalada, no hace
    nada).
  - El `.desktop` **no** es la diferencia: los archivos salieron **byte a byte idénticos** entre
    la máquina donde anda y la que no (`diff` sobre el mismo nombre). Lo que difiere es el
    **perfil del navegador**, que no se ve en ningún lado del dock.
- **En modo panel `root.width` NO cambia nunca**, así que cualquier cálculo colgado de
  `onWidthChanged` se congela después del arranque (2026-08-10). El contenido que llega tarde es
  la regla, no la excepción: los íconos de bandeja, las ventanas, la medición de nombres y las
  pasadas del auto-shrink mueven el layout **después** del primer frame. Los tramos de fondo de
  los separadores transparentes se calculaban así y quedaban con la geometría inicial: se veía
  como huecos pintados encima de íconos y —porque esa misma lista alimenta la máscara de
  entrada— como **clics que pasan de largo** por esos íconos, que parece un bug de la máscara y
  es un cálculo viejo. Si algo depende de dónde están las secciones, colgalo de la geometría
  (`onXChanged`/`onWidthChanged` en el delegate) y no de una lista de señales de config, que hay
  que acordarse de ampliar cada vez. El `Timer` de 16 ms de por medio hace que salga barato:
  coalesce el arranque entero en **un** cálculo, con la geometría ya asentada.
  Y se reproduce bajo Xvfb en una corrida, sin sesión real: config con dos `gap`, un
  `panelColor` bien visible (si no, el panel es negro sobre el negro de Xvfb y la captura no
  dice nada), `dockLength=45` para que el contenido **no entre** y el auto-shrink corra después
  del primer cálculo, y `python3` + `convert … txt:-` sobre una fila de la captura para listar
  los tramos pintados. Antes: `[(0,863)]` — un solo tramo, el dock "soldado". Después:
  `[(5,64) (92,353) (381,858)]`, los huecos justo donde están los separadores.
  Ojo con dos cosas del arnés: **bajo X no hay layer-shell**, así que `panelMode` no estira la
  ventana (sale de 77x77, o sea el grosor) y hay que usar `dockLength=100`; y una clave agregada
  al final de un `.conf` copiado cae dentro de la **última sección** (`[widgetNames]`), no en
  `[General]` — insertala después de la primera línea, y ojo si la clave ya existe más abajo,
  porque gana la última.
- **La franja que descubre un dock escondido está al borde de la SUPERFICIE, no de la pantalla**
  (2026-08-10). El margen de layer-shell (`screenMargin`, 4 px salvo en Compacto) separa la
  superficie del borde, así que los 3 px de la máscara de `applyHiddenMask()` caen adentro de la
  pantalla: el mouse en el borde no descubre nada y el dock aparece al *retirar* el puntero
  exactamente esos píxeles — se lee como un descubrimiento errático, no como un problema de
  geometría, y en Compacto no pasa (margen 0). Arreglado poniendo el margen del borde propio en 0
  mientras `m_hidden`; los laterales de alineación **no**, o el dock se corre a lo largo del
  borde al esconderse. Lo mismo mordía al arrancar: los `setHidden(true)` de las `Behavior` del
  slider solo corren al terminar una animación, y el primer valor escondido lo toma el binding
  **sin animar**, así que un dock que arranca oculto nunca se reportaba como oculto. Nada de esto
  se prueba bajo Xvfb (no hay layer-shell); lo que sí se comprueba ahí en diez segundos es que el
  `setHidden(true)` inicial ocurre, con un `qInfo()` temporal.
- **El modo *dodge* solo se prueba en la sesión Wayland real** (2026-08-09): bajo Xvfb no hay
  protocolo de ventanas, así que `windowsOverlap` queda en false y el dock sale siempre visible
  — la corrida limpia no prueba nada de la feature. La receta que funcionó: copiar el binario a
  un directorio **sin `#`**, un `.desktop` temporal con `X-KDE-Wayland-Interfaces` apuntando ahí
  + el `kbuildsycoca6` con ruta explícita, un `qInfo()` temporal en `updateWindowsOverlap()` que
  imprima rect/overlap/escritorio, y **cambiar de escritorio virtual** (`busctl set-property …
  current`) para ver el flanco: en el escritorio vacío `overlap` pasa a false y el dock vuelve.
  Acordate de borrar el `.desktop`, refrescar ksycoca de nuevo, volver al escritorio original y
  sacar el `qInfo()`. Y para medir la posición real, `spectacle-custom` + un recorte con una
  regla de coordenadas dibujada encima vale más que un diff de imágenes (el fondo cambia entre
  capturas y el bbox sale de pantalla completa).
- **Un `QDialog` de un cliente layer-shell se abre DEBAJO de su propio panel** (2026-08-11). Es
  el protocolo, no un problema de foco ni de `raise()`: la capa **top** está por encima de todos
  los toplevels, y un diálogo de Qt Widgets es un xdg toplevel común. En `kdock-controlmanager`
  eso tapaba el selector de color, el *Renombrar…*, el confirm y el propio diálogo de
  Configuración — se ve como "la app se colgó", porque el panel sigue arriba y el diálogo modal
  no deja hacer nada. La cura es una función de cuatro líneas (`showAsOverlay()` en
  `controlmanager/src/cmwindow.cpp`): `winId()` y las tres propiedades que la integración ya
  lee (`kdock.layershell`, `kdock.layer`=3, `kdock.keyboardInteractivity`=1). Tres cosas que
  hacen falta saber si agregás otro diálogo:
  - **Construí el widget, no llames a la estática.** `QColorDialog::getColor()` /
    `QInputDialog::getText()` / `QMessageBox::question()` muestran la ventana ellas mismas, y
    para entonces ya es tarde.
  - **Esto no se puede probar bajo Xvfb** (no hay layer-shell) **ni con `xdotool` en la sesión
    real** (XTEST no llega a un cliente Wayland nativo: los clics sobre el panel no hacen
    absolutamente nada y parece que la UI está muerta). Lo que funcionó fue un hook temporal en
    `controlmanager/src/main.cpp` —un `QTimer::singleShot` que llama al diálogo, detrás de un
    flag `--probe-dialog`— más `spectacle-custom`; y **acordate de sacarlo**.
  - **El teclado se verifica con `QGuiApplication::focusWindow()`**, no mirando la captura: si
    devuelve el `QWidgetWindow` del diálogo, el compositor le dio el `wl_keyboard`. El panel
    conserva su propio grab y no hay que quitárselo.
- **`left`/`top`/`right`/`bottom` son propiedades FINAL de `Item`** (2026-08-09): declarar
  `property bool left` en un componente QML propio es *"Cannot override FINAL property"* y
  **la carga de TODO el archivo falla** — `CmCornerGrip` tenía `left`/`top` y el panel de
  Control Manager quedó en blanco, con el diálogo de configuración abierto debajo de la
  superficie vacía (layer top), inalcanzable: parecía un cuelgue total de la app y era el QML
  que no cargaba. **`qmllint` no lo reporta** (es un chequeo en runtime del motor); la única
  reja es el log del proceso (`qrc:/qml/...: Type X unavailable` + `Cannot override FINAL
  property`). Nombrar `isLeft`/`isTop` y seguir.
- **Un control que se mueve con lo que arrastra no puede medir el arrastre contra sí mismo**
  (2026-08-10, los agarres de esquina del panel de control). El `MouseArea` daba coordenadas
  relativas al agarre, y el agarre está anclado a la esquina que redimensiona: cada resize lo
  desplaza, el evento siguiente lee ese desplazamiento como movimiento del puntero y la cosa se
  realimenta — el panel se iba al mínimo o al máximo en un puñado de eventos, con un arrastre
  que se siente errático. Va medido en coordenadas de **escena** (`mapToItem(null, m.x, m.y)`),
  que no se mueven con el hijo. Y en layer-shell hay una segunda mitad: la superficie **también
  se re-emplaza** al cambiar de tamaño (centrada, o anclada al borde opuesto), y el cliente no
  sabe dónde quedó. Esa parte no se mide: se deduce del ancla (`CmWindow::originShift()`) y se
  suma al delta. Corolario que vale para cualquier superficie anclada: **el borde clavado no
  puede seguir al puntero**, así que ese eje va 1:1 y no hay fórmula que lo arregle.
  Y el bug de al lado, del mismo día: el handler decía `grip.left`/`grip.top` cuando las
  propiedades se llaman `isLeft`/`isTop` — pero `left`/`top` **existen** en `Item` (son las
  anchor lines), son objetos, y por lo tanto siempre truthy: los dos ejes tomaban el signo
  invertido, sin un error ni una advertencia. Si renombrás una property por chocar con una FINAL
  de `Item`, `grep` por el nombre viejo.
- **KWin no deduce el borde exclusivo de un ancla de esquina** (2026-08-08): `LayerSurfaceV1Interface::exclusiveEdge()` (kwin `src/wayland/layershell_v1.cpp`, verificado en 6.6.6) solo devuelve un borde para ancla de **un solo borde** o de **un borde + las dos esquinas**. Una superficie anclada a un borde más UNA esquina (el caso de `dockLength>0` con alineación Start/End, y el flotante alineado a una esquina) no tiene strut: las ventanas maximizadas pasan por debajo del dock. kdock lo arregla por dos puntas: `applyLayerProperties()` ancla el **lado completo** cuando el dock cubre el borde entero (`dockLength==100` o panel 100%), y la integración envía el request v5 `set_exclusive_edge` (protocolo vendored actualizado a v5, versión del template a 5) para los docks parciales con esquina. Sin v5 (sway) los parciales con esquina siguen sin strut — por diseño, no es un bug de kdock. Diagnóstico: `qdbus6 org.kde.KWin /VirtualDesktopManager ...` + `kdock-previews --dump-captures` para leer la geometría de las maximizadas (la del dock derecho salía 1848x1080+72+0, la del superior pasaba por y=0).
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
- **El alto de la caja del nombre no es una línea, es `iconLabelBoxHeight()`** (`iconLabelLineHeight() * labelLines`, 2026-08-07): con *Dos renglones* prendido los nombres se envuelven y la caja mide el doble. Toda rama nueva de `cellThicknessFor()` tiene que usar esa función, no `iconLabelLineHeight()`, o el dock reserva una línea y dibuja dos. El espejo en QML es `root.labelBoxHeight`.
  Y ojo con el ancho: **una caja exactamente del ancho medido rompe la envoltura**. Qt tira la última letra al segundo renglón ("Clock" dibujado como "Cloc/k") aunque el texto entre; por eso `labelW` suma `root.labelSlack`, que es 1 px y **solo** con `labelLines > 1`. Se ve en una captura, no en el log.
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
  **Y son CUATRO puntas, no dos: cada delegate mide de nuevo, con su propio `TextMetrics`.**
  Las dos sondas del tope solo alimentan el grosor en C++; el **ancho de la caja de cada
  nombre** sale de `labelMetrics` (apps) y `secLabelMetrics` (widgets), dentro de los
  delegates. `labelBold` llegó a `secNameProbe` y no a `secLabelMetrics`, así que el nombre de
  un widget se **medía en redonda y se dibujaba en negrita**: con *Dos renglones* prendido la
  última letra se iba al segundo renglón ("Blade" dibujado como "Blad/e", "Config" como
  "Confi/g") y con un renglón se elidía. Los nombres de apps no se veían afectados porque
  `labelMetrics` mide **siempre** en negrita. Reportado por el usuario el 2026-08-08 con una
  captura; el grosor del dock no cambia, así que medirlo no lo encuentra — se ve en una captura
  o comparando `advanceWidth` con las dos fuentes. Si agregás otra opción de fuente, tocá las
  **cuatro**.
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
  `TileMenuLauncher`; el `controlmanager`, con `ControlManagerLauncher`) se salta
  `dockmanager.*`: el objeto es fino y sin estado, así que lo crea el propio `DockWindow` en su
  ctor —igual que `AppMenu`— en vez de viajar por `Shared`.
- **El scope de QML es por ARCHIVO, y un `Binding` dentro de un `Loader` es la carga.** Las dos
  mordieron al pasarle a cada tarjeta del panel de control su color de texto (2026-08-10). Un
  `.qml` que instancia un `Loader` **no** le presta sus properties al archivo cargado (los ids
  sí viajan por la cadena de contextos, las properties de la raíz no), así que un valor se pasa
  property por property y llega a `CmButton`/`CmSlider` solo si cada uso lo escribe: esos
  resuelven `theme` contra el contexto raíz, no contra la tarjeta que los declara. Y la default
  property de un `Loader` es `sourceComponent`, o sea que un `Binding { target: view.item … }`
  declarado en su cuerpo se lee como *lo que hay que cargar*: sale un **"Binding loop detected
  for property X" por cada ítem cargado** y el valor no llega. Va con `onLoaded` +
  `on<Prop>Changed`, como el `compact` que ya estaba.
- **`Component.onCompleted` (y cualquier handler) se declara UNA sola vez por objeto.** Un
  segundo `Component.onCompleted` en la raíz de `Dock.qml` no es un error de compilación ni una
  advertencia de `qmllint`: es *"Property value set multiple times"* al cargar, o sea **el QML
  entero no carga** y el dock sale de 160x160 con la ventana vacía. Si necesitás enganchar algo
  más al arranque, sumalo al handler que ya existe (`{ scheduleLabelMeasure(); scheduleGapRuns() }`).
  Lo agarró el arnés al primer intento, 2026-08-07.
- **Un modelo cuya entrada es la config de OTRA instancia tiene que reconstruirse con la señal
  ajena, no solo con la propia** (2026-08-13, el filtro *No ver apps ancladas en otros
  Seleccionables*). Cada `DockModel` de un appsel escucha `widgetAppsChanged(token)` y descarta
  los tokens que no son el suyo — que es lo correcto hasta que una opción hace que dependa de lo
  que los demás tengan anclado. Ahí hay que ampliar la guarda (y sumar `widgetOrderChanged`:
  agregar o quitar un widget cambia quiénes son "los otros"). El síntoma de olvidarlo es un ícono
  repetido que se arregla solo al reiniciar el dock, o sea que parece un problema de arranque.
- **Las ventanas se pueden falsificar en un test sin compositor**: `WindowMonitor` no es abstracto
  y `registerWindow()` es público, así que un `AbstractWindow` de tres métodos vacíos y un
  `WindowMonitor` en la pila alcanzan para ejercitar todo lo que el `DockModel` hace con ventanas
  —agrupar, filtrar, contar— en el tier `unit`. Sin eso, cualquier filtro que solo actúe sobre
  ventanas queda **sin probar** y el test da verde igual (con `monitor=nullptr` no hay ninguna).
  Está en `tests/unit/tst_appswidget.cpp`.
- **Un widget del que puede haber VARIOS por dock no se parece a los otros seis-archivos**
  (2026-08-13, *Apps Seleccionables*). Sus tokens se inventan en tiempo de ejecución
  (`appsel1`, `appsel2`…), así que no están en `knownWidgetTokens()` y `reconcileWidgetOrder()`
  los tira **sin imprimir nada** a menos que `isRepeatableToken()` los acepte; su configuración
  no puede ser un par de `Q_PROPERTY` porque hay una por instancia (va en un grupo propio del
  INI, `[appsel1] apps=…`); y su casillero no alcanza — se agrega y se quita desde la solapa
  Diseño, como un separador. Si copiás el patrón, mirá los cuatro lugares que no son obvios:
  `isRepeatableToken()`, el `sectionVisible`/`componentFor`/`isBlock` de `Dock.qml` (que hacen
  `switch` sobre el token y necesitan una rama por prefijo), `defaultWidgetLabel()` (el token
  pelado va en la tabla **solo** para que exista la clave del catálogo, y el número se pega
  aparte) y el panel de la solapa Widgets, que hay que reconstruir a mano al agregar o quitar
  uno porque cambiar de solapa no reconstruye el diálogo.
- **Un `Component` declarado en la raíz de `Dock.qml` no ve los ids del delegate que lo carga**
  (es el mismo scope por archivo de siempre): para parametrizarlo —qué modelo dibuja, en el caso
  de las Apps Seleccionables— se le empuja una property con un `Binding` desde el delegate de la
  sección, al lado del de `hovered`. Leer `sec.token` desde adentro del componente compila y
  queda `undefined`.
- **`QSettings` comparte un caché por ruta y lo revalida por (mtime, tamaño)**, así que un
  archivo reemplazado dentro del mismo segundo por otro **del mismo tamaño** se lee con el
  contenido viejo — sin error. En la app no muerde (el que aplica un preset es un proceso
  nuevo), pero un test que escribe, reemplaza y relee por `QSettings` falla una vez cada tantas
  corridas y parece un bug del import. En un test, leé el archivo (`QFile`) y no `QSettings`:
  `tests/unit/tst_configarchive.cpp` lo hace y lo explica.
- **Una sección que se repite (`spring`, `sep`, `gap`) NO va en `knownWidgetTokens()`**: esa lista
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
- **Un texto nuevo del diálogo o del QML no se traduce solo**: hay que regenerar el catálogo
  (`tools/gen-capabase.py` + `tools/sync-translations.py`, ver *Traducciones* arriba). No hay
  error ni advertencia — la cadena sale en capabase en todos los idiomas.
- **Un ícono de breeze que es line-art oscuro desaparece sobre un panel oscuro.** No es un
  bug de render ni un nombre equivocado: `brightness-high` y compañía se dibujan en negro y
  sobre el fondo del panel no se ven (pasó en la primera captura de la sección Video). El dock
  lo resuelve hace rato con dos iconsets (`widgetIconThemeDarkBg`/`widgetIconThemeLightBg`) y
  `root.widgetIconSuffix`; el panel de control usa el mismo par a través de `win.iconSuffix`.
  Regla para cualquier superficie nueva: **ninguna URL `image://icon` se arma con
  `"@" + theme.revision` a mano** — se usa el sufijo de esa superficie, que además elige el set.
- **Nuevo `.qml`**: agregalo a `qt_add_resources` o no entra al qrc. Hay **cuatro** listas: la
  del `CMakeLists.txt` de arriba (para `qml/`), la de `previews/CMakeLists.txt` (para
  `previews/qml/`), la de `tilemenu/CMakeLists.txt` (para `tilemenu/qml/`) y la de
  `controlmanager/CMakeLists.txt` (para `controlmanager/qml/` **y** `controlmanager/qml/cards/`,
  que son entradas sueltas: un `cards/*.qml` nuevo se agrega a mano). La de tilemenu tiene
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
- **En el protocolo de ventanas de Plasma, "en ningún escritorio" significa "en TODOS".**
  `AbstractWindow::desktops` (lo que llenan `virtual_desktop_entered`/`left`) llega **vacío**
  para una ventana anclada a todos los escritorios, que es exactamente al revés de lo que se
  lee. Tratarlo como "no está en ninguno" hace que mover esa ventana no deje nada y termine
  en dos escritorios. `moveToOnlyDesktop()` se apoya en eso: para una lista vacía manda solo
  el *enter* y KWin la reduce al destino.
- **Un widget nuevo cuyo backend ya existe se salta casi todo el checklist de siete
  archivos**, pero no `knownWidgetTokens()`. El `pager` reusa `VirtualDesktops` (ya viaja en
  `Shared`), así que solo hizo falta: la context property en `dockwindow.cpp`, el flag en
  `DockConfig` (+ token + etiqueta), el `Component` en `Dock.qml` y el casillero del diálogo.
  Y si el widget tiene **varios blancos de clic adentro** (como el pager o la bandeja), va
  también en **`isBlock()`**: eso apaga el `MouseArea` de la sección —que si no se come los
  clics de los hijos— y lo excluye del `Binding` de `hovered`, que sobre un componente que no
  declara esa property escupe un warning en cada arranque. El precio es que un bloque no se
  arrastra como unidad, igual que la bandeja.
- **`SIGTERM` no dispara `aboutToQuit`, y arreglarlo mal te inunda el journal.** Qt no instala
  handler para `SIGTERM`, así que un cierre de sesión (o un `kill` pelado) mata el proceso sin
  emitir nunca `aboutToQuit` — el hook desde el que `DesktopWallpapers` devuelve el fondo de
  KDE. Se destapó en el arnés en vivo: matar la instancia en el escritorio 2 dejaba **nuestra**
  imagen puesta (2026-08-06). Pero la corrección obvia —handler → `qApp->quit()`— cambia el
  cierre de golpe por un desenrollado completo, y destruir los `QQmlEngine` con los bindings
  vivos escupe **~1793 líneas** de `TypeError: Cannot read property '…' of null` (15 → 1808
  líneas de stderr, medido). O sea que "arreglé el SIGTERM" te deja el journal ilegible en
  cada logout. El handler de `main.cpp` (socketpair + `QSocketNotifier`, lo async-signal-safe
  es escribir un byte) hace lo mínimo y sale con **`::_exit(0)` sin desenrollar**, que es
  exactamente como moría antes. Si agregás algo al cierre, va en ese callback, no en un
  `quit()` limpio.
- **Un fondo de escritorio solo se puede *ver* desde un escritorio virtual vacío.** El
  containment del escritorio **no es un toplevel**: no aparece en
  `kdock-previews --dump-captures` y no hay forma de capturarlo solo. Una captura de pantalla
  del escritorio de trabajo sale tapada por las ventanas del usuario y parece que el fondo "no
  cambió". La receta que funcionó (2026-08-06): anotar el escritorio actual, `switchTo(3)`
  (que está vacío), `spectacle-custom -b -n -f -o …` entre paso y paso, y **volver al
  original**. Sin eso no se puede distinguir "escribió bien la config" de "repintó", que son
  dos cosas distintas — la config se verifica leyendo `evaluateScript`, el repintado no.
- **Un `QProcess` de larga vida sobrevive al `::_exit(0)` del SIGTERM.** El handler de cierre
  sale sin desenrollar a propósito (ver arriba), así que el destructor de `QProcess` —lo único
  que mata al hijo— nunca corre: cada logout o `kill` dejaba dos `pactl subscribe` huérfanos,
  reparentados a systemd y vivos para siempre (44 acumulados, reportado 2026-08-08). Y no se
  mueren solos: el dock ignora `SIGPIPE` process-wide por el portapapeles y las disposiciones
  se heredan por `exec` (aunque en este caso da igual, `pactl` ya lo ignora por su cuenta).
  Todo hijo que tenga que vivir mientras vive el dock va con **`kdock::tieToParent()`**
  (`src/childprocess.h`, `PR_SET_PDEATHSIG`), que también cubre el `SIGKILL` y el crash.
  Se comprueba en el arnés en diez segundos: `ps --ppid <pid>` antes, `kill -TERM`, `pgrep -a
  pactl` después.
- **Y el reflejo simétrico: un hijo de larga vida que se muere no se reintenta solo.**
  `pactl subscribe` sale cuando el servidor de audio lo suelta (reconectar una placa de audio
  alcanza) y el widget se queda sordo **sin avisar** —sigue mostrando lo que adivinaron sus
  escrituras optimistas—, que es justo el bug que parece "el volumen a veces no se actualiza".
  El relanzamiento va con backoff (1 s → 30 s, reseteado si el anterior duró más de 10 s) o un
  servidor caído te deja un fork loop, y el resync posterior va **condicionado a que el hijo
  siga vivo**: un `refresh()` son media docena de `pactl`. Para probar el peor caso, un `pactl`
  falso de dos líneas en el `PATH` que loguee y salga con 1 (receta de las herramientas falsas,
  más arriba): los timestamps muestran el backoff y el conteo muestra la amplificación.
- **Una cadena con `" = "` adentro se parte mal en el catálogo de traducciones.** El formato
  de los `.md` es `clave = texto` y separa por el **primer** `" = "`, así que un
  `tr("vacío = la fecha y la hora")` entra como clave `vacío` con valor `la fecha y la hora`:
  no falla, no avisa, y esa cadena **no se puede traducir nunca**. Se destapó escribiendo el
  panel de control (2026-08-08) y ya estaba latente en el tooltip de *Dock length*
  (`"0 = auto (panel stretches…"`, `src/settingsdialog.cpp`), que por eso sigue en capabase en
  todos los idiomas. Escribí `vacío: la fecha y la hora` y listo.
- **`Window.window` se lee desde un `Item`, no desde adentro de un `Timer`.** La property
  adjunta se resuelve sobre el objeto donde se la usa, así que un
  `running: Window.window.visible` dentro de un `Timer` escupe *"Window.window does only
  support types deriving from Item"* y el timer **no corre**: el reloj de la tarjeta se queda
  congelado y parece un bug del backend. Leelo en el `Item` raíz
  (`readonly property bool panelVisible: Window.window ? Window.window.visible : true`) y usá
  eso. Lo agarró el arnés al primer intento (2026-08-08).
- **Un `QDBusArgument` local va `const` — y en `BatteryControl` no lo estaba.** Leyendo
  `Profiles` (`aa{sv}`) de power-profiles-daemon el objeto era no-const, así que el compilador
  elegía las sobrecargas de **escritura** de `beginArray()`, y cada refresco imprimía
  `QDBusArgument: write from a read-only object` — el paso previo al abort de libdbus que ya
  está documentado más abajo. Estaba ahí desde siempre y lo destapó una sonda de aislamiento de
  30 líneas que construye un backend por vez e imprime un marcador entre uno y otro
  (2026-08-08). Si ves ese mensaje en cualquier arnés, no lo ignores: buscá **cuál** de los
  backends lo emite antes de suponer que es del código que estás tocando.
- **Un widget que se esconde cuando no está configurado se queda sin la puerta para
  configurarlo** (2026-08-11, reportado por el usuario). El widget del clima salía de
  `sectionVisible()` mientras no hubiera ciudad, y la única entrada a su configuración es el
  menú de su propio clic derecho: sin ícono en el dock no hay dónde hacer clic, y desde afuera
  se ve como "el widget no funciona". La regla: un widget cuya configuración vive **en su
  propio menú** se dibuja siempre —con un ícono neutro y sin datos— y son sus *ítems* los que
  se deshabilitan. Y ojo con el ícono neutro: `weather-none-available` es lo literal, pero
  varios iconsets lo dibujan como un "?" morado; se elige mirando el dock renderizado, no el
  nombre en disco.
- **Un `Repeater` con un array de JS por modelo destruye y reconstruye TODOS sus delegates cada
  vez que el array se re-evalúa** (2026-08-12). El idiom del panel de control es un `rev++` en
  `Connections { target: <backend> }` y listas `readonly property var xs: { card.rev; return
  backend.list() }`, así que cada eco del backend —incluido el optimista que dispara nuestra
  propia escritura— cambia la identidad del array. Con un slider adentro, eso mata al delegate
  **en el primer movimiento del arrastre**: se pierde el grab, la fila solo responde a clics
  sueltos y el puntero "se escapa" de la ventana a mitad del gesto. Desde afuera parece un
  problema de foco o del `MouseArea`, no del modelo. Va con el modelo en **entero**
  (`model: xs.length`) y el ítem leído por índice (`xs[index]`, con guarda contra `null`: la
  cuenta cambia un frame antes de que el delegate se destruya). Mordió en `VideoCard.qml` y
  estaba latente igual en `AudioCard.qml`.
  El control positivo son dos capturas: con el modelo array, un arrastre con `xdotool`
  (`mousedown`, varios `mousemove`, `mouseup`) deja el handle clavado en el valor viejo; con el
  modelo entero, lo sigue.
- **Un `Flickable` le roba el arrastre a cualquier slider que tenga adentro, y con contenido
  que ni siquiera desborda** (2026-08-12). `yflick()` es `contentHeight != height` —**`!=`, no
  `>`**—, así que un `Flickable` cuyo contenido es *más corto* que el viewport se considera
  flickable igual y toma el grab en cuanto el arrastre se desvía del eje horizontal más allá del
  umbral de Qt (~10 px). Una mano siempre se desvía: el handle deja de seguir al puntero, el
  gesto termina afuera de la ventana y **solo un clic seco funciona** (nunca cruza el umbral).
  Se lee como un problema de foco o de Wayland, y es el modelo de eventos.
  - **`flickableDirection: Flickable.VerticalFlick` NO lo arregla**: vertical es justo el eje que
    roba. Lo que sirve es `Flickable.AutoFlickIfNeeded` (solo flickea si hay algo que scrollear)
    y, para el caso en que sí desborda, apagarle `interactive` mientras el handle está apretado
    — lo hace `CmSlider` solo, buscando el `Flickable` ancestro (`findFlickable()`).
  - **Un arrastre de `xdotool` en línea recta no lo reproduce**: los pasos horizontales grandes
    cruzan primero el umbral del slider, que se queda con el grab. Para reproducirlo hay que
    imitar la mano: pasos horizontales **chicos** (≤10 px) con desvío vertical. Dos arneses
    "limpios" dieron verde sobre esto.
- **Popups y menús**: siempre `popupType: Popup.Window`, si no quedan recortados dentro
  de la superficie del dock en Wayland.
- **La fila que abre un submenú no la declarás vos: la construye el `delegate` del menú
  padre.** Por eso las cabeceras de submenú eran las únicas sin ícono mientras todas las hojas
  ya eran `IconMenuItem`. Se arregla con `delegate: SubMenuDelegate {}` en el menú padre, y el
  nombre del ícono viaja en un `property string menuIcon` del propio submenú, que el delegate
  lee por su property `subMenu` (que existe en `T.MenuItem`, no hace falta nada privado).
  Tres trampas, las tres del 2026-08-08:
  - **Un ítem creado por un `delegate` no resuelve las context properties desde el archivo del
    tipo base.** `theme` se ve desde `SubMenuDelegate.qml` pero **no** desde
    `IconMenuItem.qml`, así que dejar que la URL se arme allá (es lo que hace `iconName`, que
    le pega `theme.revision`) escupe *"Cannot read property 'revision' of undefined"* **una vez
    por fila** — 112 líneas en una config normal, y los íconos igual se dibujan, así que el
    arnés es lo único que lo ve. Armá la URL en el propio archivo y pasala por `iconSource`.
  - **La primera evaluación corre con la fila desprendida**, con *todas* las context properties
    en undefined (`console` incluido, que es lo que delata el diagnóstico). De ahí el guard
    `theme ? theme.revision : 0` más un flag `_ready` que se prende en `Component.onCompleted`:
    cuando se prende, el binding se re-evalúa con `theme` vivo y el cache-busting sigue andando.
  - **Reservá el lugar de la flecha** (`rightPadding`) y **el del tilde**: el `contentItem` de
    `IconMenuItem` es un `Row` que no sabe de `arrow` ni de `indicator` como sí sabe el
    `IconLabel` de Qt, así que la etiqueta más ancha se mete debajo de la flecha y **el tilde de
    un ítem *checkable* se dibuja encima del ícono** (se lee como un ícono mal renderizado, no
    como un layout mal hecho). Lo segundo estaba latente desde antes en `ModeMenu` y compañía.
  Para elegir los nombres de ícono, **iterá contra el dock renderizado, no contra el disco**:
  `find` en `/usr/share/icons/breeze*` dice que existen y no dice cómo se ven en el iconset que
  el dock realmente usa. `preferences-desktop-color` salió un garabato verde y
  `applications-system` un círculo rojo; los buenos fueron `color-management`, `contrast` y
  `application-menu`.
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
