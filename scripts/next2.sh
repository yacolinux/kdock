#!/bin/bash
#
# next2.sh
# Avanza el wallpaper (slideshow) SOLO en el monitor del dock que lo lanzó.
# No escribe carpetas: avanza dentro del slideshow que Plasma ya tiene
# configurado para ese monitor (facilities del sistema).
set -u

# El dock exporta KDOCK_SCREEN (conector del dock que lanzó el script). Sin esa
# variable (terminal / atajo global), caemos a la salida activa de KWin.
SCREEN="${KDOCK_SCREEN:-$(qdbus6 org.kde.KWin /KWin org.kde.KWin.activeOutputName 2>/dev/null)}"
echo "Monitor detectado: '${SCREEN:-<vacío>}'"

# Conector -> posición lógica (x y) vía wayland-info. Vacío si no está conectado.
get_pos() {
    local connector="$1" line x y
    line="$(wayland-info 2>/dev/null | grep -A6 -E "^	name: ${connector}$" \
        | grep -oE "x: -?[0-9]+, y: -?[0-9]+" | head -1)"
    [ -z "$line" ] && return 1
    x="$(printf '%s' "$line" | grep -oE "x: -?[0-9]+" | grep -oE -- "-?[0-9]+")"
    y="$(printf '%s' "$line" | grep -oE "y: -?[0-9]+" | grep -oE -- "-?[0-9]+")"
    printf '%s %s' "$x" "$y"
}

# Avanza el slideshow de UN monitor por el ciclo de plugin en DOS llamadas
# separadas (sin eso KDE no repinta). Solo actúa si ya es org.kde.slideshow.
cycle_screen() {
    local t="$1" kind
    kind="$(qdbus6 org.kde.plasmashell /PlasmaShell org.kde.PlasmaShell.evaluateScript "
var t=$t; var ds=desktops(); var done=false; var k='NONE';
for (var j=0;j<ds.length && !done;j++) { var d=ds[j];
  if (d.screen===t) { done=true;
    if (d.wallpaperPlugin==='org.kde.slideshow') { d.wallpaperPlugin='org.kde.image'; k='SLIDE'; }
    else { k='OTHER'; } } }
print(k);")"
    if [ "$kind" = "SLIDE" ]; then
        qdbus6 org.kde.plasmashell /PlasmaShell org.kde.PlasmaShell.evaluateScript "
var t=$t; var ds=desktops();
for (var j=0;j<ds.length;j++) { var d=ds[j];
  if (d.screen===t) { d.wallpaperPlugin='org.kde.slideshow'; d.reloadConfig(); break; } }"
    fi
}

# Índice de pantalla de Plasma cuya geometría arranca en la posición del
# conector (mismo criterio que WallpaperControl). Sin posición -> no hacer nada.
pos="$(get_pos "$SCREEN")"
if [ -n "$pos" ]; then
    set -- $pos
    T="$(qdbus6 org.kde.plasmashell /PlasmaShell org.kde.PlasmaShell.evaluateScript "
var t=-1;for(var i=0;i<screenCount;i++){var g=screenGeometry(i);if(g.x===$1&&g.y===$2){t=i;break;}}
print(t);")"
else
    T=""
fi

if [ -n "$T" ] && [ "$T" != "-1" ]; then
    cycle_screen "$T"
else
    echo "-> Monitor desconocido o desconectado; no se hizo nada."
fi
