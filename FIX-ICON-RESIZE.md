# Fix: Icon auto-shrink no regresa al tamaño original al cerrar apps

## Síntoma

Cuando hay muchas aplicaciones abiertas, el dock reduce el tamaño de los íconos
(`fitScale < 1`). Al cerrar aplicaciones y liberar espacio, los íconos **no
vuelven a crecer** hasta su tamaño anterior, quedando atascados en el tamaño
reducido.

## Causa raíz

Tres problemas en el mecanismo de auto-shrink en `qml/Dock.qml:103-127`:

### 1. Histeresis demasiado agresiva (growth gate)

```qml
// Antes (línea 118):
else if (fitScale < 1 && need * 1.06 < avail)
```

La condición exigía que el espacio disponible (`avail`) fuese **6 % mayor** que
el necesario (`need`) para siquiera empezar a crecer. Si al cerrar una app
`need` bajaba de 1000 → 950, `950 * 1.06 = 1007 ≥ 1000`, por lo que no crecía.
Había que cerrar múltiples apps para que el `need` acumulado bajase >5.7 %
antes de cualquier reacción.

### 2. Dampening excesivo (growth step)

```qml
// Antes (línea 121):
s = Math.min(1, fitScale * avail / need * 0.97)
```

El factor `0.97` hacía que cada pasada de crecimiento fuese más lenta de lo
necesario, requiriendo más iteraciones para converger.

### 3. `fitPasses` no reseteado en cambios externos

```qml
// Antes (línea 90):
onFitNeededChanged: fitTimer.restart()
```

El handler solo reiniciaba el timer pero **no llamaba a `scheduleRefit()`**, que
es lo que resetea `fitPasses = 0`. Si la secuencia de shrink inicial consumía
~12 pasadas (`fitPasses ≈ 12`), al cerrar una app el guard `fitPasses > 12`
(línea 110) bloqueaba `refit()` por completo, impidiendo cualquier crecimiento.

## Cambios aplicados

Archivo: `qml/Dock.qml`

| Línea | Antes | Después | Efecto |
|-------|-------|---------|--------|
| 90 | `fitTimer.restart()` | `scheduleRefit()` | Resetea `fitPasses` al cerrar apps |
| 118 | `need * 1.06 < avail` | `need * 1.01 < avail` | Histeresis 6 % → 1 % |
| 121 | `* 0.97` | `* 0.995` | Dampening 0.97 → 0.995 |

### Detalle del cambio 1 (línea 90)

```qml
onFitNeededChanged: scheduleRefit()
```

`scheduleRefit()` hace `fitPasses = 0; fitTimer.restart()`. Al resetear el
contador, cualquier cambio posterior en el layout (apps cerradas) desencadena
una secuencia de refit completa desde cero.

### Detalle del cambio 2 (línea 118)

```qml
else if (fitScale < 1 && need * 1.01 < avail)
```

El 1 % de margen es suficiente para evitar oscilación (shrink → grow → shrink)
sin exigir un exceso de espacio libre.

### Detalle del cambio 3 (línea 121)

```qml
s = Math.min(1, fitScale * avail / need * 0.995)
```

Emparejado con el `0.995` del shrink (línea 114), el crecimiento converge en
1-3 pasadas en vez de estancarse.

## Nota sobre compilación

Los cambios son solo en QML (no C++), por lo que no requieren recompilación
del binario — se cargan en tiempo de ejecución. Basta reiniciar kdock.

## Backup

El binario anterior está respaldado en:
`/usr/local/bin/kdock.bak.20260729-175849`
