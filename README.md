**Español** | [English](README.en.md) | [中文](README.zh-CN.md)

# kdock  ·  RELEASE 0.1.11

![Ejemplo de configuración de Kdock](screenshots/nueva-portada.jpg)

*Ejemplo de configuración de Kdock*

**Dock y panel para escritorios Wayland**, escrito 100 % en Qt 6.

[![CI](https://github.com/yacolinux/kdock/actions/workflows/ci.yml/badge.svg)](https://github.com/yacolinux/kdock/actions/workflows/ci.yml)
[![Licencia: MIT](https://img.shields.io/badge/licencia-MIT-blue.svg)](LICENSE)
![Qt 6.5+](https://img.shields.io/badge/Qt-6.5%2B-41cd52)
![Wayland](https://img.shields.io/badge/Wayland-layer--shell-lightgrey)

kdock **no enlaza contra KDE Frameworks ni contra Plasma**. Los protocolos Wayland se
generan directamente desde sus XML con `qtwaylandscanner`, y todo lo demás se resuelve por
D-Bus o por CLI. El resultado son seis binarios sueltos, sin plugins que instalar y sin
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
  tamaño, widgets, anclados) y manejo de hotplug. Hasta 6 docks por pantalla.
- **Y docks distintos por escritorio virtual** (KDE/KWin): además de variar de monitor a
  monitor, un dock puede atarse a uno o varios escritorios. La regla es por monitor: en un
  escritorio que tiene docks propios se ven **esos y solo esos**, y un monitor sin docks
  propios sigue mostrando los de siempre. O sea que mientras no ates ninguno, nada cambia:
  los docks actuales se ven en todos los escritorios, como hasta ahora. Se configura en
  *Docks*, marcando escritorios o con **Duplicar para el escritorio…**, que clona el dock
  en el mismo monitor para empezar a diferenciarlo. Y desde el clic derecho, **Dock → Crear
  dock vacío** agrega uno con los valores por defecto en el mismo monitor: si el escritorio
  en el que estás ya tiene docks propios, el nuevo se ata a él para que aparezca en el acto.
  **Dock → Mover Sig. Monitor**, también del clic derecho, lleva el dock al siguiente monitor
  conectado (y da la vuelta al llegar al último): la configuración viaja con él y el archivo
  viejo queda renombrado como respaldo, limpiándose solo en el próximo move. Justo debajo,
  **Dock → Copiar a Sig. Monitor** hace lo mismo pero **sin tocar el original**: el dock se
  queda donde está y el monitor siguiente recibe una copia idéntica, lista para diferenciarla
  (lo único que la copia no hereda es el nombre y, si los dos van a estar en pantalla a la
  vez, la bandeja del sistema — que se dibujaría dos veces).
  Arriba del diálogo, un selector de **escritorio** y el **nombre** del dock dicen en todo
  momento cuál de todos estás editando; abrir Configuración desde un dock preselecciona el
  suyo. Los docks de un escritorio **no se
  construyen hasta la primera vez que entrás ahí** —así no se paga la memoria de un dock que
  nunca vas a ver— y a partir de entonces cambiar de escritorio solo los muestra y los
  esconde.
- **Un fondo de pantalla distinto por escritorio virtual**, que Plasma no ofrece: su fondo es
  de la pantalla, no del escritorio. kdock lo consigue reescribiéndolo en el momento del
  cambio. El **Escritorio 1 sigue siendo de KDE**: su configuración —el slideshow con sus
  carpetas e intervalos, o lo que tengas— se guarda sola y vuelve cada vez que regresás a él,
  y también cuando kdock se cierra. Los escritorios 2 a 5 llevan, cada uno, **un modo propio**:
  *Estático* (una imagen fija por monitor, el diálogo de archivos de KDE, el que previsualiza
  imágenes) o *Slideshow* (el plugin de KDE enraizado en una **carpeta por monitor**, con el
  intervalo configurable en segundos, 5 minutos por defecto). Los modos son **excluyentes por
  escritorio**: uno puede correr un slideshow mientras otro muestra una imagen fija. Un monitor
  al que no le asignes nada no se toca. Viene **apagado**: mientras no lo prendas, kdock no le
  escribe una sola clave a tu escritorio.
- **Cuatro modos de ocultamiento**, en Configuración → General: *Siempre visible* (reserva el
  espacio, las ventanas nunca lo tapan), *Ocultar cuando no se usa* (el auto-ocultado de
  siempre: se desliza afuera y vuelve al acercar el mouse), **Ocultamiento inteligente** (queda
  a la vista mientras nada lo moleste y se esconde solo cuando una ventana llega a su
  rectángulo — el *dodge* de Latte) y **Las ventanas pasan por debajo** (siempre visible pero
  sin reservar espacio, así una ventana maximizada usa el borde entero y pasa por abajo).
  Los tres últimos no piden *exclusive zone*, así que no te recortan el área de maximizado. El
  ocultamiento inteligente mira solo las ventanas del **escritorio virtual actual** e ignora las
  minimizadas; con alineación centrada esquiva sobre la franja del borde entero, porque en
  Wayland el compositor —no el cliente— decide dónde queda una superficie centrada.
- **Modos de presentación**: flotante o panel de borde a borde, modo compacto, alineación
  inicio/centro/fin, opacidad, color de fondo, largo fijo o automático.
- **Colores rápidos**: ocho colores de fondo configurables, aplicables al vuelo desde el
  submenú *Color de fondo* del clic derecho. La paleta es **común a todos los docks** y vive
  en la configuración compartida, así que se edita una vez y sobrevive a los reinicios; el
  color elegido, en cambio, es de cada dock.
- **Etiquetas de íconos** en seis disposiciones (debajo, al costado, solo nombre…) con
  medición del nombre más largo para que el dock no desperdicie grosor. Opcionalmente en
  **negrita**, con re-medición para que el nombre más ancho siga entrando.
- **Nombres en dos renglones** (opcional): un nombre más largo que su caja se **envuelve** en
  vez de cortarse con puntos suspensivos, así se lee entero — *Nombre Ficticio de Aplicación*
  en lugar de *Nombre Ficticio de A…*. Vale para los nombres de apps y de widgets a la vez, y
  el grosor del dock sigue el cambio: la caja pasa a medir dos alturas de línea y la *zona
  exclusiva* de layer-shell se recalcula, así que las ventanas maximizadas no quedan tapadas.
- **Modo oscuro** conmutable por dock, o para todos de una con lista de excepciones: fondo
  oscuro, un único color de resaltado para los nombres y para las apps corriendo, e íconos
  de widgets resueltos contra el set oscuro. Es un **override**, no una reescritura: el
  esquema de colores que ya tenías queda intacto en el `.conf` y reaparece tal cual al
  volver a Normal. Se cambia desde su solapa, desde el submenú *Modo* del clic derecho o
  desde su propio widget. Opcionalmente arrastra consigo el **esquema de color
  y el iconset de KDE** y el iconset del propio dock: como eso sí es estado del escritorio y
  no se puede "dejar de aplicar", cada opción guarda el valor de los dos modos.
- **ColorAuto**: un **esquema de color de KDE generado a partir del fondo de pantalla**, que
  se rehace cada vez que el fondo cambia. Saca el color predominante de la imagen con el
  mismo método barato con el que el dock ya tiñe un ícono, y arma con él un esquema completo
  — pero al revés de lo que suelen hacer estas herramientas, **el contraste manda sobre el
  color**: cada texto y cada botón se empujan hasta cumplir una razón WCAG contra el fondo
  sobre el que se apoyan (AAA para el texto), así que el resultado se lee sea cual sea la
  foto. El color de selección se elige aparte y por omisión es lo que mejor contrasta: gris
  oscuro con letra blanca en los esquemas claros, gris claro con letra negra en los oscuros
  — o un color propio, o el complementario del fondo. Claro u oscuro lo decide la luminancia
  media de la imagen, con la opción de forzarlo. Los **docks se pintan por monitor**, cada
  uno con su propio fondo, y como el modo oscuro es un override: tu color de panel queda
  intacto y vuelve al desactivar. El **esquema del sistema** es uno solo y sigue al monitor
  que elijas (por omisión, el mismo que maneja la rueda del brillo). El esquema generado es
  **temporal**: se reescribe en cada cambio y se borra al apagar la opción. Y se lleva bien
  con el modo oscuro: mientras aquel está puesto, ColorAuto se aparta y vuelve solo al salir.
  Se configura en su propia solapa, para todos los docks a la vez.
- **«Generar Color» a mano**, que anda con ColorAuto apagado: desde su solapa, desde un widget
  del dock (clic izquierdo genera, clic derecho abre la configuración) o desde la tarjeta del
  panel de control. Apretarlo otra vez sobre el mismo fondo **pasa al siguiente color de esa
  imagen** —las candidatas se separan por tono, así que cada clic se ve— y se puede ir
  probando hasta que guste. Cuando uno convence, **Guardar Color Scheme** lo deja permanente
  en tu carpeta de esquemas como `kdock-1`, `kdock-2`… y lo aplica: a partir de ahí es tuyo,
  ni apagar ColorAuto ni reiniciar el dock lo pisan.
- **Auto-encogido**: cuando no entran todos los íconos, el dock reduce escala y fuente en vez
  de recortar.
- **Drag & drop** para reordenar íconos y secciones enteras.
- **Cinco clases de separador**, y ninguna se dibuja igual. Entre secciones, desde el clic
  derecho de cualquier widget: el **dinámico** (*spring*), que se estira y empuja el resto
  hacia el otro extremo; el **estático**, un hueco fijo con una línea fina; y el
  **transparente**, que además recorta el fondo del dock y su región de entrada — se ve el
  escritorio por el medio y los clics pasan, así que un dock se lee como dos. El transparente
  puede además **fijarse a un ancho**, uno por separador (solapa *Diseño*): así deja de
  repartir el sobrante y mide los píxeles que le digas, que es la forma de partir un dock en
  dos bloques pegados en vez de mandarlos a los extremos. Y **dentro del
  bloque de aplicaciones**, hasta dos separadores más (solapa *Layout*), que dividen los
  lanzadores anclados entre sí y del resto de las ventanas: cada uno puede ser una línea o
  **transparente**, o sea espacio en blanco sin línea, con el fondo del dock intacto detrás.
- **Mandar ventanas a otro escritorio virtual** desde el clic derecho de un ícono de
  aplicación: *Escritorio → Ventana aquí* la trae al escritorio actual, y *Enviar a…* la manda
  a otro **sin seguirla** — vos te quedás donde estás. Mueve todas las ventanas de esa app,
  igual que *Cerrar todas*.
- **Íconos y colores del sistema**: `QIcon::fromTheme` + esquema de color leído de
  `~/.config/kdeglobals` con recarga en vivo. El fondo de las apps corriendo se tiñe con el
  color dominante del propio ícono.
- **Un solo selector de temas**, en el dock y en la configuración: en una instalación completa
  hay ~180 iconsets y ~450 esquemas de color, así que en vez de un desplegable con esa tira de
  nombres, cada selector abre una lista **con buscador y vista previa** — tres íconos de muestra
  renderizados *en ese* iconset, o tres muestras de color del esquema. Cada fila tiene un
  casillero de **favoritos** que la sube al tope; los favoritos son una sola lista compartida
  por todos los docks y por el diálogo. Y un casillero **«Mantener abierta»** para probar temas
  uno tras otro sin reabrir la ventana en cada cambio.
- **Bandeja del sistema** (StatusNotifierItem + DBusMenu, implementados acá). Va en el dock
  que elijas —cualquiera, no solo el principal— aunque en uno solo por vez: dos bandejas a la
  vista duplicarían cada ícono. Docks que nunca se ven juntos, en cambio, pueden tener cada
  uno la suya, así un escritorio virtual con docks propios no se queda sin bandeja.
- **Docks de solo widgets**: apagando los íconos de aplicaciones (General → *Íconos de apps*),
  un dock queda como barra de widgets, bandeja y lanzadores propios, y adelgaza a la medida
  de lo que le queda.
- **Estilo Qt para todo kdock**: un desplegable en *General* elige el widget style del proceso
  — `Breeze`, `Fusion`…— para todos los docks y binarios a la vez. Vacío = el que Qt decida
  (Breeze en KDE, Fusion en otros escritorios). Se aplica al reiniciar.
- **Tooltips globales**: un casillero en *General* prende o apaga todos los tooltips de todos
  los docks a la vez, para no pagar sus ventanas si no los querés.
- **Panel de configuración** en Qt Widgets, con una solapa por área y cada una teñida de un
  color distinto para ubicarse de un vistazo (es lo que se ve en la captura de arriba). La solapa
  *Docks* lista todos los que tenés configurados —cada uno con su monitor, su borde y sus
  escritorios— y deja renombrarlos con doble clic; el alias se agrega al nombre automático,
  así que el monitor nunca se pierde. Por defecto esconde los docks y los monitores que no
  están conectados, con un casillero para volver a verlos.
- **Menú de aplicaciones** estilo Kickoff: categorías XDG, búsqueda, favoritos y pie de
  sesión. Muestra además los **submenús propios**: los que se arman con el editor de menús
  de KDE y los que crean los navegadores para sus web apps — declarados en los archivos
  `.menu` de XDG, que listan sus miembros por nombre de archivo y no por categoría (de
  hecho, los `.desktop` de las web apps no traen `Categories` en absoluto). El clic derecho
  sobre el botón del menú abre el editor de menús, configurable. **Todas las filas de la
  barra lateral llevan ícono**, tanto las categorías freedesktop como los submenús propios,
  con alternativas por si el set de íconos elegido no trae alguno de los nombres estándar.
- **Vista de lista o de grilla** en el menú, de 1 a 8 columnas, con tamaño de ícono y
  espaciado regulables: las celdas se calculan desde el ícono, así que unas pocas apps no
  se desparraman por todo el popup.

#### Interfaz traducible, en archivos de texto

Los textos escritos en el código son la **capa nativa, "capabase"**. Encima se carga una
traducción: un archivo `.md` de texto plano en `~/.local/share/kdock/translations/`, que se
elige y se edita desde la solapa *Traducciones* del panel de configuración (botón **Editar** →
se abre con tu editor de texto predeterminado; al guardar, el dock toma los cambios solo, sin
reiniciar nada). Vienen **español**, **inglés** y **chino simplificado (zh-CN)**, y se irán
agregando más idiomas.

Cada archivo tiene cuatro títulos:

| Título | Qué traduce | Clave |
|---|---|---|
| `Configuracion` | El panel de configuración | el texto capabase |
| `UIdock` | Menús, tooltips y popups del dock | el texto capabase |
| `Widgets` | Los nombres de los widgets | el token del widget (`clock`, `pager`…) |
| `Apps` | Los nombres de las aplicaciones | el id del `.desktop` |

Lo que una traducción no incluya cae al texto de capabase — y para las apps, al `Name=` de su
`.desktop` —, así que un archivo incompleto es perfectamente válido: se traduce lo que se
quiera y el resto sigue funcionando. Un widget que hayas **renombrado a mano** conserva ese
nombre en todos los idiomas. El botón *Actualizar apps* llena la sección `Apps` con todas las
aplicaciones instaladas, listas para renombrar, y *Nueva…* duplica una traducción para armar
la propia: eso es todo lo que hace falta para agregar un idioma, sin recompilar.

Para el chino hace falta una fuente CJK instalada (Noto Sans CJK o WenQuanYi); sin ella el
sistema dibuja cuadraditos, y no es algo que kdock pueda resolver.

Vienen además nueve capas **ALT**, con los nombres cambiados por apodos: tres juegos —
`*-ALT-startrek` (naves de Star Trek), `*-ALT-hacker` (apodos de hacker) y `*-ALT-starwars`
(hardware de Star Wars) para los widgets— por tres idiomas de base, `english-`, `spanish-` y
`zh-CN-`. Las nueve comparten el mismo reparto de la sección `Apps`: inteligencias
artificiales de novelas de ciencia ficción y herramientas de hackeo ficticias. En inglés y en
español los apodos son nombres propios y quedan igual; **en chino se traducen también**
(冬寂 Wintermute, 企业号 Enterprise, 匡氏十一型 Kuang Mark Eleven), que es la forma de que la
broma se lea en el idioma de la interfaz. Sirven además de ejemplo completo de las cuatro
secciones — sobre todo de `Apps`, que es la única que se llena con ids de `.desktop` reales.

| | Star Trek | Hacker | Star Wars |
|---|---|---|---|
| Discos | Reliant · 可靠号 | Phantom Phreak · 幽灵飞客 | Astromech · 宇航技工机 |
| Red | Excelsior · 卓越号 | Phiber Optik · 光纤客 | Comlink · 通讯器 |
| Volumen | Intrepid · 无惧号 | Blade · 利刃 | Sonic Emitter · 声波发射器 |
| Modo oscuro | Equinox · 春分号 | Nightfall · 夜幕 | Carbonite · 碳素冷冻 |

Y en la sección `Apps`, las mismas en las nueve: Dolphin es *Wintermute* (冬寂), Konsole
*Mycroft Holmes* (迈克罗夫特), Edge *Kuang Mark Eleven* (匡氏十一型) — el icebreaker de
*Neuromante* —, Wireshark *Packet Ripper* (数据包撕裂者). En total se entregan **trece
capas**: capabase, los tres idiomas y las nueve ALT.

### Widgets

Todos opcionales y reordenables. Los backends hablan D-Bus o CLI directo, nunca KDE
Frameworks:

| Widget | Qué hace | Backend |
|---|---|---|
| Volumen | Sink por defecto: scroll, mute, nivel | `wpctl` / `pactl` (PipeWire) |
| Brillo | Brillo de pantalla | `brightnessctl` |
| Batería | Carga, estado y perfil de energía | UPower + power-profiles-daemon |
| Discos | Unidades extraíbles: montar, desmontar, expulsar, abrir | UDisks2 |
| Red | Mini-ventana con tres solapas: **Wi-Fi** (redes cercanas, unirse con contraseña), **Guardadas** y **Detalles** (IP, máscara, puerta de enlace, DNS, MAC, IPv6). Botón *Configurar redes…* al editor completo | NetworkManager |
| Clima | Ícono de la condición y temperatura; el clic abre la mini-app del pronóstico. Varias ciudades guardadas, con una activa | Open-Meteo (HTTPS, sin API key) |
| Portapapeles | Historial de texto e imágenes con búsqueda, persistente, capturado en segundo plano | `ext-data-control-v1` |
| Iconset de KDE | Cambia el iconset de todo el escritorio, dejando intacto el del propio dock | `plasma-changeicons` |
| Esquema de color | Aplica un esquema de color de KDE a todo el escritorio | `plasma-apply-colorscheme` |
| Reloj | 12/24 h, fecha, segundos (dos variantes) | — |
| Overview | Abre el efecto Overview de KWin | kglobalaccel |
| Mover ventana | Al siguiente escritorio virtual, o al siguiente monitor (clic derecho: al anterior) | kglobalaccel |
| MaxMin | Maximiza (clic izquierdo) o minimiza (clic derecho) la ventana activa | kglobalaccel |
| Cerrar ventana | Cierra la ventana activa; con clic derecho la manda al escritorio siguiente sin cambiar de escritorio | Protocolo del compositor + KWin (D-Bus) |
| Siguiente fondo | Avanza la presentación de fondos de un monitor | D-Bus de Plasma |
| Sesión | Cerrar sesión, reiniciar, apagar, bloquear, suspender | D-Bus de KDE |
| Mostrar escritorio | Toggle de "mostrar escritorio" | Protocolo del compositor |
| Escritorios | Paginador mínimo: un número por escritorio virtual, clic para ir; el actual va resaltado | KWin (D-Bus) |
| Auto-ocultar | Prende y apaga el auto-ocultado (los otros dos modos se eligen en Configuración) | — |
| Modo oscuro | Clic izquierdo: esquema normal; clic derecho: modo oscuro | — |
| Relanzadores | Mini-dock anidado: un ícono despliega una barra con otros lanzadores | — |
| Script Runner | Ejecuta un script shell configurable | `sh` |
| Menú de mosaicos | Abre y cierra el menú de pantalla completa | `kdock-tilemenu` (D-Bus) |
| Control Manager | Abre el panel de control: audio, brillo por monitor, energía, calendario, reproducción, red, clima, fondos y sistema. Puede dibujar texto propio (o el reloj) en el dock, con su fuente | `kdock-controlmanager` (D-Bus) |

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
  renombrarlo o cambiarle el ícono sin tocar el `.desktop`. Los nombres van en negrita por
  omisión, conmutable desde el panel.
- **Grupos como solapas**: cada sección puede partirse en grupos con nombre, que aparecen
  como una barra de solapas arriba, abajo o a un costado (a elección), y solo cuando hay más
  de uno. Un mosaico pasa a otro grupo **arrastrándolo sobre su solapa** o desde su menú
  contextual → *Mover a grupo*, que además ofrece crear un grupo nuevo con ese mosaico
  adentro. El panel de configuración trae un editor de grupos —una sección por vez— para
  crearlos, renombrarlos, ordenarlos y quitarlos.
- **Al soltar no pasa nada raro**: si la celda está libre, va; si la ocupa un mosaico del mismo
  tamaño, se intercambian; cualquier otro caso se rechaza y el mosaico vuelve a su lugar. Nada
  se reacomoda solo porque moviste a un vecino, y un fantasma de color te dice cuál de los tres
  casos es *antes* de soltar.
- **Buscador**, **índice alfabético** lateral (en las secciones que no acomodaste a mano),
  barra de categorías —con ícono en todas sus filas— a izquierda o derecha, u oculta; fila de
  sesión, y navegación por teclado.
- **Mantener abierto**: un casillero en la esquina que apaga los cuatro caminos de cierre
  automático (Esc, la ✕, perder el foco, lanzar una app). Con él puesto el menú queda como una
  ventana más, que se puede mandar al fondo y recuperar con el alt-tab.
- La disposición es **una sola para toda la sesión** —todos los docks abren el mismo menú— y se
  exporta e importa como JSON.

Se lanza en el primer clic del widget y queda residente, así que las aperturas siguientes son
instantáneas. A diferencia de `kdock-previews`, **no necesita ningún permiso especial de
KWin**.

### `kdock-calendar` (binario accesorio)

**Calendario de mes standalone**, pensado para abrirse desde el widget del reloj (el clic del
reloj puede apuntar a este binario y el segundo clic lo cierra) o desde un Script Runner. El
widget del reloj no se toca: es un toplevel normal aparte, sin configuración propia.

- **Aspecto KDE con números grandes**: semana **lunes-primero**, fecha local, **día actual
  resaltado** en una píldora con el color de acento, selección con anillo, y días de otros
  meses / fines de semana atenuados. La grilla 7×6 toma todo el espacio y los números escalan
  con la celda, así que crece bien en ventanas grandes.
- **Pintado a mano en Qt Widgets** (nada de QML ni KDE Frameworks): usa la paleta del sistema,
  por lo que sigue el tema claro/oscuro y el acento KDE automáticamente.
- **Navegación**: flechas ‹ › en el encabezado, rueda del mouse, `PageUp`/`PageDown` y el botón
  **Hoy** para volver al mes actual. El pie muestra la fecha larga del día seleccionado.
- **`kdock-calendar --month AAAA-MM`** arranca en otro mes (por defecto, el actual).
- Igual que `kdock-tilemenu`, **no necesita ningún permiso especial de KWin**: corre directo
  desde `build/` y su `.desktop` solo da nombre e ícono en el gestor de tareas.

### `kdock-controlmanager` (binario accesorio)

**Panel de control anclado a un borde de la pantalla** — el lugar donde ver y tocar todo
junto: audio, brillo por monitor, perfil de energía, modo oscuro, calendario, reproducción,
red, clima, fondo de escritorio y sistema. Con su propio árbol (`controlmanager/`),
su propia configuración y su propio panel de ajustes; lo prende y lo apaga el widget
*Control Manager* del dock.

- La primera solapa (**Principal**) es una **grilla de tarjetas** que se reacomodan y
  redimensionan arrastrando (1×1…6×3, cada una con su color de fondo opcional), y cada
  sección tiene además su solapa propia — **la misma tarjeta en los dos tamaños**. Las celdas
  pueden ser más altas que anchas, así las tarjetas se configuran en ancho y alto.
- **Audio**: el mezclador completo — salidas, entradas y streams por app, con predeterminado,
  mute y el techo de 150 %.
- **Video y energía**: un slider de brillo **por monitor** (PowerDevil), perfil de energía y
  el modo oscuro del dock, en vivo por D-Bus.
- **Calendario**: mes navegable, con botón para abrir `kdock-calendar`.
- **Reproducción**: carátula, título/artista, barra de progreso y transporte para cualquier
  reproductor MPRIS2.
- **Red**: lo mismo que el widget del dock, solapa por solapa — redes cercanas (con la fila de
  contraseña para unirse), conexiones guardadas y los datos de la conexión (IP, máscara,
  puerta de enlace, DNS, MAC).
- **Clima**: condición actual, pronóstico de N días y detalles, sobre la misma configuración
  que el widget del dock y la mini-app.
- **Fondo de escritorio**: avanza la presentación de cada monitor.
- **Sistema**: las cuatro configuraciones (KDE, kdock, mosaicos, previews), reinicios y la
  fila de sesión con etiquetas (bloquear, suspender, cerrar sesión, reiniciar, apagar).
- **Apariencia**: fondo (tema / color / imagen), opacidad, esquinas redondeadas, negritas,
  **un solo tamaño de fuente para todo el panel**, iconset propio, tamaño mínimo de los
  botones y la posición de las solapas. El panel se **redimensiona arrastrando sus esquinas**,
  como una ventana estándar.
- Cierra con Esc, la ✕ o al perder el foco — todo anulable con **Ventana permanente**, que lo
  deja como un panel fijo.

Es una superficie layer-shell como el dock: **no pide ningún permiso especial de KWin**, su
`.desktop` solo da nombre e ícono, y no hace falta refrescar el índice de aplicaciones.


### `kdock-weather` (binario accesorio)

**El clima, en su propia ventana**: ubicación, temperatura grande, ícono de la condición,
flecha de viento con su velocidad, y abajo dos solapas — **N días** (día, ícono, probabilidad
de lluvia, máxima y mínima) y **Detalles** (sensación térmica, punto de rocío, humedad,
presión, ráfaga, visibilidad, amanecer y atardecer). Lo abre el widget *Clima* del dock, el
botón de la tarjeta del panel, o `kdock-weather` a secas.

- **Los datos son de [Open-Meteo](https://open-meteo.com)**: HTTPS y JSON, **sin API key y sin
  registro**. Su buscador de ciudades es parte del mismo servicio, así que kdock no empaqueta
  ninguna lista de lugares: se escribe el nombre y se elige entre los resultados (con provincia
  y país, que es lo que distingue las seis ciudades que se llaman igual).
- **Varias ciudades guardadas, una activa.** La activa es la que muestran el widget del dock y
  la tarjeta del panel; cambiarla en la mini-app llega a las otras dos superficies sin
  reiniciar nada.
- **Unidades a elección**: °C/°F y m/s, km/h o mph. Todo se pide en las unidades del proveedor
  y se convierte al dibujar, así que cambiarlas es instantáneo y no gasta un pedido.
- **Muestra lo último que sabe y pregunta después.** La respuesta se guarda en disco, así que
  el widget tiene temperatura en el primer frame tras un reinicio del dock. Si se cae la red no
  se vacía: atenúa el dato viejo, lo dice en el pie y reintenta con espera creciente.
- **Tamaño de fuente configurable**: toda la ventana escala con él —los íconos acompañan al
  texto— y la ventana crece para que nada quede cortado. Se ajusta en su propio diálogo o desde
  *Configuración → Fuentes* del dock, que junta la tipografía de las tres superficies.
- **Instancia única pero no residente**: el clic del widget la abre y el siguiente la cierra, y
  al cerrarse el proceso termina. Como `kdock-tilemenu` y `kdock-calendar`, **no pide ningún
  permiso especial de KWin**.
- Sin ciudad configurada el widget del dock **igual se muestra**, con un ícono genérico: el clic
  lleva derecho a elegirla.

---

## Requisitos

**Para compilar** — solo Qt 6 (≥ 6.5) y CMake/Ninja:

```sh
sudo apt install qt6-base-dev qt6-declarative-dev qt6-wayland-dev \
                 qt6-wayland-private-dev cmake ninja-build
```

Módulos de Qt: Core, Gui, Qml, Quick, Widgets, DBus, **Network** (solo para el clima) y
WaylandClient (más los headers
privados de WaylandClient, que la integración layer-shell necesita). En runtime hacen falta
**dos módulos QML**: `QtQuick.Controls` y `Qt5Compat.GraphicalEffects` (paquete
`qml6-module-qt5compat-graphicaleffects` en Debian/Ubuntu, `qt6-5compat` en Arch), que el
dock y las tarjetas de vista previa usan para las sombras. **Sin el segundo el dock no
arranca**: el QML no carga y la ventana queda vacía.

**En runtime**, cada dependencia es opcional y solo apaga su widget si falta: `wpctl` o
`pactl` (volumen y mezclador), `brightnessctl` (brillo), UDisks2 (discos), NetworkManager
(red), UPower (batería). El clima es lo único que sale a internet, y solo si configurás una
ciudad. Los widgets de Overview, mover-ventana y siguiente-fondo solo
aparecen bajo KDE.

## Tests

```bash
tests/run.sh                 # static + unit + qml (~1 min), lo mismo que corre en CI
tests/run.sh --tier live     # además, contra tu sesión Wayland y KWin
```

Cuatro tiers en ctest: **static** (invariantes del repo: qmllint, los `.qml` declarados en
el qrc, el catálogo de traducciones al día), **unit** (Qt Test contra los mismos objetos con
los que se compila el dock), **qml** (los binarios de verdad bajo Xvfb: el QML carga *y*
dibuja — se verifica también la geometría de la ventana, porque un log en silencio también
es lo que se ve cuando el QML nunca se instanció) y **live** (el *intelligent hide* contra
ventanas reales y el multi-monitor, que necesitan un compositor y por eso no corren en CI).

Cada corrida usa un `XDG_DATA_HOME` descartable y herramientas de sistema falsas en el
`PATH`, así que **no te toca el brillo, el tema ni la configuración**. Detalle en
[`tests/README.md`](tests/README.md).

## Compilar e instalar

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
sudo cmake --install build     # instala los seis binarios y sus .desktop
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
sea, sin `.desktop` y sin refrescar el índice. Lo mismo vale para **`kdock-calendar`** y para
**`kdock-controlmanager`** (su superficie layer-shell es pública).

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
  color de fondo, agregar separador, configuración. Cada entrada lleva su ícono, **incluidas
  las cabeceras de los submenús** (*Escritorio*, *Ubicación*, *Color de fondo*, *Modo*,
  *Texto App*, *Texto Widget*, *Dock*).
- **Arrastrar** — reordenar íconos y secciones.

En los widgets el clic derecho abre ese mismo menú, salvo en los que lo usan para una
segunda acción (volumen → mezclador, mover a monitor → monitor anterior, MaxMin →
minimizar, cerrar ventana → mandarla al escritorio siguiente). En todos, **Shift + clic
derecho** abre el menú.

La configuración vive en el directorio de datos XDG:

```
~/.local/share/kdock/
  kdock.conf                  # opciones compartidas (relanzadores, script runners, tema,
                              #   favoritos de iconsets y esquemas de color…)
  kdock-<monitor>[-<n>].conf  # un archivo por dock
  previews.conf               # kdock-previews (compartido + uno por pantalla)
  tilemenu.conf               # kdock-tilemenu: opciones y disposición de los mosaicos
  clipboard-history.txt
```

*Configuración → Presets* guarda todo eso como un preset con nombre (`presets/<nombre>.zip`),
lo aplica de una —reemplaza los `.conf` y reinicia kdock con sus accesorios— y lo exporta o
importa como un `.zip`.

## Base de rendimiento (RELEASE 0.1)

El dock arranca en ~240 MB RSS con el juego de docks del escritorio actual; cada escritorio
virtual adicional suma ~57 MB la primera vez que se entra (construcción diferida), y a partir
de ahí el RSS es plano — cambiar de escritorio solo muestra y oculta, nunca crea ni destruye.

**Popups y tooltips.** Cada `ToolTip`, `Menu` y `Popup` con `popupType: Popup.Window` abre su
propia `QQuickWindow` con un `QSGRenderThread` exclusivo y dos hilos del driver Mesa por
contexto GL. QtQuick.Controls no los suelta después de cerrarlos, así que en una sesión de
varias horas con media docena de docks el proceso acumulaba **68 hilos de render + 136 hilos
Mesa** y un **heap de 571 MB**, sin techo. Desde esta versión:

- Los **popups de clic** (el menú de aplicaciones, el portapapeles, los discos, la red y los
  selectores de tema) se cargan bajo demanda con `Loader { active: false }` y se destruyen
  automáticamente tras **30 segundos sin uso**, con el patrón medido en `tilemenu/`.

- Los **tooltips** son por elemento (comportamiento original): la API adjunta que los hacía
  compartir una sola instancia por ventana se probó y se descartó, porque posiciona el tooltip
  en el cursor (parpadea en docks horizontales) y sin parent adecuado cae detrás de las
  maximizadas en docks verticales. A cambio, se agregó un **interruptor global de tooltips**
  (*Configuración → General → Tooltips*) que los apaga por completo si molestan o si el
  usuario prefiere no pagar la memoria.

El `ToolTip` del reloj mejorado mantiene su diseño personalizado (contentItem propio).

| | Antes (11 h / 6 docks) | Después |
|---|---|---|
| `QSGRenderThread` | 68 | ~4-6 (uno por ventana dock visible) |
| Hilos Mesa | 136 | ~8-12 |
| Heap | 571 MB | ~180 MB (estimado) |
| Buffers i915 | 529 MB | ~120 MB (estimado) |

## Estructura del repositorio

| Ruta | Qué es |
|---|---|
| `src/` | El dock: backends, modelo, configuración, diálogo de opciones |
| `qml/` | La UI del dock y sus popups |
| `previews/` | Binario accesorio de vistas previas (árbol propio, reusa 5 archivos de `src/`) |
| `tilemenu/` | Binario accesorio del menú de mosaicos (árbol propio, reusa 8 archivos de `src/`) |
| `calendar/` | Binario accesorio del calendario de mes (árbol propio, autocontenido) |
| `controlmanager/` | Binario accesorio del panel de control (árbol propio, reusa 16 archivos de `src/`) |
| `weather/` | Binario accesorio del clima (árbol propio, reusa 5 archivos de `src/`) |
| `protocols/` | Protocolos Wayland vendoreados (layer-shell, foreign-toplevel, plasma-window, xdg-shell) |
| `translations/` | Las capas de traducción (`capabase.md` + un `.md` por idioma). Se copian al home en el primer arranque y se editan ahí |
| `tests/` | La suite: `run.sh` y cuatro tiers en ctest (`static`, `unit`, `qml`, `live`). Ver `tests/README.md` |
| `.github/` | El workflow de CI: compila y corre los tres tiers portables en un container con Qt reciente |
| `tools/` | `gen-capabase.py` (reconstruye `capabase.md` desde el código), `sync-translations.py` (propaga las claves nuevas a los demás idiomas) y `gen-alt-layers.py` (regenera las nueve capas ALT) |
| `screenshots/` | Capturas del README. `.gitignore` ignora `*.jpg`/`*.png` a propósito (una captura de escritorio muestra de más): las de acá se revisaron una por una y se agregaron con `git add -f` |
| `AGENTS.md` | Documento de arquitectura: cada widget, las trampas de Wayland, la tabla QML↔C++ |
| `CLAUDE.md` | Cómo compilar, probar e instalar; arneses de prueba sin GUI |

## Limitaciones conocidas

- **Wayland es el objetivo.** En X11 el binario corre como ventana normal, sin struts ni
  reserva de espacio.
- **Wi-Fi Enterprise (802.1X/EAP)**: el editor de redes cubre WPA/WPA2/WPA3-Personal, redes
  abiertas y WEP. Una conexión con 802.1X se muestra pero no se puede guardar desde acá.
- **Portapapeles**: guarda texto e imágenes. Otros formatos (archivos, HTML enriquecido) no.
- **El espacio reservado no es por escritorio virtual.** KWin calcula el área de maximizado
  por monitor, no por escritorio, así que si los docks de dos escritorios reservan distinto
  espacio (otro borde, otro grosor), al cambiar de escritorio las ventanas maximizadas se
  re-acomodan en todos. kdock corrige esto: tras cada cambio, re-ejecuta el maximizado de las
  ventanas que ya lo estaban (con reintentos escalonados), así que recuperan su tamaño incluso
  con docks de distinto grosor. Se desactiva con el casillero de Configuración → General o con
  `KDOCK_NO_WINDOW_ACTIONS=1`.
- **Fondos por escritorio**: se configuran los escritorios 2 a 5 (el 1 es el de KDE); a partir
  del 6 kdock no toca nada. Y al volver al Escritorio 1, si tenías un slideshow, vuelve con su
  configuración intacta pero **mostrando la imagen siguiente** — recargarlo lo hace avanzar, y
  un slideshow avanza solo de todas formas.
- **ColorAuto se entera de cualquier cambio de fondo**, lo haya hecho kdock o no: vigila el
  archivo de configuración de Plasma, así que también ve el pase de diapositivas de KDE
  avanzando solo, un script hablando D-Bus con plasmashell o un cambio desde Preferencias del
  Sistema. Lo que **no** hace es rehacer el esquema cuando ese archivo cambia sin que el fondo
  haya cambiado (Plasma lo reescribe por muchos otros motivos): si el resultado sería idéntico
  al que ya está puesto, no toca nada. Además deja **un** esquema temporal visible en la lista de
  Preferencias del Sistema: es el que está aplicado, y no se puede evitar porque
  `plasma-apply-colorscheme` solo sabe aplicar esquemas que existan como archivo.
- **Traducciones**: el dock, su panel de configuración y los paneles de `kdock-previews` y
  `kdock-tilemenu` siguen el idioma elegido. `kdock-calendar` es la excepción: al ser
  autocontenido (sin archivos de `src/`), sigue en capabase.
- La integración layer-shell usa headers **privados** de QtWaylandClient (igual que
  layer-shell-qt): una versión mayor nueva de Qt puede pedir ajustes.

## Licencia

MIT — ver [LICENSE](LICENSE).
