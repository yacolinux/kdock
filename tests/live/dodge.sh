#!/bin/bash
# Intelligent hide (dodge) contra ventanas de verdad. SOLO en la sesión Wayland.
#
# Bajo Xvfb no hay protocolo de ventanas (WindowMonitor::create() devuelve
# nullptr), así que windowsOverlap queda en false y el dock sale siempre visible:
# el tier qml prueba que el QML carga con hideMode=2, **no** que el dodge
# funcione. Esto sí.
#
# Levanta una SEGUNDA instancia aislada (no toca el dock del usuario) con
# KDOCK_DEBUG_DODGE=1, que hace que el dock imprima cada transición de
# windowsOverlap — el estado no está en D-Bus y una captura no alcanza para
# afirmarlo. Después cambia a un escritorio vacío y espera ver el flanco.
#
# Necesita el privilegio de KWin para ver las ventanas: KWin resuelve
# /proc/<pid>/exe contra un Exec= absoluto de algún .desktop con
# X-KDE-Wayland-Interfaces. Se instala uno temporal y se borra al final. Ojo: la
# ruta del repo tiene un '#', que KConfig no parsea en un Exec=, así que el
# binario se copia a un directorio sin '#'.
#
# Todo lo que toca de la sesión (escritorio actual, .desktop) se restaura al
# salir, vía la pila de kdock_on_exit.
set -uo pipefail

repo=${1:?usage: dodge.sh <repo> <kdock-binary>}
kdock=${2:?}
repo=$(cd "$repo" && pwd)
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=../lib/sandbox.sh
source "$here/../lib/sandbox.sh"

[ -n "${WAYLAND_DISPLAY:-}" ] || kdock_skip "no hay sesión Wayland (este tier no corre en CI)"
command -v busctl > /dev/null || kdock_skip "busctl no está instalado"
[ -x "$kdock" ] || kdock_skip "no encuentro el binario: $kdock"

vdm=(--user get-property org.kde.KWin /VirtualDesktopManager org.kde.KWin.VirtualDesktopManager)
desktops=$(busctl "${vdm[@]}" desktops 2>/dev/null) \
    || kdock_skip "KWin no contesta en /VirtualDesktopManager"
original=$(busctl "${vdm[@]}" current | awk '{gsub(/"/,"",$2); print $2}')
last=$(echo "$desktops" | grep -oE '[0-9a-f]{8}-[0-9a-f-]{27}' | tail -1)
[ -n "$original" ] && [ -n "$last" ] || kdock_skip "no pude leer los escritorios virtuales"
[ "$original" != "$last" ] || kdock_skip "estás en el último escritorio: corré esto desde otro"

devdir=$(mktemp -d /tmp/kdock-live.XXXXXX)   # sin '#' en la ruta, ver la cabecera
desktopfile="$HOME/.local/share/applications/kdock-livetest.desktop"
app_pid=

cleanup() {
    [ -n "$app_pid" ] && kill "$app_pid" 2>/dev/null
    busctl --user set-property org.kde.KWin /VirtualDesktopManager \
        org.kde.KWin.VirtualDesktopManager current s "$original" 2>/dev/null
    rm -f "$desktopfile"
    rm -rf "$devdir"
}
kdock_on_exit cleanup

cp "$kdock" "$devdir/kdock"
cat > "$desktopfile" <<EOF
[Desktop Entry]
Type=Application
Name=kdock live test
Exec=$devdir/kdock
NoDisplay=true
X-KDE-Wayland-Interfaces=org_kde_plasma_window_management
EOF
# Ruta explícita + XDG_DATA_DIRS: un kbuildsycoca6 pelado en esta máquina escribe
# la base de la OTRA versión de KF6 y rompe los "abrir con" del escritorio.
if [ -x /opt/kde/bin/kbuildsycoca6 ]; then
    env XDG_DATA_DIRS="/opt/kde/share:${XDG_DATA_DIRS:-/usr/share}" \
        /opt/kde/bin/kbuildsycoca6 > /dev/null 2>&1
else
    kbuildsycoca6 > /dev/null 2>&1
fi

kdock_sandbox "kdock-live-dodge"

# --- fase 1: qué monitor usar --------------------------------------------------
# Se le pregunta al dock, no al kernel: /sys/class/drm lista como "connected"
# salidas que el compositor tiene apagadas, y un dock en una de esas no aparece
# en ningún lado — el test pasaría en falso. Una corrida en vacío deja en
# knownScreens la lista que ve Qt.
"$devdir/kdock" > "$KDOCK_SANDBOX/probe.log" 2>&1 &
app_pid=$!
sleep 5
kill "$app_pid" 2>/dev/null; wait "$app_pid" 2>/dev/null; app_pid=

screen=${KDOCK_LIVE_SCREEN:-$(sed -n 's/^knownScreens=//p' "$XDG_DATA_HOME/kdock/kdock.conf" \
    | cut -d, -f1 | tr -d ' ')}
[ -n "$screen" ] || kdock_skip "el compositor no expuso ningún monitor"
kdock_info "monitor: $screen"

# --- fase 2: el dock de prueba, en modo dodge ---------------------------------
# Alineación centrada a propósito: un dock centrado esquiva sobre la franja del
# borde ENTERO (el compositor decide dónde lo pone y el cliente no se entera),
# así que cualquier ventana que toque ese borde lo activa. Con Start/End el test
# dependería de dónde tenga las ventanas quien lo corra.
printf '[General]\nenabledScreens=%s\nknownScreens=%s\n' "$screen" "$screen" \
    > "$XDG_DATA_HOME/kdock/kdock.conf"
printf '[General]\nedge=0\nhideMode=2\nalignment=1\ndockLength=0\niconSize=32\nshowSystray=false\n' \
    > "$XDG_DATA_HOME/kdock/kdock-$screen.conf"

log="$KDOCK_SANDBOX/err.log"
KDOCK_DEBUG_DODGE=1 "$devdir/kdock" > "$log" 2>&1 &
app_pid=$!
sleep 6
kill -0 "$app_pid" 2>/dev/null || kdock_fail "la instancia de prueba no arrancó (ver $log)"
grep -q "compositor offers neither" "$log" && \
    kdock_fail "KWin no concedió org_kde_plasma_window_management: el .desktop temporal no tomó"

# El estado flapea mientras el dock se arma (la superficie cambia de tamaño y el
# rect con ella), así que se espera a que se quede quieto en vez de muestrear a
# ciegas: dos lecturas iguales separadas por un segundo.
settled_overlap() {
    local prev cur i
    prev=$(grep -oE 'dodge overlap=[01]' "$log" | tail -1 | grep -oE '[01]$')
    for i in 1 2 3 4 5 6 7 8; do
        sleep 1
        cur=$(grep -oE 'dodge overlap=[01]' "$log" | tail -1 | grep -oE '[01]$')
        [ "$cur" = "$prev" ] && { echo "$cur"; return; }
        prev=$cur
    done
    echo "$prev"
}

busy=$(settled_overlap)
[ -n "$busy" ] || kdock_fail "el dock no reportó ningún estado de dodge (¿quedó fuera del modo 2?)"
kdock_info "escritorio de trabajo: overlap=$busy"

busctl --user set-property org.kde.KWin /VirtualDesktopManager \
    org.kde.KWin.VirtualDesktopManager current s "$last"
empty=$(settled_overlap)
kdock_info "escritorio vacío:      overlap=$empty"

busctl --user set-property org.kde.KWin /VirtualDesktopManager \
    org.kde.KWin.VirtualDesktopManager current s "$original"
back=$(settled_overlap)
kdock_info "de vuelta:             overlap=$back"

problems=0
if [ "$empty" != "0" ]; then
    echo "FAIL: en un escritorio vacío nada solapa, el dock tiene que estar visible (overlap=0)"
    problems=1
fi
if [ "$back" != "$busy" ]; then
    echo "FAIL: volver al escritorio original no restauró el estado ($back != $busy)"
    problems=1
fi
if [ "$busy" != "1" ]; then
    echo "AVISO: no había ninguna ventana sobre el borde de abajo, así que la mitad"
    echo "       'se esconde' no se ejercitó. Abrí una ventana ahí y repetí."
fi
if grep -qE "TypeError|ReferenceError|Binding loop" "$log"; then
    echo "FAIL: errores de QML durante el ciclo:"
    grep -E "TypeError|ReferenceError|Binding loop" "$log" | head -5
    problems=1
fi

[ $problems -eq 0 ] || exit 1
echo "ok: el dodge sigue las ventanas reales y el escritorio virtual actual"
