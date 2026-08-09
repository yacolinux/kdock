#!/bin/bash
# Runs a GUI binary under Xvfb, collects its stderr, optionally inspects the X
# session while it is alive, and shuts it down in the one order that does not
# poison the log.
#
#   xvfb-app.sh --log FILE [--settle N] [--screen WxH] [--ready SNIPPET]
#               [--inspect SNIPPET] [--out FILE] -- CMD [ARGS...]
#
# THE ORDER MATTERS, and this is the whole reason the helper exists: the app has
# to be killed **before** the X server goes away. If Xvfb dies first, Qt tears
# down the QML engine with every binding still live and prints hundreds of
# "TypeError: Cannot read property '…' of null" — teardown noise that is
# indistinguishable from a real bug in the log (measured: 15 lines -> 1808).
#
# --inspect runs a shell snippet inside the X session with $APP_PID set, after
# the app has settled; its stdout goes to --out. That is how a test gets at
# xwininfo/xdotool without racing the shutdown.
#
# --ready is a snippet polled until it succeeds (up to --settle seconds), and it
# is what makes the harness portable: a fixed sleep that is generous on a laptop
# is too short on a CI runner with no GPU, and sampling too early sees a window
# that exists but has not been sized by its QML yet — which reads exactly like a
# root that failed to load (measured: 6 s was enough here and not in a container,
# which cost three CI rounds). Without --ready it just sleeps --settle.
set -euo pipefail

log=; settle=6; screen=1920x1080; inspect=; out=/dev/null; ready=
while [ $# -gt 0 ]; do
    case "$1" in
        --log)     log=$2; shift 2 ;;
        --settle)  settle=$2; shift 2 ;;
        --screen)  screen=$2; shift 2 ;;
        --inspect) inspect=$2; shift 2 ;;
        --ready)   ready=$2; shift 2 ;;
        --out)     out=$2; shift 2 ;;
        --)        shift; break ;;
        *)         echo "xvfb-app.sh: unknown option $1" >&2; exit 2 ;;
    esac
done
[ -n "$log" ] || { echo "xvfb-app.sh: --log is required" >&2; exit 2; }
[ $# -gt 0 ] || { echo "xvfb-app.sh: no command given" >&2; exit 2; }

command -v xvfb-run > /dev/null || { echo "SKIP: xvfb-run not installed" >&2; exit 77; }

# The software backend is not a nicety either: without it the GL content never
# shows up in an X capture and every screenshot comes out black.
export QT_QPA_PLATFORM=xcb
export QT_QUICK_BACKEND=software

XVFB_INSPECT=$inspect XVFB_OUT=$out XVFB_LOG=$log XVFB_SETTLE=$settle \
XVFB_READY=$ready \
xvfb-run -a -s "-screen 0 ${screen}x24" bash -c '
    "$@" > "$XVFB_LOG" 2>&1 &
    APP_PID=$!
    if [ -n "$XVFB_READY" ]; then
        # Espera por condición, no por reloj: se sondea hasta que se cumpla o se
        # agote el presupuesto. Que se agote no es un error acá — el que llamó
        # tiene que decidirlo mirando lo que haya.
        deadline=$(( $(date +%s) + XVFB_SETTLE ))
        while [ "$(date +%s)" -lt "$deadline" ]; do
            kill -0 "$APP_PID" 2>/dev/null || break
            eval "$XVFB_READY" > /dev/null 2>&1 && break
            sleep 0.5
        done
    else
        sleep "$XVFB_SETTLE"
    fi
    if ! kill -0 "$APP_PID" 2>/dev/null; then
        echo "xvfb-app: the process died before it settled" >> "$XVFB_LOG"
        exit 1
    fi
    if [ -n "$XVFB_INSPECT" ]; then
        eval "$XVFB_INSPECT" > "$XVFB_OUT" 2>/dev/null || true
    fi
    # Kill first, X server second (see the header).
    kill "$APP_PID" 2>/dev/null || true
    for _ in 1 2 3 4 5 6 7 8 9 10; do
        kill -0 "$APP_PID" 2>/dev/null || break
        sleep 0.2
    done
    kill -9 "$APP_PID" 2>/dev/null || true
    wait "$APP_PID" 2>/dev/null || true
' bash "$@"
