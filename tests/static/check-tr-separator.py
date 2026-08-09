#!/usr/bin/env python3
"""Ninguna cadena traducible puede contener " = ".

El formato de translations/*.md es `clave = texto` y se parte por el PRIMER
" = ", así que un tr("0 = auto…") entra al catálogo como la clave "0" con valor
"auto…": no falla, no avisa, y esa cadena no se puede traducir nunca. Pasó de
verdad con el tooltip de *Dock length*, que por eso quedó en capabase en los
trece idiomas hasta 2026-08-09.

Uso: check-tr-separator.py <repo>
"""
import re
import sys
from pathlib import Path

repo = Path(sys.argv[1] if len(sys.argv) > 1 else ".")

# tr("…") y qsTr("…") en una sola línea, que es como se escriben en este árbol.
PATTERN = re.compile(r'\bq?sTr\s*\(\s*"([^"\\]*(?:\\.[^"\\]*)*)"')
SOURCES = ["src", "qml", "previews", "tilemenu", "calendar", "controlmanager"]

bad = []
for top in SOURCES:
    root = repo / top
    if not root.is_dir():
        continue
    for path in sorted(root.rglob("*")):
        if path.suffix not in (".cpp", ".h", ".qml") or not path.is_file():
            continue
        for n, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            for text in PATTERN.findall(line):
                if " = " in text:
                    bad.append(f"{path.relative_to(repo)}:{n}: {text[:70]}")

if bad:
    print("Cadenas traducibles con ' = ' adentro (rompen el catálogo):")
    print("\n".join("  " + b for b in bad))
    print("\nEscribilas con ':' en vez de ' = '.")
    sys.exit(1)

print("ok: ninguna cadena traducible contiene ' = '")
