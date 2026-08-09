#!/bin/bash
# Los binarios accesorios resuelven su contenido: kdock-tilemenu --dump-layout y
# kdock-controlmanager --dump-sections.
#
# Los dos modos de volcado existen justamente para esto: imprimen lo que el
# binario resolvió y salen sin abrir ninguna ventana, así que son la forma más
# barata de probarlos de punta a punta.
#
# Se asertan INVARIANTES ESTRUCTURALES, no un golden de la salida completa: el
# volcado del panel de control incluye monitores, reproductores MPRIS y lo que
# conteste D-Bus, o sea cosas que cambian de máquina en máquina. Un golden ahí
# sería rojo en CI el primer día y nadie volvería a mirarlo.
set -uo pipefail

repo=${1:?usage: accessories.sh <repo> <tilemenu> <controlmanager>}
tilemenu=${2:?}
controlmanager=${3:?}
# Absoluta: los symlinks del sandbox se resuelven desde OTRO directorio,
# así que una ruta relativa deja enlaces rotos y el test falla por eso.
repo=$(cd "$repo" && pwd)
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=../lib/sandbox.sh
source "$here/../lib/sandbox.sh"

command -v dbus-run-session > /dev/null || kdock_skip "dbus-run-session no está instalado"

failures=0
export QT_QPA_PLATFORM=offscreen

# ---------------------------------------------------------------- tile menu --
kdock_sandbox "kdock-tilemenu-dump"
# XDG_DATA_DIRS también, no solo XDG_DATA_HOME: el índice de apps lee los dos, y
# con los .desktop del sistema adentro el conteo depende de la máquina.
mkdir -p "$KDOCK_SANDBOX/dirs"
ln -sfn "$repo/tests/fixtures/applications" "$KDOCK_SANDBOX/dirs/applications"
export XDG_DATA_DIRS="$KDOCK_SANDBOX/dirs"
expected_tiles=$(find "$repo/tests/fixtures/applications" -name '*.desktop' | wc -l)

out="$KDOCK_SANDBOX/tilemenu.txt"
if timeout 60 "$tilemenu" --dump-layout > "$out" 2>&1; then
    got=$(grep -oE '^== cat:__all__ ==.*[0-9]+ tile' "$out" | grep -oE '[0-9]+ tile' | grep -oE '[0-9]+')
    if [ "$got" = "$expected_tiles" ]; then
        echo "ok: kdock-tilemenu resolvió los $expected_tiles mosaicos de fixture"
    else
        echo "FAIL: kdock-tilemenu vio '$got' mosaicos, esperaba $expected_tiles"
        sed 's/^/    /' "$out" | head -10
        failures=$((failures + 1))
    fi
    # Y que la categorización siga funcionando (un fixture es de Development).
    if ! grep -q "^== cat:Development ==" "$out"; then
        echo "FAIL: kdock-tilemenu no armó la categoría Development"
        failures=$((failures + 1))
    fi
else
    echo "FAIL: kdock-tilemenu --dump-layout salió con error"
    sed 's/^/    /' "$out" | head -10
    failures=$((failures + 1))
fi
unset XDG_DATA_DIRS
kdock_sandbox_cleanup

# ----------------------------------------------------------- control manager --
kdock_sandbox "kdock-cm-dump"
out="$KDOCK_SANDBOX/controlmanager.txt"
# Sin WAYLAND_DISPLAY: con la sesión del usuario a la vista, el binario se pone
# QT_WAYLAND_SHELL_INTEGRATION=kdock-layershell y abriría el panel EN LA PANTALLA
# DE VERDAD. Y con bus propio (dbus-run-session) para no hablarle a la instancia
# real; el precio es que los backends salen con sus mensajes de "no responde",
# que es lo correcto y no un bug.
if env -u WAYLAND_DISPLAY timeout 60 dbus-run-session -- \
        "$controlmanager" --dump-sections > "$out" 2>&1; then
    ok=1
    grep -q "^== Principal ==" "$out" || { echo "FAIL: falta la grilla Principal"; ok=0; }
    grep -q "^== secciones ==" "$out" || { echo "FAIL: falta la tabla de secciones"; ok=0; }
    for section in clock audio video calendar play network wallpaper system; do
        grep -qE "^  $section " "$out" || {
            echo "FAIL: la sección '$section' no aparece en el volcado"; ok=0; }
    done
    if [ $ok -eq 1 ]; then
        echo "ok: kdock-controlmanager resolvió la grilla y sus ocho secciones"
    else
        sed 's/^/    /' "$out" | head -15
        failures=$((failures + 1))
    fi
else
    echo "FAIL: kdock-controlmanager --dump-sections salió con error"
    sed 's/^/    /' "$out" | head -15
    failures=$((failures + 1))
fi
kdock_sandbox_cleanup

[ $failures -eq 0 ] || kdock_fail "$failures accesorio(s) con problemas"
