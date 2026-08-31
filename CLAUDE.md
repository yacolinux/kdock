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
- **Hay seis costuras de test en producción**, las seis apagadas por defecto y documentadas
  en el código: `KDOCK_TEST_SCREENS` (lista de monitores, `src/dockmanager.cpp`),
  `KDOCK_DEBUG_DODGE` (transiciones de `windowsOverlap`, `src/dockwindow.cpp`),
  `KDOCK_DEBUG_STARTUP` (cuánto tarda en construirse cada dock, `src/dockmanager.cpp` — la
  respuesta a "el dock no responde al arrancar", ver *Depurar un arranque que "no responde"*),
  `KDOCK_WEATHER_FIXTURE` (una respuesta grabada del proveedor en vez de la red,
  `src/weathercontrol.cpp`) — esta última es lo que hace que `tst_weather` corra en CI, que no
  tiene internet — y `KDOCK_TEST_DISPLAYS` (la lista de monitores de PowerDevil como JSON,
  `src/screenbrightness.cpp`), que es lo que deja probar a qué monitor le habla la rueda del
  brillo sin PowerDevil y sin atenuarle la pantalla a nadie, y `KDOCK_TEST_XKB_RULES` (la ruta
  del `evdev.lst` del que sale el catálogo de distribuciones de teclado,
  `src/keyboardcontrol.cpp`), que es lo que hace que el parser corra en CI, donde no hay
  `xkb-data`. Esta última es de **solo lectura**: solo decide qué lista muestran los combos. En esa última **manda que la
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
5. **Y el estilo también decide el *layout* de los controles a los que les reemplazamos los
   delegates**, no solo su apariencia. Los sliders del panel se quedaron sin área de clic al
   pasar de Basic a Fusion (paso 3) y nadie lo vio hasta el 2026-08-20: ver *Un control de
   QQC2 al que le reemplazás TODOS los delegates…* en `CLAUDE-TRAMPS.md`. Si volvés a tocar
   el estilo por omisión, medí `height()` de cada control custom, no alcanza con que el QML
   cargue.

**Son diez binarios**, los diez los compila el mismo `cmake --build build`:

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
- `kdock-clipboard` (`build/clipboard/kdock-clipboard`, árbol `clipboard/`): historial de
  portapapeles en un proceso separado. El widget `clipboard` de `kdock` solo lo togglea cerca
  del monitor y borde pulsados; la ventana es normal, redimensionable, con `Fija` y ocultado
  después de copiar. Detalles en `AGENTS.md` → *Clipboard history*.
- `kdock-desktop` (`build/desktop/kdock-desktop`, árbol `desktop/`): **copia repropósito de
  `kdock-controlmanager`** convertida en lienzo de widgets de escritorio. Misma UI, tarjetas y
  backends, pero la ventana es una superficie layer-shell **a pantalla completa, siempre
  presente y transparente**, en la capa `bottom` (debajo del dock y de toda ventana normal,
  encima del fondo), con `exclusiveZone=-1` (borde a borde, ignora los struts de los docks) y
  `scope="kdock-desktop"` (KWin la tipa **Normal**, o sea la tapan las ventanas). Bus propio
  `org.kdock.Desktop` (`/Desktop`), config propia `desktop.conf`, y **arranca con todas las
  solapas apagadas y el fondo transparente** a propósito (fase 1: repropósito limpio; el uso de
  la barra de solapas viene después). Su `.desktop` no pide privilegios (layer-shell no está en
  la lista restringida), así que no necesita refresco de ksycoca. Detalles en `AGENTS.md` →
  *Widgets de escritorio*.
- `kdock-systray` (`build/systray/kdock-systray`, árbol `systray/`): la **bandeja del sistema**.
  Es dueño del **host y del watcher de StatusNotifierItem de toda la sesión** (el código de
  `systray.{h,cpp}`, `systraymodel.{h,cpp}`, `systrayimageprovider.{h,cpp}` y `dbusmenu.{h,cpp}`
  que **antes vivía en `src/` y ahora está en `systray/src/`**) y dibuja los íconos y sus menús
  DBusMenu en su propia ventana. Superficie **layer-shell** anclada al borde del dock, estilo
  panel de control (`CmWindow`): redimensionable por config, `exclusiveZone=0`, se oculta al
  perder foco / Esc. Bus propio `org.kdock.Systray` (`/Systray`), config propia `systray.conf`.
  **Instancia única y RESIDENTE**: la ventana se esconde, el proceso sigue vivo — un host SNI
  tiene que seguir corriendo para juntar los ítems de la sesión, así que NO sirve el modelo
  "cerrar = terminar" de weather. El widget `systray` del dock es solo un botón que togglea la
  ventana (`SystrayLauncher`, `src/systraylauncher.{h,cpp}`, como el de weather/controlmanager).
  Su `.desktop` no pide privilegios (layer-shell no está restringido y SNI es D-Bus de sesión),
  así que **no necesita refresco de ksycoca**. Detalles en `AGENTS.md` → *Bandeja del sistema*.
  Ojo con el watcher zombi de kded6, más abajo (*Arnés de `kdock-systray`*).
- `kdock-setwallpaper` (`build/setwallpaper/kdock-setwallpaper`, árbol `setwallpaper/`): toma
  **una imagen como argumento** y la pone de wallpaper de kdock en el **monitor actual** (el gemelo
  de *Avanzar Wallpaper*, pero con la imagen dada). Su razón de ser es el `.desktop`
  (`Name=Setear Kdock Wallpaper`, con `MimeType` de imágenes) que lo mete en **«Abrir con»** de
  Dolphin y PCManFM-Qt. Camino primario: `org.kdock.Dock.setWallpaper` (bajo LXQt lo pinta
  `LxqtWallpapers`); respaldo: Plasma directo. Autocontenido como `kdock-calendar` —sin ventana,
  sin privilegios, corre desde `build/`, **no necesita refresco de ksycoca**—; sólo incluye
  `plasmascript.h` de `../src`. Detalles en `AGENTS.md` → *`kdock-setwallpaper`*.

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

**En la sesión LXQt el dock lo arranca `~/.config/autostart/kdock.desktop`, y ahí `Terminal=`
importa.** Con `Terminal=true` —que es lo que escribe el *Autostart* de `lxqt-config-session`—
libqtxdg envuelve el `Exec=` en el emulador de terminal por omisión
(`XdgDesktopFileData::startApplicationDetached`, `libqtxdg/src/qtxdg/xdgdesktopfile.cpp`), o sea
que kdock arranca **adentro de una qterminal**. La huella queda en el proceso aunque después
cierres la terminal: `tr '\0' '\n' < /proc/$(pgrep -x kdock)/environ | grep -E '^(TERM|WINDOWID)='`
devuelve `TERM=xterm-256color` y `WINDOWID=0`. Va en `Terminal=false` (arreglado 2026-08-20).

Los accesorios se reinician distinto y sin riesgo de dejar la pantalla pelada: cada uno sale
por D-Bus (`org.kdock.Previews` / `org.kdock.TileMenu` / `org.kdock.ControlManager` /
`org.kdock.Clipboard`, método `quit`) y vuelve a levantarse solo — el de previews al prender su
casilla, los otros cuatro en el
próximo clic de su widget (o enseguida, si tienen *Precargar*). O sea que actualizar
`kdock-tilemenu` es `install` + matarlo; no hace falta tocar el dock.

**Desde 2026-08-10 *Dock → Reiniciar* ya los baja a los cuatro** (`src/apprestart.cpp`), así que
para el usuario alcanza con eso. Pero **un accesorio que quedó corriendo no muestra el arreglo
que acabás de instalar**, y desde afuera se ve idéntico a "el bug sigue": el proceso tiene
mapeado el binario que el `install` reemplazó. La comprobación son dos segundos y no hay que
saltearla antes de volver a diagnosticar:

```bash
ls -l /proc/$(pgrep -f /usr/local/bin/kdock-controlmanager | head -1)/exe   # "(deleted)" = es el VIEJO
```

Pasó tal cual: se probó un arreglo del panel de control contra una instancia del día anterior
y el bug "seguía" (2026-08-10).

El install escribe **veinte** cosas: los diez binarios y sus diez `.desktop`. Después de
instalar hay que refrescar ksycoca, pero **solo por los dos primeros**: KWin busca ahí los
`.desktop` para conceder los privilegios (sin eso `kdock-previews` se queda sin capturas y las
tarjetas caen a ícono, y desde 2026-08-17 `kdock` se queda sin las vistas previas al hoverear
un ícono — el mismo `org.kde.KWin.ScreenShot2`, concedido **por ejecutable**). **`kdock-tilemenu`, `kdock-calendar`, `kdock-controlmanager`,
`kdock-weather`, `kdock-clipboard`, `kdock-desktop`, `kdock-systray` y `kdock-setwallpaper` no necesitan el refresco** —no piden ningún privilegio, y su `.desktop` es solo para el nombre y
el ícono del gestor de tareas (el de `kdock-setwallpaper`, además, para aparecer en «Abrir con»)—, así que si lo único que tocaste fue uno de esos, saltealo
y evitás el riesgo de abajo. **La salida del propio `install` te lo dice**: si las diez líneas
de `.desktop` dicen `Up-to-date`, ksycoca ya los tiene y no hay nada que refrescar, sin importar
cuántos binarios se hayan reemplazado. (El panel de control **es** una superficie layer-shell, pero
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

**Y para *ver* un control que está abajo de todo en su solapa, no captures el diálogo: capturá
el widget.** Las solapas van adentro de un `QScrollArea`, así que un `dlg.grab()` devuelve el
viewport y el grupo que te interesa puede quedar afuera — se ve igual que "mi widget no se
agregó". `findChildren<QGroupBox*>()` filtrando por `title()` y un `->grab()` sobre **ese**
sale entero y sin scrollear (2026-08-14, los casilleros de *Apps Seleccionables*).
Ojo también con `Translations::instance()`, que devuelve **nullptr** si la sonda no construyó
ninguna: el `->setActive("spanish")` de rigor para ver la UI traducida es un segfault sin más
explicación. Se instancia una en la pila, como hace `main.cpp`.

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

**Y filtrá los `.o` huérfanos**: el `find` agarra también objetos de fuentes que ya no existen
(el build no los borra), y el link falla con *"vtable sin definir"* / *"referencia sin definir"*
sobre una clase que no está en el árbol — se lee como el error de linkear `kdock.dir` en vez de
`kdock_core.dir`, que es otra cosa. Pasó con `src/appmenuwindow.cpp.o` (2026-08-17). Se listan
con un bucle que compruebe que el `.cpp` de cada `.o` sigue existiendo, o se saca el que moleste
con un `grep -v`.

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

### Probar el teclado sin dejar al usuario escribiendo en otro idioma

`KeyboardControl` escribe `kxkbrc` y le manda a KWin la señal de KConfig que le hace
recompilar el mapa de teclas **en caliente**. O sea que una sonda mal apuntada le cambia el
teclado a la sesión de verdad, y **lo que aísla es `XDG_CONFIG_HOME`**, no `XDG_DATA_HOME`
(`kxkbrc` sale de `QStandardPaths::GenericConfigLocation`). La notificación va igual al bus,
así que para no hablarle al KWin del usuario hace falta además `dbus-run-session`.

```bash
env -i HOME=$HOME PATH=/tmp/fakebin:/usr/bin:/bin \
    XDG_CONFIG_HOME=/tmp/kb/config XDG_DATA_HOME=/tmp/kb/data \
    QT_QPA_PLATFORM=offscreen dbus-run-session -- ./sonda
```

Tres cosas, las tres costaron una corrida (2026-08-22):

- **El `kwriteconfig6` falso no escribe el archivo**, así que una sonda con el `PATH` falso
  nunca converge: cada `apply()` vuelve a "escribir" las mismas claves porque el archivo sigue
  vacío. Para probar la convergencia hay que dejar correr el `kwriteconfig6` de verdad **con
  `XDG_CONFIG_HOME` descartable**, que es inocuo porque `--file kxkbrc` escribe justo ahí.
- **Una sonda contra un bus propio no prueba que la señal salga bien.** Con `QByteArrayList`
  sin registrar, QtDBus la manda con firma vacía y el archivo igual queda perfecto: hay que
  mirar el `dbus-monitor` (o afirmar sobre `msg.signature()`), no el `.conf`. Es el mismo
  patrón que los fondos de pantalla — la config miente.
- **`org.kde.KWin.reconfigure` no recarga el teclado.** Si estás diagnosticando "escribí el
  archivo y no pasa nada", ese es el desvío; lo que sirve es la señal
  `org.kde.kconfig.notify.ConfigChanged` sobre `/kxkbrc`.

Y la prueba de punta a punta **sí se puede hacer contra el KWin de verdad sin riesgo**, porque
es reversible en un comando: una sonda con `XDG_DATA_HOME` descartable (para no tocar el
`kdock.conf` del usuario) pero el `XDG_CONFIG_HOME` real, pidiendo una distribución distinta y
leyendo qué contesta KWin. Anotá el `LayoutList` de antes y devolvelo:

```bash
busctl --user call org.kde.KWin /Layouts org.kde.KeyboardLayouts getLayoutsList
# a(sss) 1 "latam" "" "Spanish (Latin American)"
```

### Probar el Modo QT (apariencia de LXQt) sin re-tematizarle la sesión al usuario

`QtCompat` traduce el `kdeglobals` vivo al grupo `[Palette]`, a `icon_theme` y a `[Qt] font` de
`~/.config/lxqt/lxqt.conf`, y el tema de plataforma de LXQt vigila ese archivo: escribirlo
**re-tematiza todas las apps Qt abiertas**.
O sea que no hay caja de arena posible salvo una: **lo que aísla es `XDG_CONFIG_HOME`, no
`XDG_DATA_HOME`** — `lxqt.conf` sale de `QSettings::UserScope`. Una sonda que solo aísle
`XDG_DATA_HOME` le cambia los colores a la sesión de verdad.

Con las dos variables descartables se prueba todo sin tocar nada; el sandbox de
`tests/unit/sandbox.h` ya pone las dos, que es por lo que `tst_qtcompat` corre en CI:

```bash
rm -rf /tmp/qtc && mkdir -p /tmp/qtc/config/lxqt /tmp/qtc/data/kdock
cp ~/.config/lxqt/lxqt.conf /tmp/qtc/config/lxqt/     # para probar que el resto sobrevive
# ...un kdeglobals sembrado con colores reconocibles...
env XDG_CONFIG_HOME=/tmp/qtc/config XDG_DATA_HOME=/tmp/qtc/data QT_QPA_PLATFORM=offscreen ./sonda
```

Tres cosas que costaron una corrida cada una:

- **Una app de KDE no se prueba con el arnés de una app Qt pelada.** Toda app KXmlGui construye
  `KColorSchemeManager`, que **pisa la paleta del tema de plataforma**; una sonda `QApplication`
  mínima da verde sobre un escritorio donde Dolphin sigue en Breeze Light. La forma barata de ver
  la diferencia son dos sondas de veinte líneas —una pelada y otra que llame a
  `KColorSchemeManager::instance()`— imprimiendo `qApp->palette()` antes y después: ahí el salto
  de `#2c3746` a `#eff0f1` se lee de una. Y la prueba final es **Dolphin de verdad bajo Xvfb**
  (`XDG_CONFIG_HOME` descartable + `import -window root` + contar colores dominantes con PIL),
  que es como se cerró el bug del 2026-08-20.
- **Los tests unitarios hay que correrlos por `ctest`, no ejecutando el binario a mano.** El
  `PATH` con las herramientas falsas lo pone `set_tests_properties`, así que
  `./build/tests/tst_qtcompat` usa el `kwriteconfig6` **de verdad** — que además escribe en el
  `kdeglobals` del sandbox y hace fallar la corrida siguiente por una precondición que parece un
  bug del producto. Me pasó (2026-08-20).
- **Las herramientas falsas escriben su línea de `calls.log` tarde.** Salen por
  `startDetached`, así que las de una prueba pueden aterrizar **después** de que esa prueba
  terminó: un `clear` a secas al empezar la siguiente deja pasar las rezagadas y las cuenta como
  propias (la prueba de la casilla de colores veía tres invocaciones ajenas). Hay que esperar y
  *después* vaciar, y afirmar sobre el log con `QTRY_VERIFY`, nunca con una lectura directa.
- **La segunda mitad de la feature no se prueba con una aserción sobre el archivo.** Que las
  diez claves queden bien escritas **no** prueba que algo se repinte: eso depende del
  `QFileSystemWatcher` del plugin, que vive en otro proceso. Lo único que lo prueba es la sesión
  real (abrir una app Qt, cambiar el esquema, mirar). Vale lo mismo que para los fondos de
  pantalla: la config miente, hay que mirar la pantalla.
- **`QSettings` no distingue una reescritura del mismo tamaño hecha en el mismo milisegundo.**
  Su caché por ruta se revalida por (mtime, tamaño), y el mtime es de milisegundos. Un test que
  cambie `17,18,19` por `90,91,92` a pocos ms de la lectura anterior sale **cara o cruca** —
  pasó, 50/50 en ocho corridas. En una prueba de *cableado* elegí valores de largo distinto
  (`9,9,9`) y una pausa; el caché en sí se prueba aparte, con una edición externa hecha por
  `QFile` (que es como llega la de otro programa, mientras que una hecha con `QSettings` le
  actualiza el caché al propio proceso y no prueba nada).
- **Ni el arnés ni CI ejercitan los dos `sync()` de `qtcompat.cpp`.** Están como guarda contra lo
  de arriba, no como arreglo de nada observado: sacarlos deja las pruebas en verde. El
  comentario del código lo dice; no los "verifiques" a partir de un test que pasa igual.
- **`lxqt.conf` es la SALIDA, no un ajuste de kdock**, así que un test o una sonda tiene que
  vaciarlo entre casos además del grupo `[QtCompat]` del `kdock.conf`. Si no, cualquier
  aserción de la forma "esta parte está apagada, así que su clave está vacía" ve lo que dejó la
  corrida anterior y **falla señalando al producto** (pasó con las tres casillas, 2026-08-20).
  `QSettings::clear()` y no borrar el archivo: así el caché del propio proceso queda coherente.

### Probar la capa de compatibilidad con LXQt (sesión, systray, fondos)

Las tres cosas se prueban distinto, y sólo una de las tres entra en el arnés de Xvfb.

**Sesión (`PowerControl`) — una sonda, y el `PATH` falso no es opcional.** `resolvedCommand()`
existe justo para esto: dice *qué* se lanzaría sin lanzarlo, porque lanzarlo cierra la sesión.
Aun así, corré con `lxqt-leave` falso en `/tmp/fakebin`, porque un dedo torcido en la sonda
apaga la máquina. Se linkean los `.o` de `kdock_core.dir` como siempre:

```bash
env KDOCK_TEST_SESSION=lxqt PATH=/tmp/fakebin:$PATH QT_QPA_PLATFORM=offscreen ./sonda
# session kind: Lxqt   available: true   logout -> "lxqt-leave --logout"  …
```

**`KDOCK_TEST_SESSION=kde|lxqt|other`** es la costura, y **manda que la variable esté puesta**:
un valor desconocido es `Other`, o sea "un escritorio que kdock no conoce". Como
`Session::kind()` se cachea por proceso, un test que quiera varios casos necesita **un proceso
hijo por caso** — que es exactamente lo que hace `tests/unit/tst_session.cpp`.

**Systray — se prueba entero sin GUI, y con un cliente de verdad.** Bajo `dbus-run-session` no
hay ningún watcher, así que kdock se convierte en uno: justo el camino que estaba roto. Un
`QSystemTrayIcon` de veinte líneas es el control positivo que ninguna introspección reemplaza,
porque lo que se rompía era la lectura de propiedad que hace Qt:

```bash
xvfb-run -a -s "-screen 0 1920x1080x24" dbus-run-session -- bash -c '
  env -u WAYLAND_DISPLAY XDG_DATA_HOME=/tmp/kd-sni QT_QPA_PLATFORM=xcb \
      QT_QUICK_BACKEND=software PATH=/tmp/fakebin:$PATH ./build/kdock & sleep 6
  busctl --user introspect org.kde.StatusNotifierWatcher /StatusNotifierWatcher   # las DOS interfaces
  ./tray & sleep 5                                                                # el cliente Qt
  busctl --user get-property org.kde.StatusNotifierWatcher /StatusNotifierWatcher \
         org.kde.StatusNotifierWatcher RegisteredStatusNotifierItems'              # as 1 ":1.18/StatusNotifierItem"
```

Lo que hay que leer: las dos interfaces presentes, las tres claves como **`property`** (`b`,
`as`, `i`) y no como `method`, y en el log del dock `systray host registered (we are the
watcher)` en vez de `failed to register systray host: "No such interface …"`.

**Fondos — el arnés de Xvfb NO sirve, y hay que decirlo fuerte**: bajo X no hay layer-shell, así
que la ventana sería una X normal y no probaría ni la capa, ni el anclaje, ni la región de
entrada vacía. Silencio ahí no prueba nada de la feature. Se prueba con una segunda instancia
aislada en la sesión Wayland real (`enabledScreens=VIRT-9` para que no arme ningún dock) y se
**mide en píxeles**: imágenes de color sólido generadas con PIL, una distinta por monitor y por
escritorio, y después contar cuánto de cada monitor quedó de ese color exacto. Acá dio 94–97 %
en el escritorio vacío. Cinco cosas:

- **Sembrá `[QtCompat] enabled=false` en el `.conf` de prueba.** Si no, la migración de
  auto-activación de Modo QT ve un `XDG_DATA_HOME` descartable, lo cuenta como primer arranque y
  **le re-tematiza la sesión al usuario** — `lxqt.conf` vive en `XDG_CONFIG_HOME`, que este arnés
  no aísla.
- **NO pongas un `pcmanfm-qt` falso en `/tmp/fakebin`.** Es el mismo directorio que hace falta
  por el brillo, y ahí el falso **se come el relanzamiento del gestor de escritorio**: la corrida
  termina "bien", el `calls.log` dice `pcmanfm-qt <- --desktop --profile=lxqt`, y el usuario se
  queda sin escritorio. Pasó (2026-08-20). Comprobalo siempre al terminar:
  `busctl --user get-property org.pcmanfm.PCManFM /Application org.pcmanfm.Application desktopManagerEnabled`
  tiene que dar `b true`; si no, `setsid pcmanfm-qt --desktop --profile=lxqt &`.
- **Matá la instancia con SIGTERM**, no con SIGKILL: es el camino que devuelve el escritorio, y
  probarlo es la mitad de la feature.
- **Una captura del escritorio 1 puede no mostrar nada**: si las ventanas del usuario lo tapan,
  el conteo de píxeles mide sus ventanas. El escritorio virtual **vacío** es donde se mide; para
  el resto, la sonda de `imageForTesting(desktop, screen)` dice qué archivo eligió cada monitor,
  que es más preciso que contar píxeles a través de una terminal.
- **Devolvé el escritorio virtual original** (anotá el uuid antes).

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

### Arnés de `kdock-systray` (y el watcher zombi de kded6)

La lógica del **host SNI** ya la congela `tests/unit/tst_systray.cpp` (bajo `dbus-run-session`,
con un cliente `QSystemTrayIcon` de verdad): registrar un ícono no puede colgar la GUI, y el
host junta el ítem. Ese test **no** ejercita la ventana ni el endurecimiento del watcher.

**`--dump` es la herramienta inocua**: levanta el host, espera a que asienten los viajes al
watcher, imprime si somos el watcher y qué ítems ve, y sale sin abrir ventana. Pero **compite
por el watcher**, así que corré en un bus propio o vas a pelearle la bandeja al kdock del
usuario:

```bash
dbus-run-session -- env QT_QPA_PLATFORM=offscreen ./build/systray/kdock-systray --dump
# -> "we are the watcher  kde: true fdo: true"  (bus vacío: nadie más lo tiene)
```

Y el arnés gráfico, igual que el del panel de control (bajo X **no** hay layer-shell, así que
prueba que el QML carga y dibuja, no el anclaje ni el cierre por foco):

```bash
rm -rf /tmp/sys-w && mkdir -p /tmp/sys-w/kdock && ln -s ~/.local/share/icons /tmp/sys-w/icons
printf '[General]\ncloseOnFocusLoss=false\n' > /tmp/sys-w/kdock/systray.conf
timeout 40 xvfb-run -a -s "-screen 0 1280x800x24" dbus-run-session -- bash -c '
  env -u WAYLAND_DISPLAY XDG_DATA_HOME=/tmp/sys-w QT_QPA_PLATFORM=xcb QT_QUICK_BACKEND=software \
      ./build/systray/kdock-systray --show > /tmp/sys-w.log 2>&1 &
  APP=$!; sleep 7; import -window root /tmp/systray.png; kill $APP'
```

Con el bus propio sale la bandeja vacía ("La bandeja está vacía"); para ver íconos hay que
levantar un cliente de bandeja en ese mismo bus (como hace `tst_systray`).

**El watcher zombi de kded6 (la causa del bug original, 2026-08-24).** En esta sesión mixta
(LXQt + KDE de `/opt/kde`) el `kded6` de Ubuntu **acapara el nombre `org.kde.StatusNotifierWatcher`
pero no sirve ningún objeto** en `/StatusNotifierWatcher` (su plugin `kf6/kded/statusnotifierwatcher.so`
registra el nombre y no exporta el objeto). Por eso el viejo `SystrayHost` quedaba dueño solo del
watcher `org.freedesktop` con **0 ítems**, mientras `blueman-tray` se registraba contra el watcher
muerto de kded6, y el widget se ocultaba (`count == 0`). Diagnóstico read-only:

```bash
busctl --user call org.freedesktop.DBus /org/freedesktop/DBus org.freedesktop.DBus \
    GetNameOwner s org.kde.StatusNotifierWatcher            # quién lo tiene
qdbus6 org.kde.StatusNotifierWatcher /StatusNotifierWatcher # "No such object path" = zombi
```

`ensureWatcher()` (en `systray/src/systray.cpp`) ahora **no confía en el nombre**: prueba el
objeto con un `Get` de `RegisteredStatusNotifierItems`; si da error, `becomeWatcher()` toma FDO
(y KDE si puede). **No se le puede robar el nombre a kded6 en caliente** (no pidió
`AllowReplacement`): para que `kdock-systray` sea el watcher de verdad hay que **ganarle el
arranque** (autostart temprano) o **liberar el nombre** desactivando el módulo roto de kded6
(`~/.kde-opt/config/kded6rc` → `[Module-statusnotifierwatcher]\nautoload=false`). Es un cambio de
sesión: preguntá antes.

### Arnés de `kdock-clipboard` en la sesión real (klipper como control)

Bajo Xvfb **no hay data-control** (es X11), así que el backend del accesorio solo se
puede probar en la sesión Wayland de verdad. Una sonda que linkee `waylandclipboard.cpp` (o
`clipboardhistory.cpp`) e imprima lo que captura alcanza, y **Klipper sirve de control desde la
CLI** en las dos direcciones:

```bash
qdbus6 org.kde.klipper /klipper setClipboardContents "kdock prueba"   # -> la sonda debe verlo
qdbus6 org.kde.klipper /klipper getClipboardContents                  # tras nuestro setText()
spectacle-custom -b -n -c                                             # copia una imagen
```

Lo que prueba de verdad: `kdock-clipboard` **no abre la ventana ni toma foco cuando está oculto**,
así que si ve el texto, la captura en segundo plano funciona. Con
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
- **La misma cadena en C++ y en QML es UNA sola entrada del catálogo, y alcanza para las dos.**
  `gen-capabase.py` deduplica entre secciones a propósito (*"the lookup map is shared between
  the two sections, so a string used in both places is written once, in whichever comes
  first"*) y `KdockTranslator::translate()` hace `Q_UNUSED(context)`: busca por texto fuente en
  **una** tabla. O sea que una opción que se lleva del diálogo al clic derecho —el mismo texto
  en un `tr()` y en un `qsTr()`— aparece una vez sola, en `UIdock` o en `Configuracion` según
  cuál se enumere primero, y traducirla ahí traduce las dos.
  Verificado el 2026-08-17 con la casilla *Vista previa de la ventana*: la clave quedó **solo**
  en `UIdock`, y con un `spanish.md` de prueba que la mapea a `MARCA-COMPARTIDA` la casilla del
  **diálogo** (un `tr()`) sale con ese texto. Antes acá decía lo contrario —que eran dos
  entradas y que había que buscar la clave dos veces— y eso manda a buscar un duplicado que no
  existe. El síntoma del 2026-08-15 era real pero la causa es la de dos ítems más abajo: un
  `.md` de usuario sembrado antes de que la cadena existiera, que `mergeSection()` es lo que
  arregla.
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
- QML: `QtQuick` + `QtQuick.Controls` (sin sufijo, ver *Qt style*), nada que dependa de
  estilos de KDE. `Qt5Compat.GraphicalEffects` es dependencia de runtime del dock (si falta, el
  QML no carga y la pantalla queda sin dock): lo usan el `OpacityMask` de la imagen de panel y
  el `Glow` del resplandor de neón (los dos glows, rim y halo exterior). El 2026-08-26 se probó
  migrar el neón a `QtQuick.Effects` (`MultiEffect`) por un supuesto ahorro de buffers, pero se
  percibió más lento en la sesión real y se revirtió el mismo día a `Glow` — que era el estado
  fluido. Si volvés a intentar la migración, medí GPU en la sesión Wayland real antes de comitear.
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

**Y desde 2026-08-22 el camino de lectura → aplicación también se prueba en CI, pero sólo por
la rama de LXQt.** `AutoColorScheme::setWallpaperSource()` reemplaza el viaje por D-Bus por un
`std::function`, así que `tst_autocolorscheme` puede inyectar rutas y afirmar sobre lo que sale.
Tres cosas de esos casos:

- **La fuente hay que sembrarla con la clave `""`.** `systemScreenName()` cae a
  `QGuiApplication::screens()` y bajo `offscreen` la única pantalla **no tiene nombre**, así que
  el monitor que manda se llama `""`. Con cualquier otro conector, `leadImage` queda vacío y la
  contabilidad de `variant`/`lastImage` no corre: el test pasa sin probar esa mitad.
- **Las imágenes se generan con `QImage`, no son fixtures**, misma razón que
  `tst_wallpapercolors`: lo que se afirma es el color que sale. Y van **saturadas**, porque el
  portón de saturación/valor del muestreo descarta gris, negro y blanco.
- **Los `.colors` sobreviven entre casos.** El `init()` limpia la sección `[ColorAuto]` del
  `kdock.conf` pero no el directorio, así que un caso que afirme "no se escribió nada" tiene que
  borrar los dos generados primero o ve los de la prueba anterior.

Y el control positivo que confirmó que esos casos no pasan de arriba: cambiar `if (m_source)`
por `if (false)`, recompilar y correr — 6 de los 7 fallan. (El séptimo, "una fuente vacía no
aplica nada", pasa igual: sin fuente la llamada D-Bus también falla y no se aplica nada. Es un
test más débil por naturaleza.)

**Y ojo con que `Session::kind()` cambia entre esta máquina y CI**: acá
`XDG_CURRENT_DESKTOP=LXQt:kwin_wayland` da `Lxqt`, en el container de Arch no está definida y da
`Other`, o sea que el watcher de `appletsrc` se arma en CI y no acá. Ningún test afirma sobre
eso hoy, pero si tocás esa rama corré el tier entero con las tres:
`for s in kde lxqt other; do KDOCK_TEST_SESSION=$s ctest --test-dir build -L unit; done`.

Lo que esos dos no pueden probar, y cómo se probó:

- **Que la solapa funcione**: la sonda que linkea los `.o` de `kdock_core.dir` (receta de más
  arriba), con `XDG_DATA_HOME` y `XDG_CONFIG_HOME` descartables **y el `PATH` falso**, que acá
  no es opcional: la solapa aplica esquemas e iconsets de verdad. Desambiguá los controles
  **por el `QGroupBox` que los contiene**; el interruptor maestro es el único `QCheckBox` que
  no está dentro de ninguno. Con `enabledScreens=` de una pantalla inexistente el
  `DockManager` no arma ninguna ventana y la sonda no necesita ningún backend real.
  - **Y con `KDOCK_TEST_SESSION` se prueban las dos ramas de la solapa sin cambiar de máquina**
    (2026-08-22). Bajo `lxqt` el grupo se llama *Esquema del sistema* y crece dos filas de
    estado (*Fondos* / *Modo QT*) con sus botones de salto; bajo `kde` se llama *Esquema del
    sistema (KDE)* y no tiene ninguna. La sonda las encuentra por el `title()` del grupo e
    imprime el `text()` de cada `QLabel`, que es lo que prueba **cuál** de los cuatro mensajes
    salió — la captura sola no distingue "apagado" de "activado". El `grab()` va sobre **ese
    grupo**, no sobre el diálogo: las solapas viven en un `QScrollArea` y el grupo puede quedar
    afuera del viewport.
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
El cuerpo de cada trampa está en **`CLAUDE-TRAMPS.md`** — abrí ese archivo y leéla entrada cuyo título coincida con el área que estás tocando. Este índice es solopara saber qué existe y cuándo consultarlo:
- **Un `QDialog` sin parent no lo borra nadie, y el diálogo de kdock colgaba de un `QQuickView`**
- **Un repintado que se cuelga de `changed()` no puede refrescar la fuente de `changed()`**
- **Una conexión de una solapa del diálogo NO puede tener a `this` de contexto**
- **Vigilar un DIRECTORIO donde uno mismo escribe es un bucle garantizado**
- **`QZipWriter` AGREGA al archivo que encuentra, no lo pisa**
- **Un archivo que se exporta y el import descarta no avisa de nada**
- **No apliques una configuración desde el proceso que la está usando**
- **`plasma-apply-colorscheme` no hace NADA si el nombre pedido ya es el que está puesto**
- **`QColor` guarda más de 8 bits por canal y `operator==` compara ESO, no `red()/green()/blue()`**
- **Una señal no puede llamarse igual que un método estático de la misma clase.**
- **`DockConfig::anyDarkModeActive()` recorre las instancias VIVAS de `DockConfig`.**
- **`QFileSystemWatcher` suelta el watch cuando el archivo se reemplaza por `rename`.**
- **Cuidado con las respuestas asíncronas que llegan después de que el usuario canceló.**
- **Un fondo monocromático rompe cualquier "probá otro color" basado en el tono.**
- **Los colores dominantes de una foto suelen ser el mismo tono**
- **Un `import` de QML que falta no imprime NADA: el dock arranca con la ventana vacía**
- **`... | grep -q` bajo `set -o pipefail` devuelve 141**
- **`qmllint` a secas puede ser el de Qt5.**
- **Un arnés gráfico con `sleep` fijo miente en la máquina de otro.**
- **En modo panel `root.width` NO cambia nunca**
- **La franja que descubre un dock escondido está al borde de la SUPERFICIE, no de la pantalla**
- **Y la cura de eso, si mueve la superficie, es peor que la enfermedad**
- **Y esto sí se prueba bajo Xvfb, de punta a punta**
- **El modo *dodge* solo se prueba en la sesión Wayland real**
- **Un `QDialog` de un cliente layer-shell se abre DEBAJO de su propio panel**
- **`left`/`top`/`right`/`bottom` son propiedades FINAL de `Item`**
- **Un control que se mueve con lo que arrastra no puede medir el arrastre contra sí mismo**
- **KWin no deduce el borde exclusivo de un ancla de esquina**
- **Grosor del dock**
- **El alto de la caja del nombre no es una línea, es `iconLabelBoxHeight()`**
- **Una entrada de esa fórmula se mide en QML**
- **Leer una propiedad D-Bus que es un struct: dos trampas, la segunda te tira el proceso.**
- **Un widget nuevo del dock se toca en SIETE archivos**
- **El scope de QML es por ARCHIVO, y un `Binding` dentro de un `Loader` es la carga.**
- **`Component.onCompleted` (y cualquier handler) se declara UNA sola vez por objeto.**
- **Una señal que hay que hacerle llegar a otro `DockConfig` se RELAYA, no se conecta cruzada.**
- **Las ventanas se pueden falsificar en un test sin compositor**
- **Un widget del que puede haber VARIOS por dock no se parece a los otros seis-archivos**
- **Un `Component` declarado en la raíz de `Dock.qml` no ve los ids del delegate que lo carga**
- **`QSettings` comparte un caché por ruta y lo revalida por (mtime, tamaño)**
- **Una sección que se repite (`spring`, `sep`, `gap<n>`) NO va en `knownWidgetTokens()`**
- **Una clave nueva del grupo del menú se toca en CINCO lugares**
- **Los controles de las solapas Colores y Fuentes son copias, no atajos.**
- **Un texto nuevo del diálogo o del QML no se traduce solo**
- **Un ícono de breeze que es line-art oscuro desaparece sobre un panel oscuro.**
- **Nuevo `.qml`**
- **Un componente de sección se instancia aunque la sección esté invisible.**
- **La systray puede vivir en cualquier dock, pero en uno solo.**
- **`m_instances` NO es "los docks visibles".**
- **En el protocolo de ventanas de Plasma, "en ningún escritorio" significa "en TODOS".**
- **`SIGTERM` no dispara `aboutToQuit`, y arreglarlo mal te inunda el journal.**
- **Un fondo de escritorio solo se puede *ver* desde un escritorio virtual vacío.**
- **Un `QProcess` de larga vida sobrevive al `::_exit(0)` del SIGTERM.**
- **Y el reflejo simétrico: un hijo de larga vida que se muere no se reintenta solo.**
- **Una cadena con `" = "` adentro se parte mal en el catálogo de traducciones.**
- **`Window.window` se lee desde un `Item`, no desde adentro de un `Timer`.**
- **Un `QDBusArgument` local va `const` — y en `BatteryControl` no lo estaba.**
- **Nunca hagas una llamada D-Bus bloqueante desde adentro de un handler de D-Bus.**
- **Un bucle de eventos anidado en `main()` dispara handlers antes de que la app exista.**
- **Popups y menús**
- **Un popup del dock con el que se pueda INTERACTUAR va con `Qt.ToolTip`, nunca con `Qt.Popup`** — y no puede cerrarse por *leave*.
- **QML no concatena literales adyacentes como C++.**
- **Un `Menu` de QtQuick.Controls no se dimensiona solo a su ítem más largo.**
- **Un control de QQC2 al que le reemplazás TODOS los delegates puede quedar de 0 px de alto, y sigue dibujándose.**
- **Un `Menu` o un `ToolTip` declarados dentro del delegate se instancian por fila.**
- **`showMaximized()` no hace nada sin gestor de ventanas.**
- **Un auto-placement que prueba cada celda contra la lista de lo ya puesto es O(n³).**
- **`QUrl` percent-codifica las llaves del uuid de KWin.**
- **Cualquier transferencia del portapapeles va asíncrona o te cuelga el dock.**
- **Para escribir settings de NetworkManager hace falta `qDBusRegisterMetaType`.**
- **NM normaliza lo que le mandás, y hay que releerlo para saber qué guardó.**
- **Una prueba del editor de redes escribe en el NetworkManager de verdad.**
- **`Qt::WA_TransparentForMouseEvents` deja sin eventos de mouse también a los hijos.**
- **`QWidget` que aún no tiene padre no lo ve `findChildren()`.**
- **Nunca `setVisible(true)` sobre una página adentro de un `QTabWidget`.**
- **El arnés de Xvfb tiene efectos reales sobre la máquina.**
- **Un clic por coordenadas contra una lista que se reordena apunta a otra fila.**
- **Clave de fila en `DockModel`**
- **`enabledScreens=` vacío no es lista vacía**
- **Nunca esperes a un proceso hijo desde el hilo de la GUI, por más rápido que "siempre" conteste.**
- **Editar `tests/lib/fakebin.sh` NO regenera las herramientas falsas** si el `.stamp` ya existe.
- **Una sonda sin `XDG_DATA_HOME` aislado le deja al usuario un escritorio SIN DOCKS, y no se repara sola.**
- **`kdock-previews` necesita DOS privilegios de KWin**
- **Correr `./build/previews/kdock-previews` no está autorizado.**
- **Un `HoverHandler` sobre la superficie del dock se puede quedar pegado en `hovered=true` para siempre, y con eso muere el auto-hide entero.**
- **Un `QDBusAbstractAdaptor` fija el nombre de interfaz con `Q_CLASSINFO`**, y los adaptores pertenecen al OBJETO, no a la ruta.
- **Apagarle el gestor de escritorio a PCManFM-Qt lo mata**, así que volver a prenderlo no puede ser sólo una llamada D-Bus.
- **Un interruptor compartido entre dos motores no dice cuál de los dos está corriendo.**
- **Un `Q_PROPERTY(... CONSTANT)` que depende del orden de arranque deja el widget invisible para siempre**, sin un solo mensaje.
- **El arnés de Xvfb del dock no sirve para clickear widgets con el motor de fondos encendido**: la superficie del fondo tapa el dock y se come los clics.
- **El *scope* de una superficie layer-shell decide su `WindowType` en KWin**, y kdock lo tenía hardcodeado en `"dock"` para todas.
- **Medir un "flicker" comparando dos capturas mide el contenido vivo de las ventanas**, no el bug.
- **`org.kde.KWin.reconfigure` no recarga el mapa de teclas**, y `/etc/default/keyboard` es la respuesta de X11.
- **Un metatipo de D-Bus registrado por el test le queda registrado al código de producción.**
- **Un combo que abre en un valor de *fallback* no guarda ese valor**, y la casilla que lo aplica no aplica nada.
- **`QDBusConnection::registerService` no hace cola, y el reinicio del dock corre una carrera contra sí mismo.**

## Depurar un arranque que "no responde"

"El dock no recibe clicks N segundos después de reiniciar" tiene al menos cuatro causas posibles
y **ninguna se distingue mirando el código**. El 2026-08-21 se recorrieron las cuatro en ese
orden; las tres primeras son descartes de un comando cada una, y la cuarta es la que encontró el
bug (un deadlock de D-Bus con la bandeja, ver `AGENTS.md` → *Arranque sin bloqueo tras reinicio*).

**Antes que nada, dos preguntas al usuario**, porque parten el árbol en dos: ¿los clicks **se
pierden** o llegan tarde de golpe? ¿es sólo el dock o **todo el escritorio**? "Se pierden" y
"todo" descarta que el bucle del dock esté ocupado y apunta a una superficie que se come el
input… o a que el resto de las apps también están bloqueadas, que es lo que pasaba acá.

1. **¿Se cuelga KWin?** Latencia de una llamada trivial cada 150 ms durante el reinicio. Su D-Bus
   se atiende en el mismo hilo que el input, así que sirve de termómetro del compositor:
   ```bash
   busctl --user get-property org.kde.KWin /VirtualDesktopManager \
          org.kde.KWin.VirtualDesktopManager current   # ~15 ms; un pico de segundos = KWin parado
   ```
2. **¿Qué superficies hay, y en qué capa?** Un script de KWin cargado por D-Bus
   (`loadScript` + `start`) que imprima `workspace.windowList()` con `frameGeometry`, `dock`,
   `desktopWindow` y `layer`, más `windowAdded`/`windowRemoved` para tener la **línea de tiempo
   con timestamps** en el journal. Así se ve si algo a pantalla completa aparece por encima de las
   ventanas (capa ≥ 2) y cuándo aparece cada dock. La superficie del wallpaper de kdock sale con
   `desktopWindow=true layer=0`, o sea **debajo** de las ventanas: no puede comerse un clic
   dirigido a Dolphin. **Acordate de `unloadScript` al terminar.**
3. **¿Se redimensionan las ventanas?** El mismo script, conectando `frameGeometryChanged` de cada
   ventana normal: al morir el dock desaparecen sus zonas exclusivas y la sospecha natural es una
   tormenta de relayout en todas las apps. Acá **no hubo ni un solo cambio de geometría**.
4. **¿Se cuelga el propio dock, y dónde?** Dos herramientas, en este orden:
   ```bash
   # (a) termómetro: latencia de org.kdock.Dock durante el reinicio
   busctl --user --timeout=30 call org.kdock.Dock /Dock org.kdock.Dock primaryDockId
   # (b) backtrace del bloqueo, en vivo (ptrace_scope=2 en esta máquina: hace falta sudo)
   sudo eu-stack -p $(pgrep -x kdock)
   ```
   Un `dock_ms=24352` es la firma del **timeout de 25 s de D-Bus**, no de "el arranque es lento".
   Y `eu-stack` sobre el proceso congelado da la respuesta en una línea: dos muestras a 2 s de
   distancia con la misma pila = bloqueo, y la pila dice exactamente quién.
5. **¿Quién no contesta?** `dbus-monitor --session` durante el reinicio, y después parear cada
   `method call` con su `method return`/`error` por `serial`/`reply_serial` para listar las
   llamadas lentas. Es lo que mostró las dos mitades del deadlock (25,18 s en un sentido, 24,14 s
   en el otro) y **a quién** le estaba esperando cada una.

Y para el costo legítimo del arranque (instanciar el QML de cada dock) está la costura
`KDOCK_DEBUG_STARTUP=1`, que imprime `startup dock <id> built in N ms` por dock — apagada por
omisión, como `KDOCK_DEBUG_DODGE`.

**Dos advertencias sobre medir esto:**

- **Un reinicio disparado desde el menú se mide igual con `busctl --user call org.kdock.Dock
  /Dock org.kdock.Dock restart`**, que es el mismo camino (`kdock::restartAll`). No hace falta
  tocar el dock a mano.
- **El proceso relanzado hereda el stderr del que lo lanzó.** Si en algún momento lo arrancaste
  con `> /tmp/loquesea.log`, todos los reinicios siguientes escriben ahí y no en el journal ni en
  el log que estás mirando. `ls -l /proc/$(pgrep -x kdock)/fd/2` antes de sacar conclusiones. Y si
  truncaste ese archivo mientras el proceso viejo lo tenía abierto, queda con un agujero de NULs
  al principio y **`grep` lo trata como binario**: `grep -a`.

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

## Probar las vistas previas al hoverear un ícono (bloque Apps)

Desde 2026-08-17 **kdock también pide capturas a KWin** (`AGENTS.md` → *Vista previa de
ventana*). Tres cosas que hay que saber, y ninguna es evidente:

**1. `kdock.desktop` lleva ahora `X-KDE-DBUS-Restricted-Interfaces=org.kde.KWin.ScreenShot2`, y
el privilegio es POR EJECUTABLE.** O sea que el `--dump-captures` de `kdock-previews` **no
responde por kdock**, y un `./build/kdock` sin `.desktop` propio recibe `NoAuthorized` en cada
captura: no aparece ninguna vista previa y se ve **idéntico** a un bug del QML. Por eso kdock
tiene su propio volcado, que no abre ninguna ventana y sirve en la sesión real:

```bash
kdock --dump-captures /tmp/kdock-thumbs
```

0 ventanas → falta `X-KDE-Wayland-Interfaces`. Ventanas sí y capturas `NoAuthorized` → falta la
otra clave, o ksycoca no la vio. Para probar **el binario del build** sin reinstalarle el dock
al usuario: un `.desktop` temporal en `~/.local/share/applications/` con las dos claves, más el
refresco de ksycoca con la ruta explícita de más arriba. Copiá el binario a una ruta **sin `#`**
(`/tmp/kdock-diag/kdock`) y apuntá el `Exec=` ahí; y **borrá el `.desktop` al terminar**, con
otro refresco. Verificado así el 2026-08-17: 16 ventanas, 16 capturas, las minimizadas incluidas.

**2. El hover NO se puede disparar con `xdotool`** (XTEST no llega a un cliente Wayland nativo,
misma trampa que el diálogo del panel de control). Lo que funcionó es un **hook temporal en el
delegate de `Dock.qml`**: un `Timer` que llama a `root.queueAppPreview(content, appsGrid.appsModel,
delegateRoot.index, appsGrid.sectionToken)` — o sea el sitio de llamada de producción con los
argumentos de producción, con lo único falso siendo el disparador. Con eso se probaron, en tres
corridas, los tres casos que importan:

- **positivo**: `windowCount > 0` en el bloque de apps → aparece la micro ventana;
- **negativos** (`console.log` del token + `root.previewUuid` después de llamar): un lanzador
  sin ventanas y un `appsel1` con ventana → `previewUuid` vacío en las 14 rondas, nada en
  pantalla;
- **re-targeting**: intervalos escalonados por índice (`3000 + index * 900`) hacen que los
  íconos se turnen, y tres capturas de pantalla seguidas muestran tres posiciones distintas con
  tres ventanas distintas. Es la prueba de que el ciclo destruir/rehacer del xdg_popup no se
  traba (un xdg_popup no se puede mover).

**Acordate de sacar el hook** (`grep -c TEMP-PROBE qml/ src/` tiene que dar 0).

**Pero los botones de la vista previa NO necesitan nada de eso** (2026-08-19): `AppPreviewWindow.qml`
es autocontenido a propósito —no toca ninguna context property, todo entra por properties— así que
se instancia solo con un `QQmlComponent` y el `image://icon` registrado, y bajo Xvfb es una ventana
X normal. O sea que **`xdotool` sí le hace clic**, y las señales se leen de un `console.log`:

```bash
cp qml/AppPreviewWindow.qml /tmp/apvprobe/qml/          # la ruta del repo tiene un '#'
# Probe.qml: un AppPreviewWindow con las properties a mano y un console.log por señal
xdotool mousemove 260 14 click 1     # los tres botones: 320-4-66 → centros 260 / 283 / 306
```

Con eso se verificaron en una corrida los cuatro gestos (los tres botones + el clic en la captura)
y que **todos** emiten `activity()`, que es lo que impide que el watchdog cierre el preview a
mitad del clic. Es también la forma de elegir los nombres de ícono: `PROBE_MIN=1 PROBE_MAX=1`
dibuja la tira en estado "restaurar" y ahí se ve, por ejemplo, que `window-restore` para los dos
botones son dos rombos idénticos y que `view-restore` a 14 px es un borrón.

**Y ojo con lo que el hook mide: `root.previewUuid` y `appPreview.visible` son estado GLOBAL**,
no del delegate que los imprime. Solo hay un preview a la vez, así que el reporte de cualquier
ícono ve el que puso *otro*, y un widget con la casilla apagada aparece en el log como
`visible=true` — que se lee exactamente como una fuga de la reja y no lo es (2026-08-17,
probando las previews de *Apps Seleccionables*). Lo que sí atribuye es **el uuid**: si el único
que llegó a mostrarse pertenece a una app que solo lista el bloque habilitado, la reja aguantó.
`grep -o 'uuid=\[{[^}]*}\]' | sort | uniq -c` contra la salida de `--dump-captures` lo resuelve
de una.

**3. La instancia aislada va con `hideMode=3`** (*las ventanas pasan abajo*), no con
`autohide=true`: es visible —hace falta para capturar la pantalla— y **no reserva espacio**, así
que no le mueve ninguna ventana al usuario. `autohide=true` no le mueve nada pero tampoco deja
ver nada, y `hideMode=0` le re-acomoda las maximizadas.

Y la comprobación del requisito que da nombre a la feature —que la micro ventana **no** sea un
toplevel, o sea que no pueda salir en el dock ni en una barra de tareas— es un `--dump-captures`
**con el preview en pantalla**: enumera todo lo que KWin reporta como ventana, y el preview no
tiene que estar. Ojo con el `grep`: acá hay una Konsole cuyo *título* contiene "kdock" y da un
falso positivo.

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
