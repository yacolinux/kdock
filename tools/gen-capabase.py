#!/usr/bin/env python3
"""Regenerate translations/capabase.md from the source tree.

capabase is the native layer: every string exactly as it is written in the code,
so a translation file is a copy of this one with the right-hand sides replaced.
The catalogue comes from lupdate (the same extractor Qt uses for tr()/qsTr()),
because there is no way to enumerate tr() calls at runtime.

Sections:
  Configuracion  strings whose lupdate context is a C++ class (the dialogs)
  UIdock         strings whose context is a .qml file (the dock surface)
  Widgets        section token -> factory label, from DockConfig::defaultWidgetLabel
  Apps           left empty; filled at runtime from the installed .desktop files

Run from the repo root:  python3 tools/gen-capabase.py
"""

import re
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "translations" / "capabase.md"

# lupdate names a QML context after the file, so the contexts of every qml/ tree
# are exactly the UIdock bucket. Everything else is a C++ class.
QML_DIRS = [ROOT / "qml", ROOT / "previews" / "qml", ROOT / "tilemenu" / "qml",
            ROOT / "controlmanager" / "qml", ROOT / "controlmanager" / "qml" / "cards",
            ROOT / "weather" / "qml", ROOT / "systray" / "qml",
            ROOT / "desktop" / "qml"]
QML_CONTEXTS = {p.stem for d in QML_DIRS for p in d.glob("*.qml")}

# The four binaries share one catalogue: the accessories' strings live in the
# same .md files as the dock's (see AGENTS.md -> Capa de traducciones). Their
# language is not chosen apart either — they follow kdock's, minus the ALT part.
SCAN_DIRS = [ROOT / "src", ROOT / "qml",
             ROOT / "previews" / "src", ROOT / "previews" / "qml",
             ROOT / "tilemenu" / "src", ROOT / "tilemenu" / "qml",
             ROOT / "controlmanager" / "src", ROOT / "controlmanager" / "qml",
             ROOT / "weather" / "src", ROOT / "weather" / "qml",
             ROOT / "systray" / "src", ROOT / "systray" / "qml",
             ROOT / "desktop" / "src", ROOT / "desktop" / "qml"]

HEADER = """# capabase

<!-- Capa nativa de kdock: los textos tal como están en el código.
     Para traducir, copiá este archivo con el nombre del idioma y cambiá el
     lado derecho de cada línea.

     Formato: una entrada por línea, "clave = texto". Separa el PRIMER " = ".
     Escapes: \\n salto de línea, \\\\ barra invertida.
     Los marcadores %1 %2 … se conservan tal cual o el texto sale mal armado.
     Una clave ausente (o con el valor vacío) usa el texto por defecto: capabase
     para Configuracion/UIdock/Widgets, y el Name= del .desktop para Apps.

     Configuracion: cadenas del diálogo de Configuración (clave = texto capabase).
     UIdock:        cadenas que dibuja el dock: menús, tooltips, popups.
     Widgets:       nombres de los widgets, por token interno.
     Apps:          nombres de aplicaciones, por id de .desktop. Se puebla con
                    el botón "Actualizar apps" de la solapa Traducciones. -->
"""


def escape(text: str) -> str:
    return text.replace("\\", "\\\\").replace("\n", "\\n")


def widget_labels() -> list[tuple[str, str]]:
    """token -> factory label, parsed out of DockConfig::defaultWidgetLabel."""
    src = (ROOT / "src" / "dockconfig.cpp").read_text(encoding="utf-8")
    body = src.split("QString DockConfig::defaultWidgetLabel", 1)[1].split("};", 1)[0]
    pairs = re.findall(
        r'\{QStringLiteral\("([^"]+)"\),\s*(?:tr|QStringLiteral)\("((?:[^"\\]|\\.)*)"\)\}',
        body,
    )
    return [(token, label.replace('\\"', '"')) for token, label in pairs]


def main() -> int:
    with tempfile.TemporaryDirectory() as tmp:
        ts = Path(tmp) / "kdock.ts"
        subprocess.run(
            ["lupdate", "-no-obsolete", "-locations", "none"]
            + [str(d) for d in SCAN_DIRS] + ["-ts", str(ts)],
            check=True, stdout=subprocess.DEVNULL,
        )
        tree = ET.parse(ts)

    config: list[str] = []
    uidock: list[str] = []
    seen: set[str] = set()
    for context in tree.getroot():
        name = context.find("name").text
        # The DockConfig context is nothing but the widget labels, which get
        # their own section keyed by token: skip it here or every widget name
        # would exist twice, with two different keys.
        if name == "DockConfig":
            continue
        bucket = uidock if name in QML_CONTEXTS else config
        for message in context.findall("message"):
            source = message.find("source").text or ""
            # The lookup map is shared between the two sections, so a string
            # used in both places is written once, in whichever comes first.
            if source in seen:
                continue
            seen.add(source)
            bucket.append(source)

    lines = [HEADER, "## Configuracion"]
    lines += [f"{escape(s)} = {escape(s)}" for s in sorted(config, key=str.lower)]
    lines += ["", "## UIdock"]
    lines += [f"{escape(s)} = {escape(s)}" for s in sorted(uidock, key=str.lower)]
    lines += ["", "## Widgets"]
    lines += [f"{token} = {escape(label)}" for token, label in widget_labels()]
    lines += ["", "## Apps", ""]

    OUT.parent.mkdir(exist_ok=True)
    OUT.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"{OUT}: {len(config)} Configuracion, {len(uidock)} UIdock, "
          f"{len(widget_labels())} Widgets")
    return 0


if __name__ == "__main__":
    sys.exit(main())
