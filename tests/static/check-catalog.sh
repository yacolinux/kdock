#!/bin/bash
# El catálogo de traducciones está al día con el código.
#
# Toda cadena nueva en tr()/qsTr() queda fuera de las traducciones hasta que se
# regenera el catálogo, y eso no falla ni avisa: la cadena simplemente sale en
# capabase en los trece idiomas. Este chequeo corre las dos herramientas y exige
# que no cambien nada.
#
# No usa git a propósito (para que ande igual con el árbol sucio y en un tarball):
# guarda una copia de translations/, corre las herramientas —que reescriben el
# directorio— compara, y **restaura siempre**, incluso si algo falla.
set -uo pipefail

repo=${1:-.}
cd "$repo"

command -v lupdate > /dev/null || { echo "SKIP: lupdate no está instalado" >&2; exit 77; }
command -v python3 > /dev/null || { echo "SKIP: python3 no está instalado" >&2; exit 77; }

snapshot=$(mktemp -d /tmp/kdock-catalog.XXXXXX)
restore() {
    if [ -d "$snapshot/translations" ]; then
        rm -rf translations
        cp -a "$snapshot/translations" translations
    fi
    rm -rf "$snapshot"
}
trap restore EXIT

cp -a translations "$snapshot/translations"

python3 tools/gen-capabase.py    > "$snapshot/gen.log"  2>&1 || { cat "$snapshot/gen.log"; exit 1; }
python3 tools/sync-translations.py > "$snapshot/sync.log" 2>&1 || { cat "$snapshot/sync.log"; exit 1; }

if ! diff -rq "$snapshot/translations" translations > "$snapshot/diff.log" 2>&1; then
    echo "FAIL: el catálogo quedó desactualizado respecto del código:"
    sed 's/^/    /' "$snapshot/diff.log"
    echo
    echo "    Corré:  python3 tools/gen-capabase.py && python3 tools/sync-translations.py"
    echo "    y traducí las claves nuevas antes de commitear."
    exit 1
fi

echo "ok: capabase y los doce idiomas están al día con el código"
