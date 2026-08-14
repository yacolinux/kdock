#!/usr/bin/env python3
"""Cada token de widget está enchufado en las tres puntas que se olvidan calladas.

Agregar un widget se toca en siete archivos y **el que se olvida no da error**:
sin su línea en knownWidgetTokens() el widget no aparece nunca aunque su flag
esté en true y el Component exista; sin su rama en componentFor() el Loader queda
vacío; sin rama en sectionVisible() no se muestra jamás; sin defaultWidgetLabel()
el nombre sale como el token pelado.

Se cruza la lista canónica (knownWidgetTokens) contra esas tres. El casillero del
diálogo queda afuera a propósito: se conecta por setter (`setShowVolume`), no por
token, así que no hay forma honesta de cruzarlo con un grep.

Uso: check-widget-tokens.py <repo>
"""
import re
import sys
from pathlib import Path

repo = Path(sys.argv[1] if len(sys.argv) > 1 else ".")
cfg_cpp = (repo / "src" / "dockconfig.cpp").read_text(encoding="utf-8")
cfg_h = (repo / "src" / "dockconfig.h").read_text(encoding="utf-8")
dock_qml = (repo / "qml" / "Dock.qml").read_text(encoding="utf-8")


def body_of(text, signature):
    """El cuerpo de una función, desde su firma hasta la llave que la cierra."""
    start = text.index(signature)
    depth, i = 0, text.index("{", start)
    for j in range(i, len(text)):
        if text[j] == "{":
            depth += 1
        elif text[j] == "}":
            depth -= 1
            if depth == 0:
                return text[i:j]
    raise SystemExit(f"no pude encontrar el final de {signature}")


tokens = re.findall(
    r'QStringLiteral\("([^"]+)"\)',
    body_of(cfg_cpp, "QStringList DockConfig::knownWidgetTokens()"),
)
labels = set(
    re.findall(
        r'\{QStringLiteral\("([^"]+)"\)',
        body_of(cfg_cpp, "DockConfig::defaultWidgetLabel"),
    )
)
# Los tokens repetibles (spring/sep/gap) NO van en knownWidgetTokens —esa lista
# se deduplica— pero sí tienen que tener Component en Dock.qml. isRepeatableToken
# es inline, así que sale del header.
repeatables = re.findall(
    r'QLatin1String\("([^"]+)"\)',
    body_of(cfg_h, "static bool isRepeatableToken"),
)

visible_body = body_of(dock_qml, "function sectionVisible(token)")
component_body = body_of(dock_qml, "function componentFor(token)")

problems = []
for token in tokens:
    if token not in labels:
        problems.append(f"{token}: falta en defaultWidgetLabel()")
    if f'case "{token}"' not in visible_body:
        problems.append(f"{token}: falta su rama en sectionVisible() (nunca se muestra)")
    if f'case "{token}"' not in component_body:
        problems.append(f"{token}: falta su rama en componentFor() (el Loader queda vacío)")

# Un repetible puede estar *numerado*: sus instancias se llaman gap1, gap2… y
# Dock.qml no las despacha con un `case` (el token pelado nunca aparece en
# widgetOrder, la migración lo convierte al cargar) sino con un helper de
# prefijo. Para esos, lo que hay que exigir es el helper, en las dos funciones —
# y la etiqueta del token pelado, que es la clave del catálogo por la que se
# traducen todas las instancias.
numbered = {"gap": "isGapToken("}

for token in repeatables:
    helper = numbered.get(token)
    if helper:
        if token not in labels:
            problems.append(f"{token}: falta en defaultWidgetLabel() (es la clave "
                            "del catálogo de todas sus instancias)")
        if helper not in visible_body:
            problems.append(f"{token} (numerado): falta {helper} en sectionVisible()")
        if helper not in component_body:
            problems.append(f"{token} (numerado): falta {helper} en componentFor()")
    elif f'case "{token}"' not in component_body:
        problems.append(f"{token} (repetible): falta su rama en componentFor()")
    if token in tokens:
        problems.append(f"{token}: es repetible y además está en knownWidgetTokens() "
                        "(esa lista se deduplica, el token desaparecería)")

if problems:
    print("Tokens de widget mal enchufados:")
    print("\n".join("  " + p for p in problems))
    sys.exit(1)

print(f"ok: {len(tokens)} tokens conocidos + {len(repeatables)} repetibles, "
      "todos con etiqueta y sus dos ramas en Dock.qml")
