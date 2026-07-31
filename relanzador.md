# Plan de Implementación: Relanzador (Nested Dock Widget)

## Visión General

El **Relanzador** es un widget tipo "menú de iconos" que, al hacer clic, despliega una barra de iconos anidada (un mini-dock). Este mini-dock solo aloja **lanzadores** (pinned apps) y opcionalmente **widgets** (volumen, brillo, reloj, autohide, systray). Puede contener **hasta 2 niveles** de anidamiento (dock principal → relanzador → sub-relanzador).

## Arquitectura

### Opción elegida: Popup QML inline

En lugar de crear una ventana Wayland separada (complejo con posicionamiento en Wayland), el relanzador usa un componente `Popup` de QML que se despliega dentro del mismo `DockWindow`. Esto:
- Reusa el layout del dock principal
- Funciona sin problemas en Wayland
- No requiere gestión de ventanas adicionales

### 1. Clases C++ Nuevas

#### `RelanzadorConfig` (src/relanzadorconfig.h/cpp)
Configuración persistente de **un** relanzador. Guardada en `QSettings` bajo `relanzadores/<id>/`.

```cpp
class RelanzadorConfig : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString id READ id CONSTANT)
    Q_PROPERTY(QString title READ title WRITE setTitle NOTIFY titleChanged)
    Q_PROPERTY(QString iconName READ iconName WRITE setIconName NOTIFY iconNameChanged)
    Q_PROPERTY(QStringList pinned READ pinned WRITE setPinned NOTIFY pinnedChanged)
    Q_PROPERTY(bool showVolume READ showVolume WRITE setShowVolume NOTIFY showVolumeChanged)
    Q_PROPERTY(bool showBrightness READ showBrightness WRITE setShowBrightness NOTIFY showBrightnessChanged)
    Q_PROPERTY(bool showClock READ showClock WRITE setShowClock NOTIFY showClockChanged)
    Q_PROPERTY(bool showAutohide READ showAutohide WRITE setShowAutohide NOTIFY showAutohideChanged)
    Q_PROPERTY(bool showSystray READ showSystray WRITE setShowSystray NOTIFY showSystrayChanged)
    Q_PROPERTY(bool hasSubRelanzador READ hasSubRelanzador WRITE setHasSubRelanzador NOTIFY hasSubRelanzadorChanged)
    Q_PROPERTY(QString subRelanzadorId READ subRelanzadorId WRITE setSubRelanzadorId NOTIFY subRelanzadorIdChanged)
    Q_PROPERTY(int nestingLevel READ nestingLevel CONSTANT)
    // ...
};
```

#### `RelanzadorModel` (src/relanzadormodel.h/cpp)
`QAbstractListModel` simplificado que solo contiene lanzadores. Similar a `DockModel` pero:
- Sin `WindowMonitor` (no muestra ventanas activas)
- Sin `active`, `minimized`, `windowCount` roles
- Items: `name`, `iconName`, `desktopId`, `isSubRelanzador`
- Métodos: `launch(row)`, `moveItem(from, to)`, `addItem(desktopId)`, `removeItem(row)`

#### `RelanzadoresManager` (src/relanzadoresmanager.h/cpp)
Gestiona la colección de relanzadores. Expuesto a QML como `relanzadores`.

```cpp
class RelanzadoresManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(QQmlListProperty<RelanzadorConfig> items READ items NOTIFY itemsChanged)
    Q_PROPERTY(int count READ count NOTIFY itemsChanged)
    Q_INVOKABLE RelanzadorConfig* createRelanzador(const QString &title);
    Q_INVOKABLE void removeRelanzador(const QString &id);
    Q_INVOKABLE RelanzadorConfig* get(const QString &id) const;
};
```

#### `RelanzadorPopup` (qml/RelanzadorPopup.qml)
Componente QML que se instancia por cada relanzador. Es un `Popup` de QML con un layout tipo dock interno.

```qml
Popup {
    id: popup
    modal: false
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    padding: 8
    // Usa el mismo layout que el dock pero simplificado
}

### 2. Flujo de Datos

```
DockWindow (main)
  ├── DockModel (apps + windows)
  ├── RelanzadoresManager
  │     ├── RelanzadorConfig #1 (id="relanzador_0", nivel=1)
  │     │     ├── RelanzadorModel (pinned: dolphin, firefox)
  │     │     └── RelanzadorPopup (QML)
  │     └── RelanzadorConfig #2 (id="relanzador_1", nivel=2)
  │           ├── RelanzadorModel (pinned: konsole)
  │           └── RelanzadorPopup (QML, sin opción de sub-relanzador)
  └── Widgets (volume, brightness, clock, autohide, systray)
```

### 3. Persistencia (QSettings)

```ini
[relanzadores]
count=2
ids=relanzador_0,relanzador_1

[relanzador_0]
title=Dev Tools
iconName=applications-development
pinned=org.kde.dolphin,org.kde.firefox
showVolume=false
showBrightness=false
showClock=true
showAutohide=false
showSystray=false
hasSubRelanzador=true
subRelanzadorId=relanzador_1

[relanzador_1]
title=Terminals
iconName=utilities-terminal
pinned=org.kde.konsole
showVolume=false
showBrightness=false
showClock=false
showAutohide=false
showSystray=false
hasSubRelanzador=false
```

### 4. Reglas de Niveles de Anidamiento

| Nivel | Descripción | Puede tener sub-relanzador |
|-------|-------------|---------------------------|
| 0 | Dock principal | ✅ Sí (Relanzador nivel 1) |
| 1 | Relanzador en dock | ✅ Sí (Sub-relanzador nivel 2) |
| 2 | Sub-relanzador | ❌ No (máximo 2 niveles) |

- `RelanzadorConfig::nestingLevel` se calcula al crear: nivel 1 si está en el dock, nivel 2 si es sub-relanzador de otro relanzador.
- El UI no muestra la opción "Add sub-relanzador" si `nestingLevel >= 2`.

### 5. Integración con Dock.qml

El dock principal muestra los relanzadores como widgets adicionales. El orden relativo entre widgets y relanzadores es configurable (igual que el alineamiento de widgets existentes).

```qml
// Dentro de widgetRow en Dock.qml:
Repeater {
    model: relanzadores.count
    RelanzadorWidget {
        relanzador: relanzadores.get(index)
    }
}
```

### 6. Reuso de Código

El popup del relanzador puede reusar:
- `IconProvider` (`image://icon/...`) para iconos
- `DesktopEntryIndex` para lanzar apps
- Layout de `Dock.qml` (Grid + Repeater + delegate)
- Widgets existentes (volumen, brillo, reloj, etc.) como componentes QML

### 7. UI para Configurar Relanzadores

Nueva pestaña en `SettingsDialog`:
- Lista de relanzadores existentes
- Botón "Add relanzador" → diálogo con título e icono
- Botón "Remove relanzador"
- Editor de items (lista de desktop IDs, como los pinned del dock)
- Checkbox para cada widget opcional (volume, brightness, clock, autohide, systray)
- Checkbox "Has sub-relanzador" (solo disponible si nivel < 2)

### 8. Archivos Nuevos

```
src/
  relanzadorconfig.h        — Configuración de un relanzador
  relanzadorconfig.cpp
  relanzadormodel.h         — Modelo de items de un relanzador
  relanzadormodel.cpp
  relanzadoresmanager.h     — Manager de la colección de relanzadores
  relanzadoresmanager.cpp
qml/
  RelanzadorWidget.qml      — Icono del relanzador + popup
  RelanzadorPopup.qml       — Layout interno del popup (dock-like)
```

### 9. Pasos de Implementación

1. ✅ **COMPLETADO** - Crear `RelanzadorConfig` y `RelanzadoresManager` (C++)
2. ✅ **COMPLETADO** - Crear `RelanzadorModel` (C++, QAbstractListModel)
3. ✅ **COMPLETADO** - Crear `RelanzadorPopup.qml` (QML, dock-like popup)
4. ✅ **COMPLETADO** - Integrar relanzadores en `Dock.qml` (widgetRow)
5. ✅ **COMPLETADO** - Añadir configuración en `SettingsDialog` (pestaña "Relanzadores": alta/baja de relanzadores + editor de apps con add/remove/up/down)
6. ✅ **COMPLETADO** - Registrar clases en `main.cpp` y `dockwindow.cpp`
7. ✅ **COMPLETADO** - Implementar límite de 2 niveles de anidamiento
8. ✅ **COMPLETADO** - Probar y ajustar posicionamiento del popup (ver "Fix del popup" abajo)

### 10. Estado de Implementación

**Archivos creados:**
```
src/
  relanzadorconfig.h/cpp        ✅ Configuración de un relanzador
  relanzadormodel.h/cpp         ✅ Modelo de items de un relanzador
  relanzadoresmanager.h/cpp     ✅ Manager de la colección de relanzadores
qml/
  RelanzadorWidget.qml          ✅ Icono del relanzador + popup
  RelanzadorPopup.qml           ✅ Layout interno del popup (dock-like)
```

**Archivos modificados:**
```
src/main.cpp                    ✅ Registro de RelanzadoresManager
src/dockwindow.h/cpp            ✅ Exposición a QML
CMakeLists.txt                  ✅ Inclusión de nuevos archivos
qml/Dock.qml                    ✅ Integración en el dock
```

**Funcionalidades implementadas:**
- ✅ Persistencia de configuración en QSettings
- ✅ Modelo de items con roles (name, iconName, desktopId, isSubRelanzador)
- ✅ Métodos de lanzamiento y reordenamiento
- ✅ Popup QML con layout de dock, alimentado por el modelo real (`RelanzadoresManager::getModel(id)`)
- ✅ Límite de 2 niveles de anidamiento
- ✅ Posicionamiento inteligente del popup según el borde del dock
- ✅ Cierre con Escape y click fuera
- ✅ UI de configuración en `SettingsDialog` (pestaña "Relanzadores")
- ✅ **Fix del popup: los lanzadores ahora se muestran** (ver abajo)

**Pendiente (próximos pasos):**
- 🔲 Widgets opcionales dentro del popup (volumen, brillo, reloj, autohide, systray) — hoy son placeholders `Item{}` vacíos en `RelanzadorPopup.qml`
- 🔲 Apertura de sub-relanzador anidado desde el popup (TODO en el `onClicked` del delegate)
- 🔲 Drag & drop entre relanzadores
- 🔲 Menú contextual (click derecho) sobre el widget del relanzador + selector de icono en la config (hoy el icono queda fijo en `folder`)
- 🔲 Fallback de icono visible para items cuyo `.desktop` no resuelve (hoy quedan invisibles; el modelo crea el item pero sin icono)

### 12. Fix del popup (los lanzadores no se mostraban)

**Síntoma:** el relanzador aparecía como icono en el dock, pero al hacer clic no
mostraba ningún lanzador.

**Causa:** `RelanzadorPopup.qml` era un `Popup` normal (sin `popupType`). En Wayland
la superficie layer-shell del dock es diminuta (`iconSize + padding`); un `Popup`
normal de QtQuick se renderiza dentro del overlay de esa superficie, así que al
abrirse por fuera del dock quedaba **recortado e invisible**.

**Solución:** añadir `popupType: Popup.Window` a la raíz del `Popup`. Esto lo convierte
en un `xdg_popup` real que la integración layer-shell delega a xdg-shell vía
`attachPopup()` (`src/layershell.cpp`), pudiendo así extenderse más allá de la
superficie del dock. Es el mismo patrón que ya usaban los menús contextuales y
tooltips en `Dock.qml`. Cambio de una sola línea, sin tocar C++ ni CMake.

### 11. Decisiones Implementadas

- **Posicionamiento del popup**: Se abre en la dirección opuesta al borde del dock (arriba si el dock está abajo, etc.)
- **Cierre del popup**: Click fuera cierra + Escape cierra + Click en launcher cierra
- **Drag & drop**: Implementado `moveItem()` para reordenar dentro del relanzador
