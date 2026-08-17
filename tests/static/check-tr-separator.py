#!/usr/bin/env python3
"""Ninguna cadena traducible puede contener " = ".

El formato de translations/*.md es `clave = texto` y `splitEntry()`
(`src/translations.cpp`) parte por el PRIMER " = ", así que un tr("0 = auto…")
entra al catálogo como la clave "0" con valor "auto…": no falla, no avisa, y esa
cadena no se puede traducir nunca. Pasó de verdad con el tooltip de *Dock
length*, que por eso quedó en capabase en los trece idiomas hasta 2026-08-09.

Dos agujeros que tuvo este chequeo, los dos tapados el 2026-08-17 y los dos
congelados en SELFTEST más abajo:

1. **El patrón era `\\bq?sTr`**, que matchea `qsTr` y `sTr` pero **nunca `tr`**.
   O sea que durante toda su vida este script miró el QML y no revisó un solo
   `tr()` de C++. Cuando se lo corrigió aparecieron dos cadenas rotas que ya
   estaban en el árbol, una de ellas con su traducción al español partida al
   medio en `spanish.md`.
2. **C++ concatena literales adyacentes y el chequeo iba línea por línea.** Un
   tr() partido en dos líneas —lo normal en este árbol: hay ~147— escondía el
   " = " en la segunda, que el patrón ni empezaba a matchear. Se lo comió una
   cadena nueva de la solapa General (vista previa de ventana) y lo que la
   encontró fue mirar el diff del catálogo a mano, no el test.
   **QML no concatena literales adyacentes** (ver CLAUDE-TRAMPS.md), así que la
   unión se hace solo en .cpp/.h.

Uso: check-tr-separator.py <repo>
"""
import re
import sys
from pathlib import Path

SEPARATOR = " = "

# Un literal de C/C++/JS: "…" con escapes.
LITERAL = r'"(?:[^"\\]|\\.)*"'
# tr(…) / qsTr(…) y la corrida de literales adyacentes que le sigue. Cubre
# `QObject::tr(` y `->tr(` porque `:` y `>` no son caracteres de palabra, y no
# matchea el `tr` de `attr(`/`str(` porque ahí no hay borde de palabra.
# El `\s*` entre literales cruza saltos de línea; el `(?:\s*LITERAL)*` se corta
# solo en la coma, así que el segundo argumento de tr() —el contexto de
# desambiguación, que no es clave del catálogo— nunca entra.
CALL = re.compile(r'\b(?:qsTr|tr)\s*\(\s*(' + LITERAL + r'(?:\s*' + LITERAL + r')*)')

SOURCES = ["src", "qml", "previews", "tilemenu", "calendar", "controlmanager"]
CPP_SUFFIXES = (".cpp", ".h")
SUFFIXES = CPP_SUFFIXES + (".qml",)


def offenders(text, join_adjacent):
    """(línea, cadena) por cada tr()/qsTr() cuyo texto contiene " = ".

    `join_adjacent` es lo que distingue C++ de QML: en C++ el compilador pega los
    literales contiguos y lupdate ve UNA cadena, que es la que va al catálogo.
    """
    for match in CALL.finditer(text):
        parts = re.findall(LITERAL, match.group(1))
        if not join_adjacent:
            parts = parts[:1]
        # Sin las comillas. El cuerpo se deja tal cual (con sus escapes): el
        # " = " que importa se escribe igual en el fuente que en el catálogo.
        body = "".join(part[1:-1] for part in parts)
        if SEPARATOR in body:
            yield text.count("\n", 0, match.start()) + 1, body


# Casos congelados: (fuente, es_cpp, tiene_que_saltar). Corren en cada corrida,
# antes del árbol. Un chequeo que dejó de cubrir lo suyo se ve exactamente igual
# que un árbol limpio, que es como los dos agujeros de arriba duraron meses.
SELFTEST = [
    # -- tiene que saltar --------------------------------------------------
    # Agujero 1: tr() pelado de C++, una sola línea.
    ('label->setText(tr("0 = automático"));', True, True),
    ('QObject::tr("a = b")', True, True),
    # Agujero 2: tr() partido, con el separador entero en el segundo literal.
    ('setToolTip(tr("Cuánto tarda en aparecer. "\n'
     '              "0 = enseguida."));', True, True),
    # …y con el separador a caballo de la unión, que es donde una revisión a ojo
    # tampoco lo ve.
    ('tr("Con 0 =" " enseguida")', True, True),
    ('text: qsTr("0 = auto")', False, True),
    # -- NO tiene que saltar -----------------------------------------------
    ('tr("clave=valor")', True, False),          # sin espacios alrededor
    ('tr("uno", "contexto = dos")', True, False),  # el 2º arg no es clave
    ('foo("a = b")', True, False),               # no es una llamada a tr
    ('attr("a = b"); str("c = d")', True, False),  # `tr` adentro de otra palabra
    ('tr("una " "cadena larga")', True, False),  # partida pero sana
    ('qsTr("clave=valor")', False, False),
]

failures = []
for source, is_cpp, should_flag in SELFTEST:
    flagged = bool(list(offenders(source, join_adjacent=is_cpp)))
    if flagged != should_flag:
        verb = "no detectó" if should_flag else "detectó de más"
        failures.append(f"  {verb}: {source!r} ({'cpp' if is_cpp else 'qml'})")
if failures:
    print("El chequeo está roto: falló su propio autotest.")
    print("\n".join(failures))
    sys.exit(1)

repo = Path(sys.argv[1] if len(sys.argv) > 1 else ".")

bad = []
for top in SOURCES:
    root = repo / top
    if not root.is_dir():
        continue
    for path in sorted(root.rglob("*")):
        if path.suffix not in SUFFIXES or not path.is_file():
            continue
        text = path.read_text(encoding="utf-8")
        for line, body in offenders(text, join_adjacent=path.suffix in CPP_SUFFIXES):
            bad.append(f"{path.relative_to(repo)}:{line}: {body[:70]}")

if bad:
    print('Cadenas traducibles con " = " adentro (rompen el catálogo):')
    print("\n".join("  " + b for b in bad))
    print("\nEscribilas con ':' en vez de ' = '.")
    sys.exit(1)

print('ok: ninguna cadena traducible contiene " = " '
      f"({len(SELFTEST)} casos de autotest incluidos)")
