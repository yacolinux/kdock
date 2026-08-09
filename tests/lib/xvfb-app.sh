#!/bin/bash
# Runs a GUI binary under Xvfb, collects its stderr, optionally inspects the X
# session while it is alive, and shuts it down in the one order that does not
# poison the log.
#
#   xvfb-app.sh --log FILE [--settle N] [--screen WxH] [--inspect SNIPPET]
#               [--out FILE] -- CMD [ARGS...]
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
set -euo pipefail

log=; settle=6; screen=1920x1080; inspect=; out=/dev/null
while [ $# -gt 0 ]; do
    case "$1" in
        --log)     log=$2; shift 2 ;;
        --settle)  settle=$2; shift 2 ;;
        --screen)  screen=$2; shift 2 ;;
        --inspect) inspect=$2; shift 2 ;;
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
xvfb-run -a -s "-screen 0 ${screen}x24" bash -c '
    "$@" > "$XVFB_LOG" 2>&1 &
    APP_PID=$!
    sleep "$XVFB_SETTLE"
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
