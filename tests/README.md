# Suite de tests de kdock

```bash
tests/run.sh                 # lo que corre en CI: static + unit + qml (~1 min)
tests/run.sh --tier unit     # un tier suelto: static | unit | qml | live
tests/run.sh --tier all      # incluye live (necesita tu sesión Wayland)
tests/run.sh -R dockconfig   # filtro por nombre, se pasa tal cual a ctest
```

`run.sh` configura con `-DKDOCK_BUILD_TESTS=ON`, compila y corre `ctest`. Todo está
registrado en ctest con un **label**, así que el runner local y el job de GitHub Actions
(`.github/workflows/ci.yml`) son el mismo comando con otro filtro.

Las recetas manuales de las que salió cada tier están en `CLAUDE.md`; acá está el **por qué**
de cada uno y cómo agregarle un caso.

## Los cuatro tiers

| tier | qué prueba | dónde corre |
|---|---|---|
| `static` | invariantes del repo: qmllint, `.qml` en el qrc, catálogo al día, `tr()` sin `" = "`, tokens de widget completos, tarjetas del panel sin `theme.foreground` | en cualquier lado, sin compilar |
| `unit` | Qt Test contra `kdock_core`, headless (`offscreen`) | en cualquier lado |
| `qml` | los binarios de verdad bajo Xvfb: el QML carga **y dibuja** | necesita Xvfb |
| `live` | dodge y multi-monitor contra KWin | **solo** en la sesión Wayland; nunca en CI |

Un test que no puede correr **se saltea** (código 77), no falla: sin `xvfb-run`, sin
`lupdate`, con un solo monitor. Ojo con esto al leer una corrida: `Skipped` no es verde.

## Reglas del sandbox (no son opcionales)

Todo lo que arranca un binario pasa por `lib/sandbox.sh`, que da un `XDG_DATA_HOME` /
`XDG_CONFIG_HOME` descartable **por corrida** y pone `lib/fakebin.sh` al frente del `PATH`.

- **Sin el `PATH` falso, un test le cambia el brillo y el tema del escritorio a quien lo
  corra**: `BrightnessControl` reaplica el brillo guardado al arrancar y `AppearanceControl`
  lanza `plasma-apply-colorscheme` (que además avisa por D-Bus, así que aislar
  `XDG_CONFIG_HOME` no alcanza).
- **El sandbox es por corrida y no por suite** porque los docks que dejó la anterior corren
  los slots, y porque `Translations::seedFiles()` solo copia lo que falta: un directorio
  reusado esconde las traducciones nuevas.
- Los `.desktop` del usuario se enlazan (`kdock_sandbox_link_apps`) o se usan los de
  `fixtures/`. Con un `XDG_DATA_HOME` vacío los menús salen **vacíos y parece un bug**.
- La limpieza se registra con `kdock_on_exit`, nunca con `trap ... EXIT` directo: el trap del
  helper y el del script se pisan, y lo que queda vivo es un proceso de prueba y un `.desktop`
  temporal que le sigue concediendo privilegios de KWin a esa ruta.

## Qué cubre cada test unitario

`tst_dockgeometry` (el rectángulo del dock), `tst_dockconfig` (migraciones, orden de
secciones, grosor), `tst_dockmanager` (mover/copiar dock, bandeja), `tst_desktopentry` (los
`app_id` deformados de Chromium/Edge), `tst_translations` (el merge del catálogo),
`tst_settingsdialog` (**ninguna conexión de una solapa sobrevive al cambio de dock**) y
`tst_qmlload` (**el QML del qrc compila con el Qt con el que se linkeó**).

`tst_settingsdialog` congela un crash real: `buildTabs()` borra las solapas y las rehace en
cada cambio de dock, pero el `DockConfig` es el del dock vivo y sobrevive, así que una
conexión con `this` de contexto quedaba llamando `setEnabled()` sobre botones liberados —
anclar una app desde el dock cerraba kdock (ver *Trampas que muerden* en `CLAUDE-TRAMPS.md`). Es un
test de widgets y corre igual headless, porque `sandbox.h` ya construye una `QApplication`.

`tst_systray` es el único que necesita un **bus de sesión propio**: `SystrayHost` toma
`org.kde.StatusNotifierWatcher`, así que en el bus de verdad le pelearía la bandeja al kdock
del usuario. Por eso se registra a mano en `tests/CMakeLists.txt`, envuelto en
`dbus-run-session`, y se saltea si esa herramienta no está. Congela el deadlock del 2026-08-21:
un cliente de bandeja que exporta su ícono y después duerme sin bucle de eventos, y la pregunta
de si el bucle del host sigue latiendo mientras tanto. El control positivo —una sola lectura de
propiedad bloqueante— lo hace fallar con `registerItem() tardó 2455 ms`.

`tst_qmlload` es el que más rinde por línea y conviene entender por qué existe: `qmllint` mira
los archivos del árbol, no lo que quedó *dentro* del binario, y el smoke necesita Xvfb y
medio minuto. `tst_qmlload` instancia cada `.qml` del recurso con un `QQmlComponent` en
milisegundos, así que cubre el archivo que nadie agregó al `qt_add_resources` y —lo que lo
justificó— **los `import` que no existen en esa instalación de Qt**. Con eso se encontró que
`Qt5Compat.GraphicalEffects` era una dependencia de runtime sin declarar: falta el módulo,
el QML no carga, el dock arranca con la ventana vacía y **no imprime una sola línea**.

## Agregar un caso

- **unit**: un `.cpp` en `unit/` que use `KDOCK_TEST_MAIN` (de `unit/sandbox.h`, que instala el
  sandbox **antes** de `QApplication`, porque `XDG_DATA_HOME` tiene que estar puesto antes de
  la primera llamada a `QStandardPaths`), más su línea `kdock_add_unit_test(...)` en
  `CMakeLists.txt`. Linkea `kdock_core`, o sea los mismos objetos del binario.
- **qml**: una fila más en el array `CASES` de `qml/smoke.sh` (`nombre|claves del .conf|ancho
  esperado|alto esperado`).
- **static**: un script en `static/` que salga 0/1/77 y su `kdock_add_static_test(...)`.

## Dos cosas que hacen que el tier `qml` no mienta

Valen para cualquier arnés gráfico de este proyecto:

1. **Silencio no es prueba.** Una corrida sale limpia también cuando el QML **nunca se
   instanció**. Por eso `smoke.sh` no mira solo stderr: comprueba con `xwininfo` que la ventana
   existe y que su tamaño es el que la configuración implica (una raíz rota sale de 160x160).
2. **Al proceso se lo mata ANTES de bajar el Xvfb** (`lib/xvfb-app.sh`). Si se cae el servidor
   X primero, Qt desenrolla con los bindings vivos y escupe cientos de `TypeError … of null` de
   teardown, indistinguibles de un bug de verdad.

Los dos se comprobaron rompiendo un `.qml` a propósito: `qmllint` **pasa** y `qml.smoke` falla.
Ese es exactamente el reparto de trabajo entre los dos.

## Las dos costuras de test en producción

Son dos, chicas y documentadas en el código:

- **`KDOCK_TEST_SCREENS`** (`src/dockmanager.cpp`): la lista de monitores. Es lo único que ata
  `DockManager` a la plataforma y bajo Xvfb no hay forma de simular un segundo monitor
  (`xrandr --setmonitor` no hace nada acá: este Xvfb no implementa monitores de RandR 1.5).
- **`KDOCK_DEBUG_DODGE`** (`src/dockwindow.cpp`): imprime cada transición de `windowsOverlap`.
  El estado del *intelligent hide* no está en D-Bus y una captura no alcanza para afirmarlo.

Las dos están apagadas salvo que se las pida explícitamente.

## Trampas de los tests que ya mordieron

- **Una sonda con el `Shared` vacío se cae si el dock que crea cae en un monitor conectado**:
  `sync()` arma un `DockWindow` de verdad con todos los servicios en `nullptr`. Se evita atando
  los docks a un escritorio virtual que no es el actual, o poniéndolos en un monitor que no
  esté en `KDOCK_TEST_SCREENS`.
- **`QSettings` escribe diferido**: una aserción sobre archivos falla si el `DockConfig` que los
  escribió sigue vivo. Por eso `tst_dockmanager` siembra con un `DockConfig` local y de vida
  corta.
- **Las rutas relativas rompen el sandbox**: los symlinks se resuelven desde otro directorio, así
  que los scripts normalizan `repo` con `cd … && pwd`.
- **La ruta del repo tiene un `#`**: no se puede pasar como `-D` con string literal (rompe la
  compilación) ni en un `Exec=` de un `.desktop` (KConfig no lo parsea).
- **Un chequeo estático que dejó de cubrir lo suyo se ve igual que un árbol limpio.**
  `check-tr-separator.py` prohíbe `" = "` dentro de una cadena traducible, y durante toda su
  vida no revisó **ni un solo `tr()` de C++**: su patrón era `\bq?sTr`, que matchea `qsTr` y
  `sTr` pero no `tr`. Encima iba línea por línea, y C++ concatena literales adyacentes, así que
  un `tr()` partido en dos escondía el separador en la segunda línea. Salía verde en las dos
  corridas. Al taparlo (2026-08-17) aparecieron dos cadenas rotas que ya estaban en el árbol.
  Por eso ese script ahora trae un **`SELFTEST`** que corre antes del repo y falla si alguno de
  sus once casos deja de detectarse: un check nuevo que no puede fallar por sí mismo no prueba
  nada. El control es sacarle el arreglo y ver que el autotest lo cante.
