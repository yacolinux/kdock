#!/bin/bash
# El QML del dock carga, corre y dibuja — con una matriz de configuraciones.
#
# Es la única reja que ve los errores de SCOPE de ids: qmllint es análisis
# estático y no los detecta. Un `ReferenceError: dockRepeater is not defined`
# pasó el lint, se instaló, y dejó una feature entera muerta en silencio.
#
# DOS COSAS QUE HACEN QUE ESTE TEST NO MIENTA, y son la mitad del valor:
#
#   1. Silencio no alcanza. Una corrida sale limpia también cuando el QML NUNCA
#      se instanció (sin dock no hay QML que fallar). Por eso, además de mirar
#      stderr, cada caso comprueba con xwininfo que la ventana existe y que su
#      tamaño es el que esa configuración implica. Una raíz rota sale de 160x160
#      y acá se ve.
#   2. El proceso se mata ANTES de bajar el Xvfb (lo hace lib/xvfb-app.sh): si se
#      cae el servidor X primero, Qt desenrolla con los bindings vivos y escupe
#      cientos de TypeError de teardown que parecen bugs de verdad.
set -uo pipefail

repo=${1:?usage: smoke.sh <repo> <kdock-binary>}
kdock=${2:?usage: smoke.sh <repo> <kdock-binary>}
# Absoluta: los symlinks del sandbox se resuelven desde OTRO directorio,
# así que una ruta relativa deja enlaces rotos y el test falla por eso.
repo=$(cd "$repo" && pwd)
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=../lib/sandbox.sh
source "$here/../lib/sandbox.sh"

[ -x "$kdock" ] || kdock_skip "no encuentro el binario: $kdock"
command -v xvfb-run > /dev/null || kdock_skip "xvfb-run no está instalado"
command -v xwininfo > /dev/null || kdock_skip "xwininfo no está instalado (paquete x11-utils)"

SCREEN_W=1920
SCREEN_H=1080

# Patrones que delatan un QML roto. "Property value set multiple times" es el que
# aparece cuando alguien declara un segundo Component.onCompleted: no es error de
# compilación ni lo ve qmllint, pero el archivo entero deja de cargar.
BAD='TypeError|ReferenceError|is not defined|Unable to assign|Binding loop|Property value set multiple times|does not exist on|Cannot read property|QQmlComponent: Component is not ready|error:'

# caso: nombre|claves del .conf|ancho esperado|alto esperado  (-1 = no se mira)
CASES=(
  "panel-abajo|edge=0\ndockLength=100|$SCREEN_W|-1"
  "panel-arriba|edge=1\ndockLength=100|$SCREEN_W|-1"
  "vertical-izq|edge=2\ndockLength=100|-1|$SCREEN_H"
  "flotante|edge=0\ndockLength=0\nalignment=1|-1|-1"
  "etiquetas-2-renglones|edge=0\ndockLength=100\niconLabelMode=1\nlabelLines=2\nlabelBold=true|$SCREEN_W|-1"
  "sin-iconos-de-apps|edge=0\ndockLength=100\nshowAppIcons=false|$SCREEN_W|-1"
  "compacto-dodge|edge=0\ndockLength=100\ncompact=true\nhideMode=2|$SCREEN_W|-1"
  "ventanas-abajo-es|edge=3\ndockLength=100\nhideMode=3\nlanguage=spanish|-1|$SCREEN_H"
)

failures=0

for case in "${CASES[@]}"; do
    IFS='|' read -r name keys want_w want_h <<< "$case"

    kdock_sandbox "kdock-smoke-$name"
    kdock_sandbox_link_apps
    # La copia por pantalla gana: en el primer arranque migrateFirstRun() copia
    # kdock.conf a kdock-<screen>.conf y a partir de ahí lee ESA. Bajo Xvfb la
    # pantalla se llama "screen". Escribir solo una de las dos no se ve.
    printf "[General]\nautohide=false\n%b\n" "$keys" > "$XDG_DATA_HOME/kdock/kdock.conf"
    cp "$XDG_DATA_HOME/kdock/kdock.conf" "$XDG_DATA_HOME/kdock/kdock-screen.conf"

    log="$KDOCK_SANDBOX/stderr.log"
    geom="$KDOCK_SANDBOX/geometry.txt"

    # La ventana buena es la de ("kdock" "kdock"): hay tres que se llaman kdock.
    "$here/../lib/xvfb-app.sh" --log "$log" --settle 6 --screen "${SCREEN_W}x${SCREEN_H}" \
        --out "$geom" \
        --inspect 'xwininfo -root -children | awk "/\"kdock\": \(\"kdock\"/ {print \$0}"' \
        -- "$kdock" || true

    problems=()

    if [ ! -s "$log" ]; then
        : # sin salida, perfecto
    elif grep -qE "$BAD" "$log"; then
        problems+=("errores de QML:")
        while IFS= read -r line; do problems+=("      $line"); done \
            < <(grep -E "$BAD" "$log" | head -5)
    fi

    # 1920x68+0+0 -> ancho 1920, alto 68
    size=$(grep -oE '[0-9]+x[0-9]+\+[0-9-]+\+[0-9-]+' "$geom" 2>/dev/null | head -1)
    if [ -z "$size" ]; then
        problems+=("no apareció la ventana del dock: el QML no llegó a instanciarse")
        # Sin esto, en una máquina ajena (CI) el fallo no dice NADA de la causa:
        # un X que no arranca, un binario que no linkea o una raíz rota se ven
        # exactamente igual desde acá.
        problems+=("últimas líneas de su salida:")
        while IFS= read -r line; do problems+=("      $line"); done \
            < <(tail -8 "$log" 2>/dev/null)
    else
        w=${size%%x*}; rest=${size#*x}; h=${rest%%+*}
        if [ "$w" = "160" ] && [ "$h" = "160" ]; then
            # 160x160 es el tamaño por defecto de una QQuickView cuya raíz no
            # cargó, así que acá SIEMPRE hay una razón en stderr (un módulo QML
            # que falta, el qrc vacío, un error de sintaxis): sin volcarla, en
            # una máquina ajena el fallo no dice nada.
            problems+=("ventana de 160x160: la raíz del QML no cargó. Su salida:")
            while IFS= read -r line; do problems+=("      $line"); done \
                < <(tail -12 "$log" 2>/dev/null)
        fi
        [ "$want_w" != "-1" ] && [ "$w" != "$want_w" ] && \
            problems+=("ancho $w, esperaba $want_w (geometría: $size)")
        [ "$want_h" != "-1" ] && [ "$h" != "$want_h" ] && \
            problems+=("alto $h, esperaba $want_h (geometría: $size)")
    fi

    if [ ${#problems[@]} -eq 0 ]; then
        echo "ok: $name ($size)"
    else
        echo "FAIL: $name"
        printf '    %s\n' "${problems[@]}"
        failures=$((failures + 1))
    fi

    kdock_sandbox_cleanup
done

[ $failures -eq 0 ] || kdock_fail "$failures configuración(es) con problemas"
echo "ok: ${#CASES[@]} configuraciones cargaron el QML y dibujaron"
