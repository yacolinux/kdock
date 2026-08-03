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

**Son tres binarios**, los tres los compila el mismo `cmake --build build`:

- `kdock`;
- `kdock-previews` (`build/previews/kdock-previews`, árbol `previews/`): tiras de vista previa
  de ventanas, se prende desde *Configuración → Previews*. Detalles en `AGENTS.md` →
  *Dock Preview*;
- `kdock-tilemenu` (`build/tilemenu/kdock-tilemenu`, árbol `tilemenu/`): el menú de
  aplicaciones a pantalla completa, lo prende el widget `tilemenu` del dock. Detalles en
  `AGENTS.md` → *Menú de mosaicos*.

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

El install escribe **seis** cosas: los tres binarios y sus tres `.desktop`. Después de
instalar hay que refrescar ksycoca, pero **solo por los dos primeros**: KWin busca ahí los
`.desktop` para conceder los privilegios (sin eso `kdock-previews` se queda sin capturas y las
tarjetas caen a ícono). **`kdock-tilemenu` no necesita el refresco** —no pide ningún
privilegio, y su `.desktop` es solo para el nombre y el ícono del gestor de tareas—, así que si
lo único que tocaste fue el menú de mosaicos, saltealo y evitás el riesgo de abajo. Y ojo:
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

**`kdeglobals` de esta máquina no tiene `General/ColorScheme`** (solo `ColorSchemeHash`), así
que `AppearanceControl::currentColorScheme()` devuelve **vacío** y cualquier combo que se
siembre con él cae en el primer ítem de la lista. Si lo que sigue es aplicar ese valor, le
cambiás el esquema al usuario sin que lo pida: poné una entrada "(no cambiar)" con id vacío
al frente. El iconset sí se lee bien (`Icons/Theme`).

El `main.cpp` de prueba instancia el widget, y con un `QTimer::singleShot` hace
`w.grab().save("/tmp/p/out.png")` y sale; el PNG se lee directo. Vale la pena correrlo dos
veces, una con `app.setPalette()` claro y otra oscuro: el diálogo hereda el esquema de KDE
(`Theme::applyAppPalette`) y un color que se lee bien en uno puede desaparecer en el otro.
Encontró el bug de que las solapas coloreadas se pasaban de los 1000 px del diálogo (las
diez ya piden ~940) y la barra caía en modo flechas de scroll.

**La barra de solapas se desborda cada vez que agregás una.** Volvió a pasar con la solapa
*DarkMode* (2026-08-02): once títulos piden **1086 px** y el `QTabBar` cae en modo flechas
**sin avisar**, escondiendo las últimas — el diálogo pasó a 1120 px. Antes de agregar una
solapa, medí: un `main` de diez líneas que arma un `ColoredTabWidget` con los títulos e
imprime `coloredTabBar()->sizeHint().width()` (no hace falta ni GUI ni captura).

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
en *Bajar* no hacía nada (2026-08-02). Dos apuntes: `findChildren` devuelve en orden de
creación, así que dos botones con el mismo texto se distinguen por índice sin tocar el código
de producción; y no hace falta `app.exec()` si no vas a capturar.

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
  QML. Copiá `qml/` a `/tmp` y usá un import relativo.

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
- **Una entrada de esa fórmula se mide en QML**: `config.effectiveLabelWidth`, el ancho del
  nombre más largo que el dock dibuja (`iconLabelWidth` es solo el tope). `Dock.qml` lo mide
  off-screen y lo reporta con `config.setMeasuredLabelWidth()`; el detalle está en
  `AGENTS.md` → *App-icon labels*. Es la única realimentación QML→C++ del grosor y es segura
  porque el ancho natural de un texto no depende del grosor: **no le agregues un gancho de
  re-medición a `dockThicknessChanged`**, que es justo la señal que emite el setter.
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
- **Nuevo `.qml`**: agregalo a `qt_add_resources` o no entra al qrc. Hay **tres** listas: la
  del `CMakeLists.txt` de arriba (para `qml/`), la de `previews/CMakeLists.txt` (para
  `previews/qml/`) y la de `tilemenu/CMakeLists.txt` (para `tilemenu/qml/`). Esta última tiene
  además un **segundo** `qt_add_resources` con `BASE ".."` que mete `qml/IconMenuItem.qml` de
  kdock **verbatim**: con ese `BASE` cae en `:/qml/`, o sea el mismo directorio del recurso que
  los `.qml` del menú, y se usa como tipo vecino sin ningún import.
- **Popups y menús**: siempre `popupType: Popup.Window`, si no quedan recortados dentro
  de la superficie del dock en Wayland.
- **QML no concatena literales adyacentes como C++.** Un `qsTr("primera parte "` seguido de
  `"segunda parte")` en la línea siguiente —el reflejo de cualquiera que venga del `.cpp`— es
  un **error de sintaxis** (*"Unexpected token `string literal`"*) que tira la carga del
  archivo entero. Va con `+`, o todo en una línea. Lo agarró el arnés al primer intento
  (2026-08-03), pero solo porque el arnés existe: `qmllint` **no lo reportó**.
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
- **El arnés de Xvfb tiene efectos reales sobre la máquina.** No es una caja de arena: el
  `BrightnessControl` reaplica al arrancar el `Brightness/lastBrightness` de la config que le
  des (`brightnessctl set`), así que correr el arnés **te cambia el brillo de la pantalla** al
  valor que tenga ese `.conf`. Lo mismo vale para cualquier backend que escriba al sistema.
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
