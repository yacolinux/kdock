# Portapapeles con ext-data-control, Wi-Fi con contraseña y solapa Redes

## Context

kdock arrastra dos limitaciones documentadas (README.md:296-303, AGENTS.md:325) y le falta una
pieza:

1. **Portapapeles**: `ClipboardHistory` captura vía `QClipboard::dataChanged`, y en Wayland una
   app solo lee la selección mientras tiene el foco del teclado. La superficie del dock es
   inerte al teclado (`kdock.keyboardInteractivity = 0`), así que la captura **en segundo plano
   no funciona**: el historial solo se llena mientras el popup está abierto (que sí toma el foco
   exclusivo). El arreglo correcto es el protocolo data-control.
2. **Wi-Fi**: `NetworkControl` solo activa conexiones **ya guardadas**; unirse a una red nueva
   escribiendo la contraseña no está.
3. **Falta un editor de redes**: crear/editar/borrar conexiones Wi-Fi y Ethernet (DHCP, IP
   estática, DNS, rutas, por dispositivo), tomando como referencia el configurador de KDE
   (plasma-nm). Se accede con **clic derecho en el widget de red**, igual que el mixer de audio.

### Hallazgos que cambian el diseño (verificados en esta máquina)

- **El protocolo es `ext-data-control-v1`, no `wlr-data-control`.** `wayland-info` muestra
  `ext_data_control_manager_v1 v1` y **ningún** `zwlr_data_control`: el KWin de `/opt/kde`
  renombró su implementación (`kwin/src/wayland/datacontroldevicemanager_v1.cpp` implementa
  `QtWaylandServer::ext_data_control_manager_v1`). El propio KSystemClipboard de KDE ya migró
  (`kguiaddons/src/systemclipboard/waylandclipboard.cpp` incluye `qwayland-ext-data-control-v1.h`).
- **No hace falta privilegio de KWin.** El `interfacesBlackList` de `kwin/src/wayland_server.cpp`
  tiene solo `org_kde_plasma_window_management`, `org_kde_kwin_fake_input`,
  `zkde_screencast_unstable_v1`, `org_kde_plasma_activation_feedback` y
  `kde_lockscreen_overlay_v1`. Data-control no está: cualquier cliente lo bindea, **sin tocar
  `X-KDE-Wayland-Interfaces` ni ksycoca**.
- **Unirse a una red nueva NO necesita un agente de secretos.** `AddAndActivateConnection` acepta
  la `psk` inline; con `psk-flags=0` NM guarda el secreto él mismo (es lo que hace `nmcli device
  wifi connect`). `nmcli general permissions` da `settings.modify.system: sí` y
  `wifi.scan: sí`, o sea que crear/editar/borrar no dispara polkit. NM es 1.54.3.
- **Los secretos guardados se pueden leer**: `Settings.Connection.GetSecrets("802-11-wireless-security")`
  devuelve la `psk` sin prompt (las conexiones existentes son system-owned), así que el editor
  puede mostrar la contraseña guardada.
- **La barra de solapas del diálogo ya está al límite**: once títulos piden 1086 px y por eso el
  menú de mosaicos terminó siendo un grupo dentro de la solapa *Menu*
  (`settingsdialog.cpp:1215-1219`). La solapa Redes necesita resolver eso primero.

### Decisiones tomadas con el usuario

| Tema | Decisión |
|---|---|
| Navegación del diálogo | **Columna lateral izquierda** (`QTabWidget::West`) reusando `ColoredTabBar` |
| Popup del widget Wi-Fi | Lista **redes cercanas** con señal/candado y **contraseña inline** |
| Seguridad Wi-Fi | WPA/WPA2/WPA3-Personal (psk/sae), abierta y WEP. **Sin** 802.1x/EAP |
| Historial del portapapeles | **Texto + imágenes** (miniaturas en el popup) |

---

## Parte A — Solapas en columna lateral (habilita la solapa Redes)

Es el prerrequisito: con la barra horizontal, una solapa más cae en modo flechas de scroll sin
avisar.

**`src/coloredtabbar.{h,cpp}`** — la clase ya se pinta sola (`paintEvent` dibuja el rect
redondeado y llama `CE_TabBarTabLabel`), así que el cambio es acotado:

- `tabSizeHint()`: cuando `shape()` es `RoundedWest`/`RoundedEast`, devolver
  `QSize(maxLabelWidth + 24, fontMetrics().height() + 14)` — ancho **uniforme** (máximo sobre
  todos los títulos) para que la columna quede pareja, y ~30 px de alto por solapa.
- `paintEvent()`: en modo vertical dibujar la etiqueta con `p.drawText(..., Qt::AlignVCenter |
  Qt::AlignLeft, tabText(i))` en vez de `CE_TabBarTabLabel`, que **rota el texto 90°** (si se
  deja rotado el largo vuelve a ser el de hoy, 1086 px, ahora en vertical, y desborda igual).
  La franja de acento del seleccionado pasa del borde superior al **borde izquierdo**.
  La rama sin tinte (`tint` inválido) también dibuja el texto a mano, para que no se cuele
  ninguna etiqueta rotada.
- `setElideMode(Qt::ElideNone)` y dejar que QTabBar ponga sus flechas de scroll si algún día se
  pasan de ~28 solapas (es el fallback gratis que pidió el usuario).

**`src/settingsdialog.cpp`** — `m_tabWidget->setTabPosition(QTabWidget::West)` en el ctor, y
actualizar el comentario de `resize()` (settingsdialog.cpp:136-149): el ancho ya no lo dicta la
barra de solapas. Se mantiene 1120 px, que ahora reparte ~160 px a la columna y el resto al
editor de redes, que es la solapa que más ancho pide.

Nada más cambia: `addTab`, `count()`, `setCurrentIndex`, `applyTabColors()` y `showAudioTab()`
siguen siendo la misma API de `QTabWidget`.

---

## Parte B — Portapapeles: `ext-data-control-v1` + imágenes

### B.1 Cliente del protocolo

- **`protocols/ext-data-control-v1.xml`** — copia de
  `/usr/share/wayland-protocols/staging/ext-data-control/ext-data-control-v1.xml`.
- **`CMakeLists.txt`** — agregarlo al `qt_generate_wayland_protocol_client_sources(kdock FILES …)`
  que ya existe (línea 63), más los `.cpp/.h` nuevos en `qt_add_executable`.
- **`src/waylandclipboard.{h,cpp}`** (nuevo) — patrón idéntico a
  `src/plasmawindowmonitor.{h,cpp}`, que ya usa `QWaylandClientExtensionTemplate` + las clases
  `QtWayland::…` generadas:

  ```
  DataControlManager : QWaylandClientExtensionTemplate<…>, QtWayland::ext_data_control_manager_v1
  DataControlDevice  : QObject, QtWayland::ext_data_control_device_v1   // data_offer / selection / finished
  DataControlOffer   : QObject, QtWayland::ext_data_control_offer_v1    // junta los mime de offer()
  DataControlSource  : QObject, QtWayland::ext_data_control_source_v1   // send() / cancelled()
  WaylandClipboard   : QObject   // fachada: active(), setText(), setImage(), textCopied(), imageCopied()
  ```

  El `wl_seat` que pide `get_data_device` sale de
  `qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>()->seat()`
  (`/usr/include/…/QtGui/qguiapplication_platform.h:66`), y el `wl_display` del mismo sitio.

  Cinco detalles que hacen o rompen esto:

  1. **Lectura asíncrona, nunca bloqueante.** `pipe2(fds, O_CLOEXEC)` → `offer->receive(mime,
     fds[1])` → `close(fds[1])` → **`wl_display_flush()`** (sin el flush la otra app no ve el
     pedido y el `read` cuelga) → `QSocketNotifier(Read)` acumulando hasta EOF, con un watchdog
     de 5 s que cierra y descarta. Un `read()` bloqueante en el hilo de la GUI congela el dock si
     la app de origen no escribe.
  2. **Escritura asíncrona.** En `send(mime, fd)`: `fcntl(fd, O_NONBLOCK)` + `QSocketNotifier(Write)`
     por chunks. Una imagen de varios MB no entra en el buffer del pipe (64 KB) y un `write()`
     bloqueante colgaría el dock mientras el otro lado lee.
  3. **No leerse a uno mismo.** El compositor manda el evento `selection` **también** al dueño;
     leerlo sería leer del propio proceso (deadlock potencial). Mientras nuestro
     `DataControlSource` esté vivo y sin `cancelled()`, la selección propia se ignora (reemplaza
     al `m_ownText` actual de `clipboardhistory.cpp:57`).
  4. **Respetar el hint de gestores de contraseñas**: si el offer anuncia
     `x-kde-passwordManagerHint`, no se guarda nada (convención de Klipper/KWallet).
  5. **Preferencia de mime**: `text/plain;charset=utf-8` > `text/plain` > `UTF8_STRING` >
     `STRING`; si no hay ninguno y hay `image/png` (o `image/jpeg`, `image/bmp`), se toma la
     imagen.

  Si el global no está (X11, Xvfb, otro compositor), `active()` es `false` y **no se toca nada**:
  `ClipboardHistory` sigue con `QClipboard` como hoy.

### B.2 Historial con imágenes

**`src/clipboardhistory.{h,cpp}`** — la entrada deja de ser un `QString`:

```cpp
struct Entry { QString text; QString imageFile; QSize size; };  // uno u otro
```

- **Almacenamiento**: `~/.local/share/kdock/clipboard-history.json` (índice ordenado:
  `{"type":"text","text":…}` / `{"type":"image","file":"img-<sha1>.png","w":…,"h":…}`) más
  `~/.local/share/kdock/clipboard-images/*.png`.
- **`clipboard-history.txt` sigue existiendo** como export derivado, escrito en `save()`, para
  que *Ver historial actual* y *Guardar historial* (`openInEditor`, `saveHistoryDialog`) sigan
  funcionando igual; las imágenes salen como una línea
  `[imagen 1920x1080 — clipboard-images/img-….png]`.
- **Migración**: si no hay JSON y sí `.txt`, se importa el `.txt` como entradas de texto (el
  formato de delimitadores actual, `clipboardhistory.cpp:143`).
- **Dedup por SHA-1** de los bytes de la imagen: repetir un copiado la sube al tope sin duplicar
  el PNG. Tope `kMaxEntries = 50` (ya existe) más `kMaxImages = 20` y un techo de 8 MB por
  imagen (más grande se ignora). Al desalojar una entrada de imagen se borra su PNG, y al cargar
  se barren los PNG huérfanos.
- **Toggle** `clipboard/captureImages` en `QSettings` (default true), igual que `audio/maxVolume`
  de `AudioControl`: `ClipboardHistory` es un singleton compartido, así que **no** va en
  `DockConfig` (que es por dock).
- `entries(query)` agrega a cada mapa `isImage`, `imageUrl` (`file://…`) y `size`; el filtro de
  búsqueda aplica solo a las de texto (con query no vacía las imágenes no se listan).
- `setClipboard(text)` y `setClipboardImage(file)` enrutan por `WaylandClipboard` cuando está
  activo, y por `QClipboard` si no.

**`qml/ClipboardPopup.qml`** — delegate con dos formas: fila de texto como hoy, o miniatura
(`Image`, `fillMode: PreserveAspectFit`, alto 56) + `"Imagen 1920×1080"` a la derecha. La altura
de fila pasa a depender del tipo.

**`qml/Dock.qml`** — al `clipMenu` (línea ~1897) se le agrega un ítem *checkable* "Guardar
imágenes" enganchado al toggle.

---

## Parte C — Wi-Fi: redes cercanas y contraseña

### C.1 Refactor chico del backend

**`src/nmdbus.{h,cpp}`** (nuevo) — los helpers estáticos que hoy viven en `networkcontrol.cpp:27-49`
(`getAll`, `getProp`) pasan acá, más:

- `using NMSettingsMap = QMap<QString, QVariantMap>;` (`a{sa{sv}}`)
- `registerNmTypes()`: `qDBusRegisterMetaType<NMSettingsMap>()` y
  `qDBusRegisterMetaType<QList<QVariantMap>>()` (`aa{sv}`, para `address-data`/`route-data`).
  Leer ya funciona con los `operator>>`; **escribir** no compila/marshala sin registrar (mismo
  idiom que `systray.cpp:292`).
- `objectPaths(QVariant)`, `ssidToString(QByteArray)`, y las constantes de servicio/rutas.

`networkcontrol.cpp` pasa a usarlos (sin cambio de comportamiento).

### C.2 `NetworkControl` (widget del dock)

Agrega, sobre lo que ya tiene:

- `Q_INVOKABLE QVariantList accessPoints()` — del dispositivo Wi-Fi: `Device.Wireless.AccessPoints`
  y un `GetAll` por AP (`Ssid` es `ay`, `Strength` es **byte**, `Flags`/`WpaFlags`/`RsnFlags` dan
  la seguridad, `HwAddress`, `Frequency` → banda 2.4/5 GHz). Dedup por SSID quedándose con el más
  fuerte, marcado `saved` (hay conexión guardada con ese SSID) y `active` (es el
  `ActiveAccessPoint`), ordenado por señal.
- `Q_INVOKABLE void requestScan()` → `Device.Wireless.RequestScan({})`; `scanning` sale de
  comparar `LastScan`.
- `Q_INVOKABLE void connectToAccessPoint(ssid, password, apPath)` — si hay conexión guardada con
  ese SSID: `ActivateConnection`. Si no: `AddAndActivateConnection(settings, device, apPath)` con
  `connection{id,type=802-11-wireless}`, `802-11-wireless{ssid(ay), mode=infrastructure}` y, si la
  red es cerrada, `802-11-wireless-security{key-mgmt=wpa-psk|sae|none, psk|wep-key0, psk-flags=0}`.
- `Q_INVOKABLE void forgetConnection(connPath)` → `Delete()`.
- `Q_INVOKABLE void setApTrackingEnabled(bool)` — el popup lo prende al abrirse. Sin esto cada
  `rescan()` (debounced 500 ms) haría ~40 round-trips D-Bus **bloqueantes** aunque nadie mire.
- `signal connectionError(QString)` — las llamadas van con `QDBusPendingCallWatcher` y el error
  (contraseña mala, sin autorización) se muestra en el popup en vez de perderse.

### C.3 `qml/NetworkPopup.qml`

Dos secciones —*Guardadas* y *Redes cercanas* (botón **Escanear**)— con candado, barras de señal,
`Conectado`/`Guardada`, fila de contraseña inline al elegir una red cerrada nueva y una etiqueta
de error. Menú contextual por fila: *Olvidar esta red*.

**Trampa**: el campo de contraseña necesita teclado, y la superficie del dock es inerte. Hay que
copiar el patrón de `ClipboardPopup.qml:14-58`: `modal: true`, `focus: true`,
`dockWindow.setKeyboardInteractive(true/false)` en `onAboutToShow`/`onClosed` **y el `Timer` de
reintento del foco** (el cambio de interactividad es double-buffered).

### C.4 Acceso desde el widget

- `qml/Dock.qml` (`networkComp`, línea 2007): `netMouse` acepta `Qt.RightButton` y abre un `Menu`
  propio —el widget es un *block*, así que `secMouse` está deshabilitado y hoy el clic derecho no
  hace nada— con *Configurar redes…* (`dockWindow.openNetworkSettings()`), *Escanear* y
  *Wi-Fi encendido/apagado*. Mismo patrón que `clipMenu`.
- `src/dockwindow.{h,cpp}`: `Q_INVOKABLE void openNetworkSettings()` = `openSettings()` +
  `m_dialog->showNetworkTab()`, calcado de `openAudioSettings()` (dockwindow.cpp:338).
- `src/dockmanager.h`: accesor `network()` junto al `audio()` de la línea 140, y `SettingsDialog`
  recibe el `NetworkControl` como el `AudioControl`.

---

## Parte D — Solapa "Redes" (editor tipo plasma-nm)

### D.1 Backend del editor

**`src/networksettings.{h,cpp}`** (nuevo, sin QML, sin GUI):

- `struct NmDevice { QString path, iface, driver, hwAddress; uint type, state; QString activeConnPath; }`
  desde `NM.Devices` + `Device` (`Interface`, `DeviceType`, `State`, `HwAddress`, `ActiveConnection`),
  filtrando ethernet (1) y wifi (2).
- `struct NmConn { QString path, uuid, id, type, iface; bool autoconnect, active; }`.
- `NMSettingsMap settings(path)`, `NMSettingsMap secrets(path, group)`, `addConnection(map)`,
  `updateConnection(path, map)`, `deleteConnection(path)`, `activate(conn, device)`, `deactivate()`.
- **Conversores puros** (lo más testeable, y lo que más se equivoca):
  ```cpp
  struct IpAddress { QString address; int prefix; };
  struct IpRoute   { QString dest; int prefix; QString nextHop; int metric; };
  struct IpConfig  { QString method; QList<IpAddress> addresses; QString gateway;
                     QStringList dns, searchDomains; QList<IpRoute> routes;
                     bool ignoreAutoDns, ignoreAutoRoutes, neverDefault, mayFail; };
  IpConfig ipFromSettings(const QVariantMap &group);
  QVariantMap ipToSettings(const IpConfig &cfg, bool v6);
  ```
  Se escribe la forma moderna `address-data` / `route-data` (`aa{sv}` con `address`/`prefix`,
  `dest`/`prefix`/`next-hop`/`metric`) + `gateway` como string, y se **borran** las claves legacy
  `addresses`/`routes` antes del `Update` para que no queden dos fuentes de verdad.
  **A verificar en la primera corrida** (ver Verificación): el dump real de esta máquina muestra
  que NM 1.54 expone `dns-data` (`as`) junto al `dns` legacy (`au`); si NM ignora `dns-data` al
  escribir, hay que mandar `dns` como uint32 en **orden de red** (`qToBigEndian` sobre
  `QHostAddress::toIPv4Address()`) y `aay` de 16 bytes para IPv6.

### D.2 UI

**`src/networksettingswidget.{h,cpp}`** — el `QWidget` de la solapa: `QSplitter` con

- izquierda: `QTreeWidget` con dos grupos, **Dispositivos** (`wlp0s20f3` Wi-Fi, `enp0s31f6`
  Ethernet, con su estado) y **Conexiones** (activas en negrita, ícono por tipo), más
  `[+ Agregar ▾]` (Wi-Fi / Ethernet), `[− Borrar]`, `[Conectar/Desconectar]`;
- derecha: `QStackedWidget` con la página de **dispositivo** (solo lectura: driver, MAC, estado,
  IP/gateway/DNS actuales desde `Device.Ip4Config`) y la de **conexión**.

**`src/connectioneditor.{h,cpp}`** — la página de conexión: `QTabWidget` **horizontal** interno
(5-6 títulos cortos, sin riesgo de desborde) + `[Aplicar] [Descartar]`:

| Página | Contenido → claves NM |
|---|---|
| General | Nombre (`connection.id`), Conectar automáticamente (+`autoconnect-priority`), Todos los usuarios (`permissions` vacío), Interfaz (`connection.interface-name`, combo de dispositivos compatibles o "cualquiera") |
| Wi-Fi | SSID (`ssid`, `ay`), Modo, BSSID, Banda, MTU |
| Seguridad Wi-Fi | Ninguna / WPA-WPA2 Personal (`wpa-psk`) / WPA3 Personal (`sae`) / WEP (`none`+`wep-key0`+`auth-alg`), contraseña con "mostrar" sembrada de `GetSecrets`, `psk-flags=0` |
| Ethernet | MAC del dispositivo, MAC clonada, MTU |
| IPv4 | Método (Automático DHCP / Automático solo direcciones / Manual / Enlace local / Compartida / Deshabilitado), tabla Dirección/Prefijo con +/−, Puerta de enlace, DNS, Dominios de búsqueda, "Ignorar DNS automático", "Requerir para que la conexión funcione" (`may-fail` invertido), botón **Rutas…** |
| IPv6 | Igual, con Automático/DHCP/Manual/Ignorar/Enlace local |

**`src/routesdialog.{h,cpp}`** — el diálogo de rutas: tabla Destino/Prefijo/Siguiente salto/Métrica
con +/−, "Ignorar rutas obtenidas automáticamente" (`ignore-auto-routes`) y "Usar esta conexión
solo para los recursos de su red" (`never-default`). Es el mismo diálogo para IPv4 e IPv6.

**`src/settingsdialog.{h,cpp}`** — `createNetworkTab()` (que solo instancia
`NetworkSettingsWidget`), `m_networkTabIndex`, `showNetworkTab()` calcado de `showAudioTab()`
(settingsdialog.cpp:1461), y el nuevo parámetro en el ctor. La solapa se agrega después de Audio,
no es por-dock (como Previews y Audio).

---

## Archivos

**Nuevos**: `protocols/ext-data-control-v1.xml`, `src/waylandclipboard.{h,cpp}`,
`src/nmdbus.{h,cpp}`, `src/networksettings.{h,cpp}`, `src/networksettingswidget.{h,cpp}`,
`src/connectioneditor.{h,cpp}`, `src/routesdialog.{h,cpp}`.

**Modificados**: `CMakeLists.txt` (protocolo + fuentes), `src/coloredtabbar.{h,cpp}`,
`src/settingsdialog.{h,cpp}`, `src/clipboardhistory.{h,cpp}`, `src/networkcontrol.{h,cpp}`,
`src/dockwindow.{h,cpp}`, `src/dockmanager.h`, `src/main.cpp`, `qml/ClipboardPopup.qml`,
`qml/NetworkPopup.qml`, `qml/Dock.qml`.

Ojo con las dos listas paralelas del `CMakeLists.txt`: las fuentes van en `qt_add_executable` y
el `.xml` en `qt_generate_wayland_protocol_client_sources`; ningún `.qml` nuevo, así que el
`qt_add_resources` no se toca.

---

## Verificación

**Orden**: A → B → C → D, cada parte verificable sola.

1. **Build y lint** (siempre, antes de instalar nada):
   `env -u CC -u CXX cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build -j`
   y `qmllint qml/*.qml 2>&1 | grep -iE "error|syntax"`.

2. **Arnés de Xvfb** (`CLAUDE.md` → *Verificación rápida*): confirma que el QML nuevo **carga**
   (los popups tocados son los dos que más lógica ganan). Bajo Xvfb no hay data-control ni
   compositor, así que `WaylandClipboard::active()` es false y `NetworkControl` sí habla con el
   NM real (system bus) — es la corrida donde se ve si el fallback de `QClipboard` sigue intacto.
   Recordar la copia `kdock.conf` → `kdock-screen.conf` y **no** encadenar `timeout` a un pipe.

3. **Parte A, sonda de `ColoredTabBar` sola** (receta de `CLAUDE.md`, `moc` + `g++` + captura):
   un `main` que arma un `ColoredTabWidget` en `West` con los **doce** títulos, imprime
   `coloredTabBar()->sizeHint()` (debe dar ~160×370, no ~1150 de ancho) y guarda el PNG. Correrlo
   **dos veces**, con paleta clara y oscura, porque el texto lo dibujamos nosotros y el contraste
   lo decide `labelColor()`.

4. **Parte B, sonda del portapapeles en la sesión real** (única forma: no hay data-control bajo
   Xvfb). Binario aparte que linkea `waylandclipboard.cpp` + los fuentes generados del protocolo e
   imprime cada selección capturada. Klipper está corriendo y sirve de arnés desde la CLI:
   ```bash
   qdbus6 org.kde.klipper /klipper setClipboardContents "kdock test 1"   # -> la sonda debe verlo
   qdbus6 org.kde.klipper /klipper getClipboardContents                  # tras nuestro setText
   ```
   Para imágenes, copiar una captura desde Spectacle/Gwenview y confirmar que aparece el PNG en
   `clipboard-images/` con el tamaño correcto. Además: copiar una contraseña desde KWalletManager
   para comprobar que el hint `x-kde-passwordManagerHint` **no** se guarda.

5. **Parte C, arnés de componente QML suelto** (receta de `CLAUDE.md` → *popups del dock*):
   `QQuickView` con `NetworkPopup.qml` y un `NetworkControl` real como context property, más
   `import -window root` bajo Xvfb. Verifica la lista de APs con datos de verdad. Las cinco
   trampas de esa receta aplican tal cual (alias `hostTheme`/`hostConfig`, captura de la raíz,
   copiar `qml/` a `/tmp` por el `#` de la ruta). La conexión con contraseña se prueba **en la
   sesión real** (el dock del build, no el instalado) contra una red conocida.

6. **Parte D, sonda del diálogo real** (receta de `CLAUDE.md` → *linkeando los `.o` del proyecto*),
   en sus tres modos:
   - **captura**: `findChild<QTabWidget*>()->setCurrentIndex(<Redes>)` + `grab().save()`, en
     paleta clara y oscura;
   - **manejo desde código**: crear una conexión Ethernet de prueba sobre `enp0s31f6` (está
     `unavailable`, sin cable: no le mueve la red al usuario), pasarla a IPv4 manual con dirección,
     gateway, dos DNS y una ruta, `[Aplicar]`, y **releer con `gdbus … GetSettings`** para
     confirmar el round-trip — es acá donde se decide `dns-data` vs `dns` en orden de red. Borrar
     la conexión al terminar.
   - **round-trip de lectura**: abrir la conexión Wi-Fi real `Claro-FEENANDEZ-2.4Ghz`, que tiene
     `ignore-auto-dns: true` y `dns: 1.1.1.1`, y confirmar que el formulario la refleja y que
     guardar sin tocar nada **no cambia** el resultado de `GetSettings`.
   **Relinkear la sonda después de cada `cmake --build`.**

7. **Instalación**: backup con timestamp (binario + `.desktop` + `.conf`) y `sudo cmake --install build`.
   **No hace falta `kbuildsycoca6`**: data-control no es una interfaz privilegiada, así que no se
   toca ksycoca (y se evita el riesgo de los dos KF6). Reiniciar el dock lo decide el usuario.

**Advertencia**: como el `BrightnessControl` del arnés, el editor de redes escribe sobre el
**NetworkManager real** — no hay caja de arena. Toda prueba de escritura va sobre conexiones
propias creadas para eso y se borra al final; nunca sobre la Wi-Fi activa del usuario.

---

## Documentación al terminar

- `AGENTS.md`: reescribir el punto 19c (Network) con AP/scan/join, la sección nueva del editor y
  la solapa; el caveat de Wayland del portapapeles (línea 325) pasa de "no implementado" a la
  descripción de `ext-data-control-v1`; la tabla QML↔C++ (línea 838) suma los `Q_INVOKABLE`
  nuevos de `NetworkControl` y `ClipboardHistory`.
- `README.md:296-303`: se caen las dos limitaciones conocidas.
- `CLAUDE.md`: trampas nuevas — el protocolo es `ext-data-control` y **no** necesita privilegio de
  KWin (a diferencia de plasma-window-management); leer/escribir el pipe **asincrónico** o el dock
  se cuelga; `qDBusRegisterMetaType` obligatorio para escribir `a{sa{sv}}`/`aa{sv}` en NM; y que
  la barra de solapas ahora es vertical, así que la restricción de los 1086 px queda sin efecto.
