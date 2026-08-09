#!/usr/bin/env python3
"""Todo .qml del árbol tiene que estar en algún qt_add_resources.

Un .qml que no entra al qrc no existe para el binario: el componente no carga y
no hay error de compilación. Hay cuatro listas (raíz, previews, tilemenu y
controlmanager, que además lista `qml/cards/*.qml` una por una), y agregar el
archivo a mano es justo el paso que se olvida.

Uso: check-qml-resources.py <repo>
"""
import sys
from pathlib import Path

repo = Path(sys.argv[1] if len(sys.argv) > 1 else ".")

CMAKES = [
    repo / "CMakeLists.txt",
    repo / "previews" / "CMakeLists.txt",
    repo / "tilemenu" / "CMakeLists.txt",
    repo / "controlmanager" / "CMakeLists.txt",
]
TREES = [
    repo / "qml",
    repo / "previews" / "qml",
    repo / "tilemenu" / "qml",
    repo / "controlmanager" / "qml",
    repo / "controlmanager" / "qml" / "cards",
]

declared = "\n".join(p.read_text(encoding="utf-8") for p in CMAKES if p.is_file())

missing = []
checked = 0
for tree in TREES:
    if not tree.is_dir():
        continue
    for qml in sorted(tree.glob("*.qml")):
        checked += 1
        # Basta con que el nombre aparezca en algún qt_add_resources: las cuatro
        # listas usan rutas relativas distintas (qml/X.qml, cards/X.qml, ...).
        if qml.name not in declared:
            missing.append(str(qml.relative_to(repo)))

if missing:
    print("Archivos .qml que no están en ningún qt_add_resources:")
    print("\n".join("  " + m for m in missing))
    print("\nAgregalos al CMakeLists.txt del árbol que corresponda.")
    sys.exit(1)

print(f"ok: los {checked} archivos .qml están declarados en el qrc")
