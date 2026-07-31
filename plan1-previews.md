# plan1-previews.md — Mejoras al panel Previews

Estado actual del panel **Dock Preview** (`previews/`): todo funciona. Dos mejoras pedidas:

1. **Desplazamiento más rápido**.
2. **Auto-escalado de tarjetas**: que el tamaño de los thumbnails baje cuando hay muchas
   ventanas y vuelva al completo cuando hay pocas.

Decisiones confirmadas con el usuario:
- Al encoger, **solo se achican las tarjetas** (el grosor de la tira y la zona exclusiva de
  layer-shell quedan fijos); las tarjetas achicadas quedan centradas en la tira.
- El auto-escalado es **configurable**: checkbox `autoFitCards` (default **on**) + piso mínimo
  `fitMinCardWidth` (default 96 px), en el panel Previews.

---

## Mejora 1 — Scroll más rápido (`previews/qml/PreviewStrip.qml`)

En el `ListView` de la tira se reemplazan los defaults de Qt por:

- `mouseWheelVelocity: 600` — un notch de rueda ≈ 2.5 tarjetas (hoy ~100 px ≈ media tarjeta).
- `flickDeceleration: 800` — más inercia al soltar el arrastre (default 1500 px/s²).
- `maximumFlickVelocity: 6000` — permite flicks más rápidos.

## Mejora 2 — Auto-escalado de tarjetas

### `previews/src/previewconfig.{h,cpp}`
Nuevas opciones persistidas + NOTIFY signals:
- `autoFitCards` (bool, default `true`).
- `fitMinCardWidth` (int, default `96`, rango 48–800).

### `previews/src/previewmodel.{h,cpp}` (análogo al `fitScale` de `Dock.qml`)
- Nueva `Q_PROPERTY(qreal cardScale)` (1.0 → se achica) + signal `cardScaleChanged`.
- `Q_INVOKABLE setAvailableLength(int px)`: QML reporta el largo útil de la lista (ya sin
  padding).
- `recomputeCardScale()`: con `autoFitCards` y `>1` ventana, ajusta `cross` por iteración
  (≤10 pasos, factor `avail/need`, como el fit del dock) hasta que `Σ extents ≤ available`,
  acotado a `[fitMinCardWidth, cardWidthPx]`.
- `mainAxisNeeded(cross)`: replica **exactamente** la geometría de `PreviewCard.qml`
  (incluye pisos de 32/24 px, `aspect` real de cada ventana, título 16 px y `cardSpacing`).
- Recalcula en: `sync()` (filas/geometría), `setAvailableLength`, y cambios de
  `edge`/`stripThickness`/`cardSpacing`/`showTitles`/`autoFitCards`/`fitMinCardWidth`.
  Sin bucle: `available` no depende del tamaño de tarjeta.
- `captureTarget()` usa `cardWidthPx * cardScale` ⇒ capturas más chicas con muchas ventanas
  (menos RAM en la caché).

### `previews/qml/PreviewStrip.qml`
- Delegate envuelto en un `Item` que ocupa el eje cruz completo con la tarjeta **centrada**
  (`anchors.centerIn`); `crossSize: Math.round(config.cardWidthPx * previews.cardScale)`.
  Con scale=1 queda geométricamente idéntico a hoy.
- Reportar `previews.setAvailableLength()` desde `onWidthChanged`/`onHeightChanged`/
  `onHorizontalChanged`/`Component.onCompleted`.

### `previews/src/previewsettingsdialog.{h,cpp}` — grupo "Miniaturas"
- Checkbox "Ajustar el tamaño de las tarjetas al contenido" (default on).
- Spinbox "Tamaño mínimo de tarjeta:" (px), habilitado solo con el checkbox on.
- Ambos con `QSignalBlocker` en `selectScreen()`.

### `AGENTS.md`
Documentar la auto-fit (flags, `cardScale`, wrapper centrado, capturas escaladas) y los props
de scroll.

---

## Archivos tocados
- `previews/src/previewconfig.{h,cpp}`
- `previews/src/previewmodel.{h,cpp}`
- `previews/qml/PreviewStrip.qml`
- `previews/src/previewsettingsdialog.{h,cpp}`
- `AGENTS.md`

## Verificación
- `ninja -C build` (target `kdock-previews`).
- Manual: abrir muchas ventanas → las tarjetas se achican y centran hasta el piso; cerrarlas →
  vuelven al tamaño configurado. Rueda y arrastre más rápidos.
- Smoke test bajo `xcb` para descartar errores de binding (las tiras quedan vacías pero el QML
  se instancia).
