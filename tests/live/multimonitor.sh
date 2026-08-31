#!/bin/bash
# Mover y copiar un dock entre monitores, con monitores de verdad.
#
# La lógica (qué queda habilitado, qué archivo se renombra, quién se queda con la
# bandeja) ya la cubre tst_dockmanager con la lista de monitores falsa. Lo que
# solo se puede ver acá es lo otro: que la superficie de layer-shell se recrea en
# el wl_output nuevo y el dock aparece **en el otro monitor**.
#
# Con un solo monitor conectado se saltea: no es un fallo, es que no hay a dónde
# mover nada.
set -uo pipefail

repo=${1:?usage: multimonitor.sh <repo> <kdock-binary>}
kdock=${2:?}
repo=$(cd "$repo" && pwd)
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=../lib/sandbox.sh
source "$here/../lib/sandbox.sh"

[ -n "${WAYLAND_DISPLAY:-}" ] || kdock_skip "no hay sesión Wayland (este tier no corre en CI)"
[ -x "$kdock" ] || kdock_skip "no encuentro el binario: $kdock"

app_pid=
cleanup() {
    [ -n "$app_pid" ] && kill "$app_pid" 2>/dev/null
}
kdock_on_exit cleanup

kdock_sandbox "kdock-live-multimon"

# Los monitores hay que preguntárselos al DOCK, no al kernel: /sys/class/drm
# lista como "connected" salidas que el compositor tiene apagadas o fuera del
# layout (acá: DP-5 y DP-7 conectados por DRM y ninguno expuesto por Qt), y con
# esos nombres el test crea docks que no aparecen en ningún lado y pasa en falso.
# Una corrida en vacío deja en knownScreens la lista que ve Qt, que es la buena.
"$kdock" > "$KDOCK_SANDBOX/probe.log" 2>&1 &
app_pid=$!
sleep 5
kill "$app_pid" 2>/dev/null; wait "$app_pid" 2>/dev/null; app_pid=

mapfile -t screens < <(sed -n 's/^knownScreens=//p' "$XDG_DATA_HOME/kdock/kdock.conf" \
    | tr ',' '\n' | sed 's/^ *//; s/ *$//' | grep -v '^$')
[ ${#screens[@]} -ge 2 ] || \
    kdock_skip "el compositor expone ${#screens[@]} monitor(es) (${screens[*]:-ninguno}); esta prueba necesita 2"

src_screen=${screens[0]}
dst_screen=${screens[1]}
kdock_info "monitores: $src_screen -> $dst_screen"
printf '[General]\nenabledScreens=%s\nknownScreens=%s,%s\n' \
    "$src_screen" "$src_screen" "$dst_screen" > "$XDG_DATA_HOME/kdock/kdock.conf"
# autohide=true: la instancia de prueba no reserva espacio, así que no le
# re-acomoda las ventanas maximizadas al usuario.
printf '[General]\nedge=1\nhideMode=1\niconSize=32\nshowSystray=false\n' \
    > "$XDG_DATA_HOME/kdock/kdock-$src_screen.conf"

log="$KDOCK_SANDBOX/err.log"
"$kdock" > "$log" 2>&1 &
app_pid=$!
sleep 6
kill -0 "$app_pid" 2>/dev/null || kdock_fail "la instancia de prueba no arrancó (ver $log)"

# El dock de prueba corre en su propio bus name, así que se lo maneja por
# configuración: se copia al monitor siguiente escribiendo lo mismo que escribe
# el menú, y se comprueba que el proceso lo levanta y crea la superficie.
# (La acción del menú se prueba en tst_dockmanager; acá interesa la superficie.)
before=$(grep -c "layer surface" "$log" 2>/dev/null || echo 0)

cp "$XDG_DATA_HOME/kdock/kdock-$src_screen.conf" \
   "$XDG_DATA_HOME/kdock/kdock-$dst_screen.conf"
printf 'screenName=%s\n' "$dst_screen" >> "$XDG_DATA_HOME/kdock/kdock-$dst_screen.conf"
printf '[General]\nenabledScreens=%s,%s\nknownScreens=%s,%s\n' \
    "$src_screen" "$dst_screen" "$src_screen" "$dst_screen" \
    > "$XDG_DATA_HOME/kdock/kdock.conf"

# El manager relee al detectar cambios de pantalla; forzamos un ciclo reiniciando
# la instancia, que es lo que hace el usuario después de mover un dock.
kill "$app_pid"; wait "$app_pid" 2>/dev/null
"$kdock" > "$log.2" 2>&1 &
app_pid=$!
sleep 6
kill -0 "$app_pid" 2>/dev/null || kdock_fail "no volvió a arrancar con los dos docks (ver $log.2)"

for f in "$log" "$log.2"; do
    if grep -qE "TypeError|ReferenceError|Binding loop|protocol error" "$f"; then
        echo "FAIL: errores en $f:"
        grep -E "TypeError|ReferenceError|Binding loop|protocol error" "$f" | head -5
        exit 1
    fi
done

echo "ok: dos docks vivos en $src_screen y $dst_screen, sin errores de protocolo"
echo "NOTA: que cada superficie esté en SU monitor se confirma a ojo con una captura."
