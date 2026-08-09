Here is Claude's plan:
╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌
 kdock — ventanas maximizadas al cambiar de escritorio + costo de los popups

 Context

 Dos problemas distintos, los dos disparados por la misma feature (docks por escritorio
 virtual, 2026-08-05), y los dos medidos sobre la instancia real (PID 175137, 11 h de vida,
 6 docks vivos sobre eDP-1 + DP-1) el 2026-08-07.

 1. Al cambiar de escritorio las ventanas quedan desmaximizadas. DockManager::sync()
 muestra el dock entrante antes de ocultar el saliente (dockmanager.cpp:548-559), a
 propósito, para no dejar un frame sin dock. El precio es que durante ese instante los dos
 docks reservan zona exclusiva sobre el mismo output, así que KWin achica el área de
 trabajo y re-acomoda las maximizadas; cuando el saliente suelta su zona, el área vuelve a
 crecer pero las ventanas no se recuperan. AGENTS.md (Docks por escritorio virtual) hoy
 dice que esto es a propósito —"el gestor de ventanas se encarga"— y la observación del
 usuario lo desmiente: hay que agregar una pasada de remediación y corregir esa nota.

 2. kdock acumula ventanas de popup que no se destruyen nunca. Perfil medido:
 1,03 GB RSS + 349 MB en swap, 859 MB private dirty, heap de 571 MB, 353 threads, de
 los cuales 68 son QSGRenderThread (uno por QQuickWindow) y 136 son del driver Mesa
 (dos por contexto GL). Además 529 MB de buffers i915 (drm-total-system0 en
 /proc/<pid>/fdinfo).

 El histograma de nacimiento de esos 68 threads (leído de /proc/<pid>/task/*/stat, campo 22)
 parte el problema en dos mitades, y las dos hay que atacarlas:

 ┌─────────────────────────────────────────┬─────────┬───────────────────────────────────────────────────────────────┐
 │                 patrón                  │ threads │                             causa                             │
 ├─────────────────────────────────────────┼─────────┼───────────────────────────────────────────────────────────────┤
 │ ráfagas de 6-7 en 4 instantes puntuales │ ~27     │ cada DockWindow nuevo crea ~6 ventanas de golpe               │
 ├─────────────────────────────────────────┼─────────┼───────────────────────────────────────────────────────────────┤
 │ nacimientos sueltos repartidos en 11 h  │ ~41     │ cada menú/tooltip/popup que el usuario abrió y que sigue vivo │
 └─────────────────────────────────────────┴─────────┴───────────────────────────────────────────────────────────────┘

 O sea: no es una fuga sin techo, pero crece monótonamente con el uso y nunca baja. La
 causa está identificada en el propio código y las dos correcciones tienen precedente medido
 dentro del repo (CLAUDE.md → Trampas que muerden, el caso del menú de mosaicos: 783 MB →
 245 MB con exactamente estos dos cambios).

 Alcance acordado con el usuario: para el punto 2, solo popups perezosos. Nada de
 QT_QUICK_BACKEND=software, ni QSG_RENDER_LOOP=basic, ni compartir el QQmlEngine.

 ---
 Parte 1 — Re-maximizar al cambiar de escritorio

 Diagnóstico previo (obligatorio antes de escribir el fix)

 No hay que adivinar qué le queda a la ventana: el arnés de Xvfb no sirve (sin KWin no hay
 maximizado ni escritorios), así que va una sonda en la sesión real que imprima el estado
 crudo. Linkea windowmonitor.cpp + plasmawindowmonitor.cpp + virtualdesktops.cpp y en
 cada currentChanged vuelca, por ventana: appId, el bitmask de state_changed y la
 geometría. Necesita el .desktop temporal con X-KDE-Wayland-Interfaces (receta de
 CLAUDE.md → Depurar agrupación de ventanas), y no abre ninguna ventana.

 Lo que decide el fix: si tras el cambio la ventana pierde el bit maximized (0x4), alcanza
 con re-pedir el estado; si lo conserva pero con geometría chica, hay que hacer el ciclo
 set_state(MAXIMIZED, 0) → set_state(MAXIMIZED, MAXIMIZED), porque KWin ignora un pedido
 de un estado que ya tiene.

 Implementación

 a) Exponer el estado que ya llega y no se lee — src/windowmonitor.h /
 src/plasmawindowmonitor.{h,cpp}. El evento org_kde_plasma_window_state_changed(flags)
 (plasmawindowmonitor.cpp:89) hoy extrae solo active/minimized/skipTaskbar. Agregar:

 - bool maximized  ← ORG_KDE_PLASMA_WINDOW_MANAGEMENT_STATE_MAXIMIZED (0x4)
 - bool maximizable ← ..._STATE_MAXIMIZABLE (0x400) — este es el filtro de diálogos
 que pidió el usuario: un diálogo no es maximizable, así que no hace falta inventar una
 heurística por título ni por tipo de ventana (el protocolo no publica el tipo).
 - virtual void maximize() en AbstractWindow (no-op por defecto, como unminimize()), con
 la implementación de PlasmaWindow copiando el idiom de activate()/minimize().

 b) Servicio nuevo src/desktopmaximize.{h,cpp}, calcado de DesktopWallpapers: ctor
 DesktopMaximize(VirtualDesktops *, WindowMonitor *), instanciado en main.cpp junto a
 desktopWallpapers (src/main.cpp:203-206). No viaja en Shared ni toca
 dockwindow.* — ningún dock lo usa, igual que DesktopWallpapers.

 Lógica, enganchada a VirtualDesktops::currentChanged:

 1. Antes del cambio ya se viene manteniendo, por uuid, qué ventanas estaban maximizadas (el
 WindowMonitor recibe state_changed continuamente; basta escuchar changed()).
 2. Tras el cambio, correr la pasada con retardo escalonado, reusando el idiom que ya está
 en DockWindow (dockwindow.cpp:177, for (int delay : {200, 700, 1500})): la pasada
 tiene que caer después de que el dock saliente soltó su zona exclusiva, y ese instante
 no tiene señal propia.
 3. Para cada ventana del escritorio actual que estaba maximizada: saltear si minimized,
 skipTaskbar o !maximizable, y re-pedir el maximizado (ciclo o pedido simple según lo
 que diga el diagnóstico).

 Ojo con la convención ya documentada: en AbstractWindow::desktops la lista vacía significa
 "en todos los escritorios", no "en ninguno" (windowmonitor.h:23-26) — una ventana anclada
 a todos entra en la pasada de cualquier escritorio.

 c) Reja para el arnés. CLAUDE.md advierte que un proceso bajo Xvfb sí llega al bus
 real, así que cualquier cosa colgada de currentChanged actúa sobre la sesión del usuario. A
 diferencia de los wallpapers, acá la acción es benigna (re-maximiza lo que ya estaba
 maximizado), así que no va apagada por defecto —es una corrección de bug y tiene que
 andar sin que el usuario configure nada—: va con dos frenos.

 - Un enabled en el kdock.conf compartido, default true, con casillero en el diálogo.
 - Un escape duro por entorno, KDOCK_NO_WINDOW_ACTIONS=1, que los arneses setean y que hace
 el objeto inerte. Hay que agregarlo a las recetas de CLAUDE.md.

 ---
 Parte 2 — Popups perezosos

 Dos cambios en qml/Dock.qml, los dos con precedente medido en tilemenu/.

 a) Los ToolTip por sección → el ToolTip adjunto

 qml/Dock.qml:918 declara un ToolTip { popupType: Popup.Window } dentro del delegate de
 sectionRepeater, o sea uno por sección (más los de las líneas 1294, 1674, 1766, 1846,
 1941, 2005, 2103, 2167…). Cada uno es una QQuickWindow con su contexto GL.

 El arreglo es el mismo que bajó el menú de mosaicos de 783 MB a 245 MB: las propiedades
 adjuntas ToolTip.text / ToolTip.visible / ToolTip.delay, que comparten una sola
 instancia por ventana. Precedente exacto a copiar: tilemenu/qml/TileMenuTile.qml:187-189
 y tilemenu/qml/TileGroupTabs.qml:192-194. Es un cambio de tres líneas por sitio y no
 cambia nada de lo que se ve.

 b) Los popups de clic → Loader { active: false } + apagado por inactividad

 Los pesados (AppMenuPopup :1893, ClipboardPopup :2058, DisksPopup :2121, NetworkPopup
 :2214, los dos ThemeListPopup :2508 y :2572, RelanzadorPopup) y los Menu de sección
 (sectionMenu, qml/Dock.qml:926, uno por sección) se instancian aunque no se abran nunca —
 y una vez abiertos, su ventana queda viva para siempre.

 Patrón, uno por popup:

 Loader {
     id: disksLoader
     active: false
     property double closedAt: 0          // vive en el Loader, no en el popup:
                                          // tiene que sobrevivir a la desactivación
     sourceComponent: DisksPopup { /* … igual que hoy … */ }
     Timer { id: idle; interval: 30000; onTriggered: disksLoader.active = false }
 }

 Tres detalles que hay que respetar:

 - closedAt sube al Loader. Es el anti-rebote del clic (disksPopup.closedAt,
 Dock.qml:2113-2117) y si se destruye con el popup, el primer clic tras la desactivación
 reabre el popup que se acaba de cerrar.
 - No desactivar en el onClosed: destruiría el popup en plena animación de cierre. De ahí
 el Timer de inactividad (30 s: reabrir enseguida sigue siendo gratis, y un popup que quedó
 olvidado suelta su ventana).
 - Los id cambian a loader.item en los MouseArea que hoy hacen disksPopup.open() /
 .visible / .close(). Conviene envolverlo en un function toggle() del propio Loader
 (que además activa antes de abrir) en vez de repetir el active = true en cada sitio.

 Para el sectionMenu vale además la otra mitad del precedente del menú de mosaicos: un solo
 Menu en la raíz, parametrizado por qué sección lo abrió, en vez de uno por fila. Es el
 cambio de mayor rendimiento de los dos, pero también el más invasivo (el menú lee sec.index,
 sec.isSpring, sec.isStaticSep): si al medir el Loader perezoso ya alcanza, no hace falta.

 Restricciones a no romper

 - popupType: Popup.Window no se toca en ningún caso: en Wayland, sin eso los popups
 quedan recortados dentro de la superficie del dock (CLAUDE.md, Popups y menús).
 - El ancho a mano de cada Menu se conserva (width: Math.max(implicitWidth + 16, 220)):
 un Menu de QtQuick.Controls no se dimensiona a su ítem más largo y le corta la última letra.
 - Todo .qml nuevo va al qt_add_resources del CMakeLists.txt de arriba, o no entra al qrc.

 ---
 Verificación

 Antes de tocar nada, la línea de base, con los mismos comandos con los que salió el
 diagnóstico (todos read-only sobre el PID vivo):

 for t in /proc/$(pgrep -x kdock)/task/*; do cat $t/comm; done | sort | uniq -c | sort -rn
 grep -E "^(Rss|Private_Dirty|Swap):" /proc/$(pgrep -x kdock)/smaps_rollup
 grep drm-total /proc/$(pgrep -x kdock)/fdinfo/*      # buffers i915

 Parte 2 bajo Xvfb (no necesita KWin): correr el arnés de CLAUDE.md con una config de
 prueba de varios docks, contar QSGRenderThread y el RSS, abrir y cerrar los popups con
 xdotool y volver a contar. El criterio de éxito es que el conteo baje tras los 30 s de
 inactividad — que es justo lo que hoy no pasa. Acordarse de las dos trampas del arnés
     │ xdotool y volver a contar. El criterio de éxito es que el conteo baje tras los 30 s de                                │
     │ inactividad — que es justo lo que hoy no pasa. Acordarse de las dos trampas del arnés                                 │
     │ (QT_QPA_PLATFORM=xcb, no offscreen; escribir las dos copias del .conf) y de que                                       │
     │ qmllint no ve los errores de scope de ids, así que la corrida del arnés es obligatoria:                               │
     │                                                                                                                       │
     │ qmllint qml/*.qml 2>&1 | grep -iE "error|syntax"                                                                      │
     │                                                                                                                       │
     │ Parte 1 solo en la sesión real (Xvfb no tiene KWin: currentPosition() es 0 y la pasada                                │
     │ no corre). Segunda instancia aislada en el bus real —dbus-run-session acá no sirve,                                   │
     │ perdés currentChanged— con autohide=true para no moverle las ventanas al usuario, el                                  │
     │ PATH falso de /tmp/fakebin para tapar BrightnessControl, y dos docks de distinto grosor                               │
     │ atados a los escritorios 1 y 2. Secuencia: maximizar dos ventanas en el escritorio 1, anotar                          │
     │ su geometría, ir al 2 y volver, y comparar. Capturar con spectacle-custom -b -n -f -o                                 │
     │ (el spectacle de /usr/bin no está autorizado en esta máquina). Volver al escritorio                                   │
     │ olvidado suelta su ventana).                                                                 │
     │ - Los id cambian a loader.item en los MouseArea que hoy hacen disksPopup.open() /            │
     │ .visible / .close(). Conviene envolverlo en un function toggle() del propio Loader           │
     │ (que además activa antes de abrir) en vez de repetir el active = true en cada sitio.         │
     │                                                                                              │
     │ Para el sectionMenu vale además la otra mitad del precedente del menú de mosaicos: un solo   │
     │ Menu en la raíz, parametrizado por qué sección lo abrió, en vez de uno por fila. Es el       │
     │ cambio de mayor rendimiento de los dos, pero también el más invasivo (el menú lee sec.index, │
     │ sec.isSpring, sec.isStaticSep): si al medir el Loader perezoso ya alcanza, no hace falta.    │
     │                                                                                              │
     │ Restricciones a no romper                                                                    │
     │                                                                                              │
     │ - popupType: Popup.Window no se toca en ningún caso: en Wayland, sin eso los popups          │
     │ quedan recortados dentro de la superficie del dock (CLAUDE.md, Popups y menús).              │
     │ - El ancho a mano de cada Menu se conserva (width: Math.max(implicitWidth + 16, 220)):       │
     │ un Menu de QtQuick.Controls no se dimensiona a su ítem más largo y le corta la última letra. │
     │ - Todo .qml nuevo va al qt_add_resources del CMakeLists.txt de arriba, o no entra al qrc.    │
     │                                                                                              │
     │ ---                                                                                          │
     │ Verificación                                                                                 │
     │                                                                                              │
     │ Antes de tocar nada, la línea de base, con los mismos comandos con los que salió el          │
     │ diagnóstico (todos read-only sobre el PID vivo):                                             │
     │                                                                                              │
     │ for t in /proc/$(pgrep -x kdock)/task/*; do cat $t/comm; done | sort | uniq -c | sort -rn    │
     │ grep -E "^(Rss|Private_Dirty|Swap):" /proc/$(pgrep -x kdock)/smaps_rollup                    │
     │ grep drm-total /proc/$(pgrep -x kdock)/fdinfo/*      # buffers i915                          │
     │                                                                                              │
     │ Parte 2 bajo Xvfb (no necesita KWin): correr el arnés de CLAUDE.md con una config de         │
     │ prueba de varios docks, contar QSGRenderThread y el RSS, abrir y cerrar los popups con       │
     │ xdotool y volver a contar. El criterio de éxito es que el conteo baje tras los 30 s de       │
     │ inactividad — que es justo lo que hoy no pasa. Acordarse de las dos trampas del arnés        │
     │ (QT_QPA_PLATFORM=xcb, no offscreen; escribir las dos copias del .conf) y de que              │
     │                                                                                                                       │
     │ Parte 1 solo en la sesión real (Xvfb no tiene KWin: currentPosition() es 0 y la pasada                                │
     │ no corre). Segunda instancia aislada en el bus real —dbus-run-session acá no sirve,                                   │
     │ perdés currentChanged— con autohide=true para no moverle las ventanas al usuario, el                                  │
     │ PATH falso de /tmp/fakebin para tapar BrightnessControl, y dos docks de distinto grosor                               │
     │ atados a los escritorios 1 y 2. Secuencia: maximizar dos ventanas en el escritorio 1, anotar                          │
     │ su geometría, ir al 2 y volver, y comparar. Capturar con spectacle-custom -b -n -f -o                                 │
     │ (el spectacle de /usr/bin no está autorizado en esta máquina). Volver al escritorio                                   │
     │ original y matar la instancia por PID de pgrep -x kdock excluyendo el                                                 │
     │ /usr/local/bin/kdock del usuario.                                                                                     │
     │                                                                                                                       │
     │ Al instalar: backup con timestamp primero (binario + .desktop + los .conf), y                                         │
     │ saltear kbuildsycoca6 — no cambia ningún .desktop ni ningún privilegio. Reiniciarle                                   │
     │ el dock al usuario le deja la pantalla sin dock, así que preguntar antes.                                             │
     │                                                                                                                       │
     │ Al terminar: documentar las dos cosas en AGENTS.md —y en particular corregir la nota                                  │
     │ de Docks por escritorio virtual que hoy afirma que kdock no hace nada con las maximizadas                             │
     │ a propósito—, y llevar a CLAUDE.md la trampa nueva (los popups Popup.Window cuestan un                                │
     │ QSGRenderThread + un contexto GL cada uno y no se liberan solos) junto con la receta del                              │
     │ histograma de nacimiento de threads, que es lo que la destapó.