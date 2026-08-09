#!/bin/bash
# qmllint sobre los cinco árboles de QML.
#
# El build NO valida el QML (rcc solo lo empaqueta), así que un error de sintaxis
# recién aparece cuando el binario arranca — y si ya lo instalaste, el usuario se
# queda sin dock. Esta es la primera reja; la segunda es tests/qml/smoke.sh, que
# ve lo que el análisis estático no puede (los errores de scope de ids).
#
# Se falla SOLO por diagnósticos `critical` del volcado JSON, no por grep sobre el
# texto. Dos motivos, los dos medidos:
#
#   - Las advertencias de "unqualified access" son esperables y son casi todo el
#     ruido (1443 en este árbol): todo el QML del proyecto usa las context
#     properties config/theme/dockModel. Fallar por ellas sería rojo permanente.
#   - Un grep por "error" es a la vez demasiado y demasiado poco: matchea la
#     línea de código citada en una advertencia (`popup.errorText = ""` rompió la
#     corrida de CI del 2026-08-09) y NO matchea un error de sintaxis de verdad,
#     que se imprime como "Expected token `;'" — sin la palabra "error".
set -uo pipefail

repo=${1:-.}
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=../lib/sandbox.sh
source "$here/../lib/sandbox.sh"
cd "$repo"

qmllint=$(kdock_qt_tool qmllint) || kdock_skip "no encontré qmllint (ni en el Qt de qmake6 ni en el PATH)"

# El volcado JSON existe desde Qt 6.5; con el qmllint de Qt5 (que solo sabe de
# sintaxis) no hay forma de separar errores de advertencias, así que se saltea en
# vez de dar un verde que no significa nada.
# Ojo con `... | grep -q` bajo `pipefail`: grep corta apenas matchea, qmllint se
# muere con SIGPIPE y el pipeline devuelve 141 AUNQUE el patrón esté. Se captura
# la salida primero.
qmllint_help=$("$qmllint" --help 2>&1)
case "$qmllint_help" in
    *--json*) ;;
    *) kdock_skip "el qmllint encontrado ($qmllint) no soporta --json: parece de Qt5" ;;
esac

trees=(qml previews/qml tilemenu/qml controlmanager/qml controlmanager/qml/cards)
report=$(mktemp /tmp/kdock-qmllint.XXXXXX.json)
trap 'rm -f "$report"' EXIT
status=0

for tree in "${trees[@]}"; do
    [ -d "$tree" ] || continue
    shopt -s nullglob
    files=("$tree"/*.qml)
    shopt -u nullglob
    [ ${#files[@]} -gt 0 ] || continue

    "$qmllint" --json "$report" "${files[@]}" > /dev/null 2>&1
    out=$(python3 - "$report" "$tree" "${#files[@]}" <<'PY'
import json, sys
report, tree, count = sys.argv[1], sys.argv[2], sys.argv[3]
with open(report, encoding="utf-8") as fh:
    data = json.load(fh)
critical, other = [], 0
for entry in data.get("files", []):
    name = entry.get("filename", "?")
    for w in entry.get("warnings", []):
        if w.get("type") == "critical":
            critical.append(f"{name}:{w.get('line', '?')}: {w.get('message', '')}")
        else:
            other += 1
if critical:
    print(f"FAIL: {tree}")
    for line in critical[:10]:
        print("    " + line)
    sys.exit(1)
print(f"ok: {tree} ({count} archivos, {other} advertencias no críticas)")
PY
    ) || status=1
    echo "$out"
done

exit $status
