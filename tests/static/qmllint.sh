#!/bin/bash
# qmllint sobre los cinco árboles de QML.
#
# El build NO valida el QML (rcc solo lo empaqueta), así que un error de sintaxis
# recién aparece cuando el binario arranca — y si ya lo instalaste, el usuario se
# queda sin dock. Esto es la primera reja; la segunda es tests/qml/smoke.sh, que
# es la única que ve los errores de scope (qmllint no los detecta).
#
# Las advertencias de "unqualified access" son esperables y se ignoran: todo el
# QML del proyecto usa las context properties config/theme/dockModel.
set -uo pipefail

repo=${1:-.}
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=../lib/sandbox.sh
source "$here/../lib/sandbox.sh"
cd "$repo"

qmllint=$(kdock_qt_tool qmllint) || kdock_skip "no encontré qmllint (ni en el PATH ni en el Qt de qmake6)"

trees=(qml previews/qml tilemenu/qml controlmanager/qml controlmanager/qml/cards)
status=0

for tree in "${trees[@]}"; do
    [ -d "$tree" ] || continue
    shopt -s nullglob
    files=("$tree"/*.qml)
    shopt -u nullglob
    [ ${#files[@]} -gt 0 ] || continue

    out=$("$qmllint" "${files[@]}" 2>&1 | grep -iE "error|syntax" || true)
    if [ -n "$out" ]; then
        echo "FAIL: $tree"
        echo "$out" | sed 's/^/    /'
        status=1
    else
        echo "ok: $tree (${#files[@]} archivos)"
    fi
done

exit $status
