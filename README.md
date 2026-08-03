# kdock

![Ejemplo de configuración de Kdock, Kdock visible en borde superior](image.png)

*Ejemplo de configuración de Kdock, Kdock visible en borde superior*

**Dock y panel para escritorios Wayland**, escrito 100 % en Qt 6.

[![Licencia: MIT](https://img.shields.io/badge/licencia-MIT-blue.svg)](LICENSE)
![Qt 6.5+](https://img.shields.io/badge/Qt-6.5%2B-41cd52)
![Wayland](https://img.shields.io/badge/Wayland-layer--shell-lightgrey)

kdock **no enlaza contra KDE Frameworks ni contra Plasma**. Los protocolos Wayland se
generan directamente desde sus XML con `qtwaylandscanner`, y todo lo demás se resuelve por
D-Bus o por CLI. El resultado son tres binarios sueltos, sin plugins que instalar y sin
arrastrar medio Plasma como dependencia.

Probado a diario en **KDE Plasma 6 / KWin**; la barra de tareas también funciona en
compositores wlroots (sway, hyprland, wayfire) por `wlr-foreign-toplevel-management-v1`.

---

## Galería

El dock, en distintos bordes y disposiciones de etiqueta:

| | |
|---|---|
| ![Dock al fondo, panel completo con etiquetas debajo](screenshots/dock-bottom-labels.jpg)<br>*Abajo, de borde a borde, etiqueta debajo del ícono* | ![Dock vertical a la izquierda, solo íconos](screenshots/dock-left-icons-only.jpg)<br>*Izquierda, compacto, solo íconos* |
| ![Dock vertical a la izquierda con etiquetas](screenshots/dock-left-labels.jpg)<br>*Izquierda, etiqueta al costado* | ![Dock vertical a la izquierda con ícono y etiqueta invertidos](screenshots/dock-left-labels-reversed.jpg)<br>*La misma, con el orden ícono/etiqueta invertido* |
| ![Dock vertical a la derecha con etiquetas](screenshots/dock-right-labels.jpg)<br>*Derecha, con los widgets abajo* | ![Dock arriba con etiquetas al costado](screenshots/dock-top-labels.jpg)<br>*Arriba, etiqueta al costado* |
| ![Dock arriba en modo compacto](screenshots/dock-top-compact.jpg)<br>*Arriba, compacto, etiqueta encima* | |

## Características

### El dock

- **Panel real de compositor** vía `wlr-layer-shell-v1`, con *exclusive zone* (el
  equivalente Wayland de los struts de X11) para que las ventanas maximizadas no lo tapen.
  La integración layer-shell es código propio compilado como plugin estático de Qt Wayland
  (`src/layershell.cpp`); los diálogos y menús se delegan solos a xdg-shell.
- **Barra de tareas** con dos backends elegidos en runtime:
  `org_kde_plasma_window_management` (KWin) y `wlr-foreign-toplevel-management-v1` (wlroots).
- **Lanzadores anclados** desde archivos `.desktop` XDG, parseados a mano, con agrupación de
  ventanas por aplicación (incluidas las **web apps de Chromium/Edge**, que reportan un
  `app_id` deformado y necesitan heurísticas propias).
- **Un dock por monitor**, opt-in: cada uno con su configuración independiente (borde,
  tamaño, widgets, anclados) y manejo de hotplug. Hasta 3 docks por pantalla.
- **Modos de presentación**: flotante o panel de borde a borde, modo compacto, alineación
  inicio/centro/fin, opacidad, color de fondo, largo fijo o automático.
- **Colores rápidos**: ocho colores de fondo configurables, aplicables al vuelo desde el
  submenú *Color de fondo* del clic derecho. La paleta es **común a todos los docks** y vive
  en la configuración compartida, así que se edita una vez y sobrevive a los reinicios; el
  color elegido, en cambio, es de cada dock.
- **Etiquetas de íconos** en seis disposiciones (debajo, al costado, solo nombre…) con
  medición del nombre más largo para que el dock no desperdicie grosor. Opcionalmente en
  **negrita**, con re-medición para que el nombre más ancho siga entrando.
- **Modo oscuro** conmutable por dock, o para todos de una con lista de excepciones: fondo
  oscuro, un único color de resaltado para los nombres y para las apps corriendo, e íconos
  de widgets resueltos contra el set oscuro. Es un **override**, no una reescritura: el
  esquema de colores que ya tenías queda intacto en el `.conf` y reaparece tal cual al
  volver a Normal. Se cambia desde su solapa, desde el submenú *Modo* del clic derecho o
  desde su propio widget. Opcionalmente arrastra consigo el **esquema de color
  y el iconset de KDE** y el iconset del propio dock: como eso sí es estado del escritorio y
  no se puede "dejar de aplicar", cada opción guarda el valor de los dos modos.
- **Auto-encogido**: cuando no entran todos los íconos, el dock reduce escala y fuente en vez
  de recortar.
- **Drag & drop** para reordenar íconos y secciones enteras, con separadores dinámicos
  (*springs*) que empujan el resto hacia el otro extremo.
- **Íconos y colores del sistema**: `QIcon::fromTheme` + esquema de color leído de
  `~/.config/kdeglobals` con recarga en vivo. El fondo de las apps corriendo se tiñe con el
  color dominante del propio ícono.
- **Bandeja del sistema** (StatusNotifierItem + DBusMenu, implementados acá).
- **Panel de configuración** en Qt Widgets, con una solapa por área y cada una teñida de un
  color distinto para ubicarse de un vistazo (es lo que se ve en la captura de arriba).
- **Menú de aplicaciones** estilo Kickoff: categorías XDG, búsqueda, favoritos y pie de
  sesión. Muestra además los **submenús propios**: los que se arman con el editor de menús
  de KDE y los que crean los navegadores para sus web apps — declarados en los archivos
  `.menu` de XDG, que listan sus miembros por nombre de archivo y no por categoría (de
  hecho, los `.desktop` de las web apps no traen `Categories` en absoluto). El clic derecho
  sobre el botón del menú abre el editor de menús, configurable.
- **Vista de lista o de grilla** en el menú, de 1 a 8 columnas, con tamaño de ícono y
  espaciado regulables: las celdas se calculan desde el ícono, así que unas pocas apps no
  se desparraman por todo el popup.

### Widgets

Todos opcionales y reordenables. Los backends hablan D-Bus o CLI directo, nunca KDE
Frameworks:

| Widget | Qué hace | Backend |
|---|---|---|
| Volumen | Sink por defecto: scroll, mute, nivel | `wpctl` / `pactl` (PipeWire) |
| Brillo | Brillo de pantalla | `brightnessctl` |
| Batería | Carga, estado y perfil de energía | UPower + power-profiles-daemon |
| Discos | Unidades extraíbles: montar, desmontar, expulsar, abrir | UDisks2 |
| Red | Conexiones guardadas y switch de Wi-Fi | NetworkManager |
| Portapapeles | Historial de texto con búsqueda, persistente | `QClipboard` |
| Reloj | 12/24 h, fecha, segundos (dos variantes) | — |
| Overview | Abre el efecto Overview de KWin | kglobalaccel |
| Mover ventana | Al siguiente escritorio virtual, o al siguiente monitor (clic derecho: al anterior) | kglobalaccel |
| MaxMin | Maximiza (clic izquierdo) o minimiza (clic derecho) la ventana activa | kglobalaccel |
| Cerrar ventana | Cierra la ventana activa; con clic derecho la manda al escritorio siguiente sin cambiar de escritorio | Protocolo del compositor + KWin (D-Bus) |
| Siguiente fondo | Avanza la presentación de fondos de un monitor | D-Bus de Plasma |
| Sesión | Cerrar sesión, reiniciar, apagar, bloquear, suspender | D-Bus de KDE |
| Mostrar escritorio | Toggle de "mostrar escritorio" | Protocolo del compositor |
| Auto-ocultar | Prende y apaga el auto-ocultado | — |
| Modo oscuro | Clic izquierdo: esquema normal; clic derecho: modo oscuro | — |
| Relanzadores | Mini-dock anidado: un ícono despliega una barra con otros lanzadores | — |
| Script Runner | Ejecuta un script shell configurable | `sh` |
| Menú de mosaicos | Abre y cierra el menú de pantalla completa | `kdock-tilemenu` (D-Bus) |

### `kdock-previews` (binario accesorio)

Tiras de **vistas previas de ventanas** en un borde de pantalla — una tarjeta por ventana con
una captura real de su contenido, clic para activarla (la referencia es el Stage Manager de
macOS). Es un segundo binario con su propio árbol (`previews/`), su propia configuración y su
propio multimonitor; corre *junto* al dock, que solo lo prende, lo apaga y le abre su panel
desde *Configuración → Previews*. Usa `org.kde.KWin.ScreenShot2`, así que es **solo para
KWin**.

- **Clic en una tarjeta** = toggle de foco como una barra de tareas: restaura y enfoca la
  ventana, y un segundo clic sobre la que ya está al frente la minimiza. Clic medio = cerrar.
- **Auto-ajuste de tarjetas**: con muchas ventanas las tarjetas se encogen para entrar en el
  largo de la tira en vez de desbordar a scroll, y vuelven al tamaño configurado al cerrarlas
  (con un tamaño mínimo regulable). El **grosor de la tira acompaña a las tarjetas**: la tira
  se adelgaza cuando estas se encogen y recupera su tamaño al crecer, así las miniaturas
  siempre llenan la tira sin banda vacía a los lados (y la zona reservada del escritorio va
  con ella).
- **Desplazamiento**: rueda del mouse, arrastre con inercia y *fling* afinados para moverse
  entre tarjetas grandes sin esfuerzo, y una barra de desplazamiento fina opcional en el borde
  interior de la tira.
- **Tamaño y ubicación**: cualquiera de los cuatro bordes, grosor regulable de 48 a 800 px
  (el panel lo llama *alto* o *ancho* según el borde), largo fijo o de punta a punta, y
  alineación inicio/centro/fin — que mueve la tira si tiene largo propio, o las tarjetas
  dentro de ella si ocupa todo el borde.
- Capturas **una por ventana al pasar a primer plano** (o refresco periódico, experimental),
  filtros por monitor / escritorio virtual / minimizadas, y auto-ocultado opcional.

### `kdock-tilemenu` (binario accesorio)

**Menú de aplicaciones a pantalla completa**, con los íconos en una matriz que se reacomoda
arrastrándolos (la referencia es el applet Tiled Menu de Plasma, y de ahí el nombre). Es un
tercer binario con su propio árbol (`tilemenu/`), su propia configuración y su propio panel;
lo prende y lo apaga el widget *Menú de mosaicos* del dock.

Ocupa **todo el escritorio libre: todo menos los docks y paneles visibles**, estén en el borde
que estén y sean los que sean. No hay una línea de geometría para lograrlo — la ventana es un
toplevel normal *maximizado*, y el compositor ya sabe restar el espacio que los paneles
reservan. Un dock con auto-ocultado sale gratis por el mismo camino: el menú cubre la pantalla
entera y el dock se dibuja encima al aparecer.

- **Cada sección recuerda su propia disposición** — Favoritos, "Todas", cada categoría y cada
  submenú `.menu`. Arrancan ordenadas solas por nombre y pasan a ser tuyas en cuanto arrastrás
  el primer mosaico. Es la diferencia con el applet de referencia, donde solo el lienzo de
  anclados se puede acomodar a mano.
- **Mosaicos de varios tamaños** (1×1, 2×1, 1×2, 2×2, 4×2, 4×4), cada uno con su color o
  imagen de fondo, y con el ícono y el nombre opcionales por separado — además de poder
  renombrarlo o cambiarle el ícono sin tocar el `.desktop`.
- **Grupos con etiqueta**: bandas con título dentro del lienzo, que se renombran, se
  reordenan, se colapsan y reciben mosaicos arrastrados.
- **Al soltar no pasa nada raro**: si la celda está libre, va; si la ocupa un mosaico del mismo
  tamaño, se intercambian; cualquier otro caso se rechaza y el mosaico vuelve a su lugar. Nada
  se reacomoda solo porque moviste a un vecino, y un fantasma de color te dice cuál de los tres
  casos es *antes* de soltar.
- **Buscador**, **índice alfabético** lateral (en las secciones que no acomodaste a mano),
  barra de categorías a izquierda o derecha —u oculta—, fila de sesión, y navegación por
  teclado.
- **Mantener abierto**: un casillero en la esquina que apaga los cuatro caminos de cierre
  automático (Esc, la ✕, perder el foco, lanzar una app). Con él puesto el menú queda como una
  ventana más, que se puede mandar al fondo y recuperar con el alt-tab.
- La disposición es **una sola para toda la sesión** —todos los docks abren el mismo menú— y se
  exporta e importa como JSON.

Se lanza en el primer clic del widget y queda residente, así que las aperturas siguientes son
instantáneas. A diferencia de `kdock-previews`, **no necesita ningún permiso especial de
KWin**.


---

## Requisitos

**Para compilar** — solo Qt 6 (≥ 6.5) y CMake/Ninja:

```sh
sudo apt install qt6-base-dev qt6-declarative-dev qt6-wayland-dev \
                 qt6-wayland-private-dev cmake ninja-build
```

Módulos de Qt: Core, Gui, Qml, Quick, Widgets, DBus y WaylandClient (más los headers
privados de WaylandClient, que la integración layer-shell necesita). En runtime hace falta
`QtQuick.Controls`.

**En runtime**, cada dependencia es opcional y solo apaga su widget si falta: `wpctl` o
`pactl` (volumen y mezclador), `brightnessctl` (brillo), UDisks2 (discos), NetworkManager
(red), UPower (batería). Los widgets de Overview, mover-ventana y siguiente-fondo solo
aparecen bajo KDE.

## Compilar e instalar

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
sudo cmake --install build     # instala los tres binarios y sus .desktop
```

> Si tu entorno exporta `CC="ccache gcc"` / `CXX="ccache g++"`, CMake AutoMoc falla:
> configurá con `env -u CC -u CXX cmake …`.

## Permisos en KWin (importante)

KWin trata la lista de ventanas y las capturas como **interfaces privilegiadas**, y solo se
las concede a un proceso cuyo `.desktop` **instalado** las declare. Resuelve
`/proc/<pid>/exe` contra el `Exec=` **absoluto** de ese archivo, que por eso lo genera CMake
en tiempo de instalación:

```ini
# kdock.desktop
X-KDE-Wayland-Interfaces=org_kde_plasma_window_management

# kdock-previews.desktop — necesita las dos
X-KDE-Wayland-Interfaces=org_kde_plasma_window_management
X-KDE-DBUS-Restricted-Interfaces=org.kde.KWin.ScreenShot2
```

Después de instalar hay que refrescar el índice para que KWin encuentre los `.desktop`:

```sh
kbuildsycoca6
```

`kdock-tilemenu` no aparece acá porque no pide nada privilegiado: se puede correr desde donde
sea, sin `.desktop` y sin refrescar el índice.

Si ejecutás uno de los otros dos desde otra ruta (`build/kdock` durante desarrollo), copiá el
`.desktop` a `~/.local/share/applications/` con el `Exec=` apuntando a la ruta absoluta de
*ese* binario. Sin esto el dock arranca igual, pero sin lista de ventanas; y
`kdock-previews`, sin la segunda clave, deja todas las tarjetas con el ícono de la app en vez
de la captura — parece un bug de render y es autorización.

En compositores wlroots no hace falta nada: `zwlr_foreign_toplevel_manager_v1` es público.

## Uso

```sh
kdock
```

- **Clic izquierdo** — lanzar / enfocar / minimizar / ciclar entre ventanas.
- **Clic medio** — nueva instancia.
- **Clic derecho** — menú contextual: lista de ventanas, anclar/desanclar, cerrar, etiquetas,
  color de fondo, agregar separador, configuración.
- **Arrastrar** — reordenar íconos y secciones.

En los widgets el clic derecho abre ese mismo menú, salvo en los que lo usan para una
segunda acción (volumen → mezclador, mover a monitor → monitor anterior, MaxMin →
minimizar, cerrar ventana → mandarla al escritorio siguiente). En todos, **Shift + clic
derecho** abre el menú.

La configuración vive en el directorio de datos XDG:

```
~/.local/share/kdock/
  kdock.conf                  # opciones compartidas (relanzadores, script runners, tema…)
  kdock-<monitor>[-<n>].conf  # un archivo por dock
  previews.conf               # kdock-previews (compartido + uno por pantalla)
  tilemenu.conf               # kdock-tilemenu: opciones y disposición de los mosaicos
  clipboard-history.txt
```

*Configuración → Backup* exporta e importa todo eso como un `.zip`.

## Estructura del repositorio

| Ruta | Qué es |
|---|---|
| `src/` | El dock: backends, modelo, configuración, diálogo de opciones |
| `qml/` | La UI del dock y sus popups |
| `previews/` | Binario accesorio de vistas previas (árbol propio, reusa 5 archivos de `src/`) |
| `tilemenu/` | Binario accesorio del menú de mosaicos (árbol propio, reusa 8 archivos de `src/`) |
| `protocols/` | Protocolos Wayland vendoreados (layer-shell, foreign-toplevel, plasma-window, xdg-shell) |
| `screenshots/` | Capturas del README. `.gitignore` ignora `*.jpg`/`*.png` a propósito (una captura de escritorio muestra de más): las de acá se revisaron una por una y se agregaron con `git add -f` |
| `AGENTS.md` | Documento de arquitectura: cada widget, las trampas de Wayland, la tabla QML↔C++ |
| `CLAUDE.md` | Cómo compilar, probar e instalar; arneses de prueba sin GUI |

## Limitaciones conocidas

- **Wayland es el objetivo.** En X11 el binario corre como ventana normal, sin struts ni
  reserva de espacio.
- **Wi-Fi**: solo se activan conexiones ya guardadas. Unirse a una red nueva escribiendo la
  contraseña necesita un agente de secretos de NetworkManager, que no está implementado.
- **Portapapeles**: en Wayland una app solo puede leer el portapapeles mientras tiene el foco
  del teclado, y la superficie del dock es inerte al teclado. La solución correcta es
  `wlr-data-control-unstable-v1`; todavía no está.
- La integración layer-shell usa headers **privados** de QtWaylandClient (igual que
  layer-shell-qt): una versión mayor nueva de Qt puede pedir ajustes.

## Licencia

MIT — ver [LICENSE](LICENSE).
