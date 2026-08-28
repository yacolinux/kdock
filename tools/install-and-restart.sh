#!/bin/bash
# Build, install and hand the running session over to the newly installed
# binaries. The D-Bus names are the stable session contract; no process-name or
# compositor-specific session inspection is needed.
set -euo pipefail

repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
build=${KDOCK_BUILD_DIR:-"$repo/build"}

env -u CC -u CXX cmake -S "$repo" -B "$build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release
env -u CC -u CXX cmake --build "$build" -j
sudo cmake --install "$build"

if command -v kbuildsycoca6 > /dev/null 2>&1; then
    kbuildsycoca6 --noincremental
fi

qdbus_tool=$(command -v qdbus6 || command -v qdbus || true)
if [[ -z "$qdbus_tool" ]]; then
    echo "Kdock instalado; no encontré qdbus6/qdbus para reiniciar la sesión." >&2
    exit 0
fi

has_owner()
{
    "$qdbus_tool" --session org.freedesktop.DBus /org/freedesktop/DBus \
        org.freedesktop.DBus.NameHasOwner "$1" 2>/dev/null | grep -qx true
}

wait_until_gone()
{
    local service=$1
    for _ in {1..80}; do
        if ! has_owner "$service"; then
            return 0
        fi
        sleep 0.1
    done
    return 1
}

wait_until_back()
{
    local service=$1
    for _ in {1..100}; do
        if has_owner "$service"; then
            return 0
        fi
        sleep 0.1
    done
    return 1
}

# The normal in-app restart deliberately keeps the SNI host resident so tray
# clients are not disturbed. After an installation that would leave the old
# executable mapped, so recycle it first; the new kdock starts it again when
# tray preloading is enabled. If kdock is not running, do not stop a standalone
# tray host that we cannot safely bring back through kdock.
if has_owner org.kdock.Dock; then
    if has_owner org.kdock.Systray; then
        "$qdbus_tool" --session org.kdock.Systray /Systray \
            org.kdock.Systray.quit
        wait_until_gone org.kdock.Systray || {
            echo "La bandeja no liberó org.kdock.Systray dentro del tiempo esperado." >&2
            exit 1
        }
    fi

    "$qdbus_tool" --session org.kdock.Dock /Dock org.kdock.Dock.restart
    wait_until_back org.kdock.Dock || {
        echo "Kdock no volvió a registrar org.kdock.Dock después del reinicio." >&2
        exit 1
    }
    echo "Kdock y sus accesorios fueron reiniciados desde la instalación nueva."
else
    echo "Kdock instalado; no había org.kdock.Dock activo, no se lanzó otra instancia."
fi
