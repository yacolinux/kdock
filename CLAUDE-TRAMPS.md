# Trampas que muerden (kdock)

El cuerpo de la sección "Trampas que muerden" de CLAUDE.md, extraído a este archivo
para mantener el contexto principal bajo el límite de tamaño. CLAUDE.md conserva un
índice de los títulos; cuando un cambio toque un área que el índice mencione, abrí
este archivo y leé la entrada correspondiente (o toda la sección).

## Trampas que muerden

- **Un `QDialog` sin parent no lo borra nadie, y el diálogo de kdock colgaba de un `QQuickView`**
  (2026-08-15, encontrado revisando el crash de arriba). `DockWindow::openSettings()` hace
  `new SettingsDialog(...)` **sin parent** —no podría tenerlo: un `QQuickView` es un `QWindow`, no
  un `QWidget`— y `DockWindow` no tenía destructor. O sea que el diálogo **sobrevivía al dock que
  lo abrió**: `DockManager::teardownInstance()` hace `delete inst.window` y el diálogo se quedaba
  en pantalla, editando un dock que ya no existe. Con los docks por escritorio virtual eso es una
  fuga **por cada ida y vuelta** (se destruyen y se rehacen en cada cambio), y cuando el dock
  vuelve, `openSettings()` construye **otro**.
  Y la mitad peligrosa: **`DockManager::removeDock()` hace `delete cfg` del `DockConfig`**, que es
  justo lo que el diálogo guarda en `m_config`. Borrar un dock desde la solapa *Docks* —o desde el
  clic derecho de cualquier dock— dejaba al diálogo con un puntero muerto, y el crash llegaba en
  la **siguiente** interacción, no ahí (ni `reloadDocksList()` ni `updateMonitorsTabButtons()`
  tocan `m_config`, así que el handler que lo borra termina limpio).
  Se arregla por las dos puntas:
  - `~DockWindow()` baja el diálogo, con **`hide()` + `deleteLater()`, nunca `delete`**: el dock
    se destruye rutinariamente desde adentro de un handler del propio diálogo (su solapa *Docks*
    es uno de los dos lugares que borran un dock), así que el objeto no puede desaparecer bajo su
    propio marco de pila. El `hide()` es lo que impide tocarlo en el intervalo.
  - `removeDock()` ahora emite `dockListChanged()` (antes solo lo hacía el move/copy) y
    `SettingsDialog` se defiende: si su `m_dockId` ya no está en `configuredDocks()`, salta a otro
    dock —**prefiriendo uno del mismo monitor**, que es donde el usuario estaba— o cierra si no
    queda ninguno. Esa conexión va **diferida** (`QTimer::singleShot(0, …)`) y con `this` de
    contexto: `this` acá **sí** corresponde, porque es del diálogo entero y no de una solapa, y el
    diferido es porque la señal llega desde adentro del handler del botón que borró el dock —
    saltar de dock rehace las solapas y borraría ese botón mientras su lambda todavía corre.
  **La lección del test, que costó una corrida**: la primera versión afirmaba "no se cayó" y
  **pasaba igual con el arreglo desactivado**, porque leer un `DockConfig` liberado no segfaultea
  de forma confiable. Una aserción sobre un bug de memoria tiene que mirar algo **observable** —
  acá, a qué dock quedan apuntando los combos de la barra— o el control positivo no falla y el
  test no prueba nada.

- **Una conexión de una solapa del diálogo NO puede tener a `this` de contexto** (2026-08-15,
  reportado como *"quise anclar una app en el 2do monitor y se cerró kdock"* — SIGSEGV con
  coredump). `SettingsDialog::buildTabs()` **borra todas las solapas** (`delete w`) y las rehace,
  y lo llama `selectDock()`, o sea cada vez que se cambia de dock: el selector Monitor/Dock/
  Escritorio de la barra de arriba, y también `showMonitorsTab()` (menú *Dock → Nombre*, *Mover/
  Copiar Sig. Monitor*, crear un dock). Pero el `DockConfig` es el del **dock vivo** y sobrevive a
  todo eso, así que un `connect(m_config, …, this, lambda)` **sobrevive a la solapa que lo
  registró** — y esos lambdas capturan punteros crudos a los botones que `buildTabs()` acaba de
  liberar. La próxima señal entra al lambda huérfano y hace `setEnabled()` sobre memoria liberada.
  El caso real: `createLayoutTab()` escuchaba `pinnedChanged` para grisar los botones de
  separadores del bloque de apps, o sea que **cualquier clic de anclar/desanclar en el dock**
  —`DockModel::togglePinned()` → `setPinned()`— mataba el proceso. Se ve como "el dock se cierra
  solo al anclar", y **reiniciar lo arregla** (el diálogo nuevo no tiene huérfanos), lo que manda
  el diagnóstico a cualquier lado menos al diálogo, que ni siquiera hace falta que esté abierto.
  La cura es una palabra: **el contexto va `tab`**, el `QWidget` raíz de la solapa, que es lo que
  `buildTabs()` borra. El propio código ya lo hacía en seis lugares con el comentario *"Bound to
  `tab`, so the connection dies when buildTabs() deletes it"*; faltaba en once.
  Dos corolarios:
  - **Si el connect usaba un puntero a método miembro** (`connect(m_config, sig, this,
    &SettingsDialog::reloadPinnedList)`), el receptor tiene que seguir siendo un `QObject`
    compatible: se envuelve en `connect(sender, sig, tab, [this]{ reloadPinnedList(); })`.
  - **Y ojo con el `disconnect()` de reentrada.** `savePinnedList()`/`saveFavoritesList()` se
    protegían de su propia escritura con un `disconnect(m_config, sig, this, &slot)` alrededor del
    setter: con el receptor mudado al tab, ese `disconnect` **ya no matchea nada** y deja de
    proteger, sin fallar ni avisar. Van con una bandera (`m_writingPinned`/`m_writingFavorites`),
    que además es correcta cuando hay varias solapas vivas.
  - Un connect **dentro** de `buildTabs()` es la otra cara: se acumulaba una conexión más por cada
    cambio de dock. El de `dockListChanged` se mudó adentro de `createMonitorsTab()`.
  Lo congela `tests/unit/tst_settingsdialog.cpp`, que arma el diálogo con dos monitores
  inventados, salta de dock y toca el config del primero. **El control positivo es obligatorio
  acá**: volver *un* connect a `this`, recompilar y confirmar que el test pasa de `Passed` a
  `SEGFAULT` — sin eso no se sabe si el test prueba el bug o mira para otro lado.

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
  geometría, y en Compacto no pasa (margen 0). Lo mismo mordía al arrancar: los
  `setHidden(true)` de las `Behavior` del slider solo corren al terminar una animación, y el
  primer valor escondido lo toma el binding **sin animar**, así que un dock que arranca oculto
  nunca se reportaba como oculto.
- **Y la cura de eso, si mueve la superficie, es peor que la enfermedad** (2026-08-15, reportado
  como *"la animación de autohide hace un bumping"*). El primer arreglo fue anclar la superficie
  con margen 0 **mientras** `m_hidden` y devolverle el margen al mostrarse. Dos bugs de una:
  - `onRevealedChanged` llama a `setHidden(false)` al **principio** del deslizamiento, así que el
    margen vuelve a mitad de la animación y el dock **salta** esos píxeles. El comentario que
    decía "el margen solo se mueve cuando el contenido ya está afuera" solo valía para el camino
    de ocultar.
  - Peor: el puntero tirado contra el borde queda en la última fila de la pantalla, que es donde
    está la franja. Al revelarse, la superficie se corre `margen` px hacia adentro y **lo deja
    afuera** → se pierde el hover → se esconde → el margen vuelve a 0 → el puntero está otra vez
    sobre la franja → se revela. **Rebote infinito**, y solo en los docks no compactos (con
    margen 0 no pasa, que es la comprobación de dos segundos: poné *Compacto* o el margen en 0 y
    fijate si el rebote se va).
  La regla general: **la geometría de una superficie de layer-shell no puede depender de un
  estado que cambia con la interacción**. El arreglo es que el margen no sea del ancla sino de la
  propia superficie — `DockConfig::edgeInset()` la hace `margen` px más gruesa y `Dock.qml` dibuja
  el dock corrido esos píxeles (banda transparente contra el borde) —, así el ancla es fijo y
  `setHidden()` solo toca la máscara. Ver *Modos de ocultamiento* en `AGENTS.md`.
- **Y esto sí se prueba bajo Xvfb, de punta a punta**, aunque no haya layer-shell: la ventana
  toma su tamaño del objeto raíz, así que `xwininfo` mide la banda (68 px en modo siempre visible,
  68 + margen en auto-ocultar), una captura dice de qué lado quedó, y la máscara de 3 px es una
  región de XShape que XTEST respeta — o sea que `xdotool mousemove <x> <alto-2>` **descubre el
  dock de verdad**. Contar píxeles del `panelColor` en la captura es la aserción (0 = escondido).
  El control positivo de la demora son dos corridas, `hideDelayMs=1500` contra `=0`, mirando la
  captura medio segundo después de sacar el puntero: sin el control, el caso pasa por casualidad.
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
- **Un filtro cuya entrada vive en el `DockConfig` de OTRO dock se prueba entero en el tier
  `unit`, sin ventanas ni compositor** (2026-08-14, *No ver apps ancladas en el monitor*). Dos
  docks del mismo monitor son dos `DockConfig` cuyos dockId comparten el nombre de pantalla
  (`VIRT-x` y `DockConfig::makeDockId("VIRT-x", 1)`, o sea `VIRT-x#1`), y con monitores
  inventados ninguno arma ventana: se construyen los dos en la pila, se edita uno y se afirma
  sobre el `DockModel` del otro. Ver `tests/unit/tst_appswidget.cpp`.
  **Y el control positivo es lo que separa "el test pasa" de "el test prueba la feature"**: acá
  el `QCOMPARE` podía dar verde sin que la señal cruzara de un config al otro. Anular a mano el
  `emit` del relay, recompilar y confirmar que el caso **falla** cuesta un minuto — y si no
  falla, el test estaba mirando otra cosa.
- **Una señal que hay que hacerle llegar a otro `DockConfig` se RELAYA, no se conecta cruzada.**
  El idiom del proyecto es que cada modelo escucha únicamente su propia config: quien escribe
  recorre `s_instances`, filtra por lo que corresponda (acá `screenOfDockId()`) y **emite la
  señal en el objeto ajeno** (`notifyMonitorAppsChanged()`, igual que `writeMenuConfigValue()` y
  `notifyDarkModeChanged()`). Conectar un `DockModel` a la config de otro dock parece más corto
  y te deja con conexiones colgando cuando ese dock se destruye.
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
- **Una sección que se repite (`spring`, `sep`, `gap<n>`) NO va en `knownWidgetTokens()`**: esa
  lista se deduplica. Va en `DockConfig::isRepeatableToken()`, que es de donde
  `reconcileWidgetOrder()` saca el permiso para dejar pasar un token más de una vez. Si te
  olvidás, el token **desaparece del `widgetOrder` en el próximo load** sin imprimir nada.
  Y si además le vas a dar un ajuste **por instancia** (el ancho del separador transparente,
  2026-08-14), el token tiene que estar **numerado** como los `appsel<n>`: dos `gap` pelados en
  `widgetOrder` son indistinguibles y no hay por dónde keyear el ajuste. Eso arrastra una
  migración en `load()` **antes** de `reconcileWidgetOrder()` (idempotente: un preset trae los
  pelados de vuelta), el `case "<token>"` de `Dock.qml` reemplazado por un helper de prefijo en
  las tres funciones (`sectionVisible`, `componentFor`, `sectionTooltip`), y una entrada en el
  dict `numbered` de `tests/static/check-widget-tokens.py`, que si no falla pidiendo el `case`
  del token pelado — que ya no existe.
  Y en `Dock.qml`, además del `Component` + `componentFor()` + `sectionVisible()`, hay que
  excluirla del `Binding` de `hovered` del delegate: un componente de sección que no declara
  esa property escupe *"Property 'hovered' does not exist on QQuickItem"* en cada arranque
  (lo agarró el arnés de Xvfb al primer intento, 2026-08-02).
  **Y si una condición que ya existía deja de valer para un caso, buscá todo lo que colgaba de
  ella**: al hacerse configurable el ancho del `gap`, `sec.isSpring` pasó a ser falso para los
  fijos, y con eso se les venían encima el nombre de sección dibujado sobre el agujero, el
  warning del `hovered` y la desaparición del ítem *Quitar* de su menú. Ninguna de las tres da
  error de compilación ni la ve `qmllint`.
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
  (`"0 = auto (panel stretches…"`, `src/settingsdialog.cpp`). Escribí `vacío: la fecha y la
  hora` y listo. Hay un test que lo prohíbe (`tests/static/check-tr-separator.py`), pero
  **tuvo dos agujeros que lo dejaron sin cubrir C++ entero**, los dos tapados el 2026-08-17:
  - **Su patrón era `\bq?sTr`**, que matchea `qsTr` y `sTr` y **nunca `tr`**. O sea que durante
    toda su vida revisó el QML y ni un solo `tr()` de C++. Al corregirlo aparecieron dos
    cadenas rotas que ya estaban en el árbol, y una de ellas tenía su traducción al español
    partida al medio *dentro del `.md`*: `Name for "%1" (empty = Nombre para «%1» (vacío` —
    clave y valor truncados los dos, así que en español se veía el texto en inglés.
  - **C++ concatena literales adyacentes y el chequeo iba línea por línea.** Un `tr()` partido
    en dos líneas (hay ~147 en el árbol) escondía el `" = "` en la segunda, que el patrón ni
    empezaba a matchear. QML **no** concatena, así que la unión se hace solo en `.cpp`/`.h`.

  La moraleja que quedó congelada en el script: **un chequeo que dejó de cubrir lo suyo se ve
  exactamente igual que un árbol limpio**. Por eso ahora corre un `SELFTEST` de once casos
  —los dos agujeros incluidos— antes de mirar el repo, y falla si alguno deja de detectarse.
  Y si necesitás barrer el catálogo a mano: en `capabase.md` **toda entrada es identidad**
  (`X = X`), salvo la cabecera y la sección `## Widgets` (que es `token = etiqueta` a
  propósito), así que una línea donde clave ≠ valor solo puede ser una cadena partida.
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
- **Un popup del dock con el que se pueda INTERACTUAR va con `Qt.ToolTip`, nunca con
  `Qt.Popup`** — y no puede cerrarse por *leave*. Las dos mitades costaron una feature entera
  cada una, y la segunda mordió dos veces (la miniatura de ventana volvió a caer en ella el
  2026-08-18, después de que un preview más viejo ya la hubiera resuelto):
  - **El grab.** `Qt::Popup` hace un *input grab* de Wayland mientras está abierto y **se traga
    el clic derecho del ícono de abajo**: el menú contextual no abre y parece un bug del menú.
    `Qt::ToolTip` no hace grab y su superficie **igual recibe input** para sus `MouseArea`, que
    es lo que hace posibles los botones de `qml/AppPreviewWindow.qml`. Con transient parent es
    además un xdg_popup, o sea que `LayerSurface::attachPopup()` lo posiciona contra el dock y
    `plasma-window-management` no lo reporta como ventana.
  - **El leave.** Ni `HoverHandler` ni `MouseArea.onExited` de la superficie del popup separada
    disparan el *leave* de forma confiable en KWin, así que cualquier `containsMouse`/`hovered`
    que use para decidir si sigue abierto **puede quedar pegado en `true`**.
  - **Pero la cura NO es un watchdog de inactividad**, y equivocarse en esto cuesta la feature
    entera (2026-08-19, reportado por el usuario). Un puntero quieto **no genera un solo evento**,
    o sea que *"N ms sin movimiento"* es exactamente lo mismo que *"el puntero se fue"*: la
    miniatura aparecía y se cerraba sola con el mouse apoyado encima, y como el único sitio que la
    encolaba era el `onEntered` del ícono, había que **salir y volver a entrar** para recuperarla
    ("hay que pasar dos veces por cada ícono"). Y no se arregla subiendo el umbral: mientras el
    reloj sea el que decide, el caso normal —alguien mirando la miniatura— es el que pierde.
    Lo que funciona son tres cosas juntas:
    1. **Medir presencia, no actividad**: una bandera por superficie (el ícono, el popup), puesta
       por `onEntered` y sacada por `onExited`, y el popup abierto mientras alguna esté en `true`.
       El `onExited` del ícono tiene que **ignorar el leave que no venga del ícono en curso**: ir
       de un ícono al de al lado puede entregar el `exited` del viejo *después* del `entered` del
       nuevo.
    2. **Un timer de gracia corto (150 ms) y nada más**, porque **cada** traspaso emite un leave
       antes del enter siguiente — del ícono al popup hay un hueco de 8 px, y del cuerpo del popup
       a un botón hay una `MouseArea` hija que le roba el hover a la madre. Verificado con la
       sonda: `pointerLeft` seguido de `activity` en los tres cruces. Cerrar en el leave deja los
       botones **inalcanzables** aunque todo lo demás esté bien.
    3. **Desmentir la bandera pegada con evidencia, no con un reloj**: cualquier movimiento del
       puntero sobre el dock (`dockHoverPos`) mientras el popup está abierto y **ninguna** bandera
       dice tenerlo es prueba de que una quedó pegada. Se cura solo en cuanto el usuario mueve el
       mouse, que es lo primero que hace ante algo que no se va, y no cierra nada mientras el
       puntero esté donde tiene que estar.
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
- **Nunca esperes a un proceso hijo desde el hilo de la GUI, por más rápido que "siempre"
  conteste.** `AudioControl::refresh()` eran cinco `pactl` con `waitForFinished(800)` cada uno:
  cuatro segundos de dock congelado por refresco, invisibles mientras el servidor de audio
  contestaba en 15 ms. Mordió el 2026-08-16 al enchufar un monitor cuyo sink HDMI entra al
  grafo — ahí `pactl` se pone lento **y** `pactl subscribe` larga una avalancha, así que el
  debounce compraba otro bloqueo apenas terminaba el anterior y el dock parecía colgado hasta
  que el grafo se calmaba. El patrón correcto está en `runAsync()`: `QProcess` en el heap,
  callback en `finished`, y un `QTimer` de watchdog que lo **mata** en vez de esperarlo.
  Dos corolarios que valen para cualquier backend que hable por CLI:
  - **Fusioná los refrescos** (uno en vuelo, uno recordado). Si no, una tormenta de eventos
    lanza N×5 procesos.
  - **Medí el bucle de eventos, no la llamada.** Un `QElapsedTimer` alrededor de `refresh()`
    puede dar 0 ms y el dock estar igual de trabado en otro punto del ciclo; la aserción que
    sirve es un `QTimer` que cuenta latidos (con el bug: 1 de 40).
- **Editar `tests/lib/fakebin.sh` NO regenera las herramientas falsas** si el `.stamp` ya
  existe: el `add_custom_command` necesita `DEPENDS` sobre el script (ya está puesto, no lo
  saques). Sin eso los tests corren contra los fakes de la compilación anterior — y eso es
  peor que un test que falla, porque **pasa**: costó un control positivo entero el 2026-08-16,
  con un `pactl` lento nuevo que nunca se escribió, así que el test midió un pactl instantáneo
  y dio verde contra el mismísimo bug que venía a cazar. Si tocás un fake, confirmalo en el
  archivo generado (`grep <lo-nuevo> build/tests/fakebin/<tool>`) antes de creerle a una corrida.
- **Una sonda sin `XDG_DATA_HOME` aislado le deja al usuario un escritorio SIN DOCKS, y no se
  repara sola.** La receta de sondas de `DockManager`/`SettingsDialog` siembra
  `enabledScreens=NOEXISTE-0` y monitores inventados (`VIRT-1`); si la corrida no aisló el home,
  eso queda escrito en el `kdock.conf` **real**. Al siguiente arranque `wantedDocks()` no
  encuentra ningún monitor que coincida, se crean **cero** ventanas, y como
  `setQuitOnLastWindowClosed(false)` el proceso se queda vivo e invisible: desde afuera se ve
  igual que *"compila pero no ejecuta"*. Y no se recupera nunca, porque `migrateFirstRun()`
  corta con `if (!DockConfig::enabledDocks().isEmpty()) return` — la lista no está vacía, está
  llena de basura. Mordió el 2026-08-16 y se llevó los 12 docks del usuario.
  El diagnóstico es una línea, y conviene hacerlo **antes** de mirar el código que se acaba de
  tocar:

  ```bash
  grep -nE "^(enabledScreens|knownScreens|knownDocks)=" ~/.local/share/kdock/kdock.conf
  ```

  La cura es restaurar esas tres claves desde el `backup-<timestamp>/kdock.conf` del último
  install (el resto del archivo no se toca: el `snapshot` de wallpapers es estado de runtime
  legítimo y revertirlo le cambia el fondo). Y borrar los `kdock-NOEXISTE-0.conf` /
  `kdock-VIRT-1-0.conf` que el propio kdock creó al leer esa lista.
  **La suite no tiene este problema** —`tests/unit/sandbox.h` pone el home descartable antes de
  `QApplication`, verificado por md5 del archivo real—; el riesgo es de las sondas a mano.
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

- **Un `HoverHandler` sobre la superficie del dock se puede quedar pegado en `hovered=true`
  para siempre, y con eso muere el auto-hide entero** (2026-08-18, reportado por el usuario: el
  dock de `eDP-1` en modo Dodge quedó fijo a la vista sobre una ventana maximizada durante ~12h;
  un reinicio del proceso lo arregló al instante). `dockHover` (en `qml/Dock.qml`) es un
  `HoverHandler` sobre `root`, y `root.revealWanted` lo usa directo (`dockHover.hovered`) sin
  ningún mecanismo de corrección. Diagnóstico en vivo con `KDOCK_DEBUG_DODGE=1`: al reiniciar,
  `DockWindow::updateWindowsOverlap()` calculó `overlap=1` correctamente de una — la lógica de
  C++ no tenía nada malo. La sospecha (no probada contra el estado podrido original, que ya no
  existía) es la misma familia de bug que ya había mordido una vez a un popup de ícono más viejo
  (ver la entrada de `MouseArea.onExited` en la sección de *Popups y menús*, y el bullet
  `previewIconHovered` en `AGENTS.md` → *Vista previa de ventana*): el commit anterior al
  bug agregó, por primera vez, un xdg_popup (la miniatura de ventana al hoverear un ícono) que
  se crea y destruye pegado a la superficie del dock — justo la clase de evento que KWin puede
  no reportarle con un *leave* limpio a la superficie de abajo.
  **No hay forma de consultar la posición real del cursor bajo Wayland sin tener ya el foco de
  puntero** (que es justo lo que está en duda), así que no hay un chequeo "contra el terreno"
  posible — la misma limitación que ya había forzado al fix del popup viejo a usar inactividad
  en vez de una consulta de posición. El fix acá es un watchdog de inactividad de 4 s, apagado
  mientras hay menú/arrastre/preview, porque el costo de un falso positivo es mayor: esconder el
  dock entero en vez de una miniatura chica.
  **Y acá sí corresponde inactividad, a diferencia del preview** (donde el mismo patrón fue el bug
  del 2026-08-19, ver *Popups y menús*): lo que se mide es una superficie **grande**, sobre la que
  quedarse 4 s sin mover un pixel es raro, y el término que se corrige es un `hovered` del que ya
  se sospecha. Sobre un ícono chico que el usuario está mirando, en cambio, quedarse quieto es
  justo lo que hace. La regla: un reloj puede desmentir un hover que estorba, nunca sostener uno
  que hace falta. Ver el bullet
  de `dockHoverStale` en `AGENTS.md` → *Modos de ocultamiento* para el detalle completo y el
  trade-off aceptado (un mouse **completamente** quieto sobre el dock más de 4 s, sin menú ni
  arrastre ni preview, ahora lo esconde solo — antes eso no pasaba).
  **Si esto vuelve a pasar**, el punto de partida es distinto al de la primera vez: dejar
  `KDOCK_DEBUG_DODGE=1` puesto de antemano (no se puede agregar después sin reiniciar, que es
  justo lo que borra el estado podrido) y revisar si `dockHoverStale` en algún momento llegó a
  `true` sin que el dock volviera a aparecer — eso descartaría a `dockHover` como la causa y
  apuntaría a otro lado (p. ej. `windowsOverlap` en sí, o `revealed`/la animación).
