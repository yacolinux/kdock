#!/bin/bash
#
# next-all.sh
# Avanza el wallpaper (slideshow) de TODOS los monitores conectados.
# No escribe carpetas: avanza dentro del slideshow que Plasma ya tiene
# configurado para cada monitor (facilities del sistema).
set -u

echo "Avanzando el slideshow de todos los monitores conectados"

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

# screenCount ya cuenta solo las pantallas conectadas; los containments con
# screen=-1 (monitores desconectados) quedan fuera del rango.
N="$(qdbus6 org.kde.plasmashell /PlasmaShell org.kde.PlasmaShell.evaluateScript "print(screenCount);")"
N="${N:-0}"
for ((s = 0; s < N; s++)); do
    cycle_screen "$s"
done
