#!/bin/bash
# One command to run the suite.
#
#   tests/run.sh                 # todo lo que corre en CI: static + unit + qml
#   tests/run.sh --tier unit     # un tier suelto (static|unit|qml|live)
#   tests/run.sh --tier all      # incluye live (necesita tu sesión Wayland)
#   tests/run.sh -R dockconfig   # filtra por nombre, como ctest
#
# Configura y compila si hace falta. El `env -u CC -u CXX` es una particularidad
# de esta máquina (CC/CXX apuntan a ccache y CMake falla si están seteadas); en
# CI no molesta.
set -euo pipefail

repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
build=${KDOCK_BUILD_DIR:-$repo/build}
tier=ci
extra=()

while [ $# -gt 0 ]; do
    case "$1" in
        --tier) tier=$2; shift 2 ;;
        --build-dir) build=$2; shift 2 ;;
        -h|--help) sed -n '2,12p' "$0"; exit 0 ;;
        *) extra+=("$1"); shift ;;
    esac
done

case "$tier" in
    ci)     filter=(-LE live) ;;
    all)    filter=() ;;
    static|unit|qml|live) filter=(-L "$tier") ;;
    *) echo "tier desconocido: $tier (static|unit|qml|live|ci|all)" >&2; exit 2 ;;
esac

echo "==> configurando ($build)"
env -u CC -u CXX cmake -B "$build" -S "$repo" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DKDOCK_BUILD_TESTS=ON > /dev/null

echo "==> compilando"
env -u CC -u CXX cmake --build "$build" -j > /dev/null

echo "==> ctest (${tier})"
ctest --test-dir "$build" --output-on-failure "${filter[@]}" "${extra[@]}"
