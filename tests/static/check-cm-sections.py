#!/usr/bin/env python3
"""Cada sección del panel de control tiene su tarjeta, y la tarjeta está en el qrc.

Agregar una sección se toca en tres lugares y **el que se olvida no da error**:
la fila en CmSections::all() la hace aparecer en la barra de solapas y en la
configuración, pero sin su `case` en CmSectionView.qml el Loader queda vacío —
la solapa se abre en blanco y la tarjeta no dibuja nada, sin una sola línea en
el log. Pasó con la sección `colorauto` (2026-08-12).

Se cruzan las tres puntas:

  1. cada id de la tabla de CmSections tiene un case en CmSectionView.qml;
  2. el .qml al que ese case apunta existe;
  3. y está declarado en controlmanager/CMakeLists.txt (si no, no entra al qrc
     y el Loader falla en runtime, que es el mismo síntoma).

Uso: check-cm-sections.py <repo>
"""
import re
import sys
from pathlib import Path

repo = Path(sys.argv[1] if len(sys.argv) > 1 else ".")
cm = repo / "controlmanager"
sections_cpp = (cm / "src" / "cmsections.cpp").read_text(encoding="utf-8")
view_qml = (cm / "qml" / "CmSectionView.qml").read_text(encoding="utf-8")
cmake = (cm / "CMakeLists.txt").read_text(encoding="utf-8")

# La tabla: {QStringLiteral("id"), QStringLiteral("icono"), ...}
table = sections_cpp[sections_cpp.index("static const QList<CmSectionInfo> t = {"):]
table = table[: table.index("};")]
ids = re.findall(r'\{QStringLiteral\("([^"]+)"\)', table)

# Los case del switch: case "id": return "cards/XCard.qml"
cases = dict(re.findall(r'case\s+"([^"]+)":\s*return\s+"([^"]+)"', view_qml))

problems = []
for sid in ids:
    if sid not in cases:
        problems.append(
            f'la sección "{sid}" no tiene case en CmSectionView.qml: su solapa abre vacía'
        )
        continue
    rel = cases[sid]
    if not (cm / "qml" / rel).exists():
        problems.append(f'la sección "{sid}" apunta a {rel}, que no existe')
    elif rel not in cmake:
        problems.append(f"{rel} no está declarado en controlmanager/CMakeLists.txt (no entra al qrc)")

# Y al revés: un case que ya no tiene sección es código muerto, no un fallo.
for sid in cases:
    if sid not in ids:
        problems.append(f'CmSectionView.qml tiene un case "{sid}" que no está en CmSections')

if problems:
    print("check-cm-sections: FALLA")
    for p in problems:
        print("  -", p)
    sys.exit(1)

print(f"ok: las {len(ids)} secciones del panel tienen tarjeta, y está en el qrc")
