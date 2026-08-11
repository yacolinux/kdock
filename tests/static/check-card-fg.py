#!/usr/bin/env python3
"""Ninguna tarjeta del panel de control pinta texto con el color del tema.

Desde el color de fuente por tarjeta, el texto de una sección lo decide la
tarjeta que la contiene (`fg`, empujado por CmSectionView) y no `theme`: sobre un
fondo claro la foreground de KDE desaparece. El que se olvida **no da error** —
la tarjeta se dibuja igual, con media leyenda invisible sobre su propio color, y
solo se ve en una captura.

Se chequean las dos puntas del contrato:

  1. `controlmanager/qml/cards/*.qml` no menciona `theme.foreground` (la única
     excepción legítima es el valor por defecto de la property `fg`);
  2. cada `CmButton` / `CmSlider` de esas tarjetas le pasa `fg:`, porque los dos
     traen su propio default y no heredan nada del archivo que los declara.

Uso: check-card-fg.py <repo>
"""
import re
import sys
from pathlib import Path

repo = Path(sys.argv[1] if len(sys.argv) > 1 else ".")
cards = sorted((repo / "controlmanager" / "qml" / "cards").glob("*.qml"))
if not cards:
    print("no encontré controlmanager/qml/cards/*.qml", file=sys.stderr)
    raise SystemExit(1)

DEFAULT = "property color fg: theme.foreground"
errors = []

for path in cards:
    lines = path.read_text(encoding="utf-8").splitlines()

    if not any(DEFAULT in line for line in lines):
        errors.append(f"{path.name}: falta `{DEFAULT}` en la raíz")

    for n, line in enumerate(lines, 1):
        if "theme.foreground" in line and DEFAULT not in line:
            errors.append(f"{path.name}:{n}: usa theme.foreground en vez de card.fg")

    # Un CmButton/CmSlider abre bloque en su propia línea (suelto o como
    # `delegate:` de un Repeater); el `fg:` va adentro, así que alcanza con
    # mirar las líneas hasta que la indentación vuelve.
    for n, line in enumerate(lines):
        m = re.match(r"^(\s*)(?:delegate: )?(CmButton|CmSlider) \{\s*$", line)
        if not m:
            continue
        indent, kind = m.group(1), m.group(2)
        body = []
        for follow in lines[n + 1:]:
            if follow.strip() and not follow.startswith(indent + " "):
                break
            body.append(follow)
        if not any(re.match(r"^\s*fg:", b) for b in body):
            errors.append(f"{path.name}:{n + 1}: {kind} sin `fg: card.fg`")

for e in errors:
    print(e, file=sys.stderr)
raise SystemExit(1 if errors else 0)
