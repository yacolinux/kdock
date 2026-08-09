#!/bin/bash
# Generates the fake system tools the suite puts first in $PATH.
#
# This is a safety device, not a convenience: the kdock backends drive real
# hardware and a real desktop through these commands. BrightnessControl reapplies
# the stored brightness on startup, AppearanceControl shells out to
# plasma-apply-colorscheme / plasma-changeicons (which also notify over D-Bus, so
# isolating XDG_CONFIG_HOME is NOT enough), and VolumeControl talks to pactl.
# Without this directory a test run changes the screen brightness and the desktop
# theme of whoever is running it.
#
# Every fake logs its arguments to calls.log, which is also what makes them
# useful as assertions: a test can check *which* id a tool was asked to apply.
#
# Usage: fakebin.sh <dir>
set -euo pipefail

dir=${1:?usage: fakebin.sh <dir>}
mkdir -p "$dir"
: > "$dir/calls.log"

# Tools that only need to swallow the call and be logged.
for tool in plasma-apply-colorscheme plasma-changeicons kwriteconfig6 kreadconfig6 \
            brightnessctl wpctl qdbus6 kbuildsycoca6 xdg-open; do
    cat > "$dir/$tool" <<EOF
#!/bin/sh
echo "\$(basename "\$0") <- \$*" >> "$dir/calls.log"
exit 0
EOF
    chmod +x "$dir/$tool"
done

# pactl needs a little more: AudioControl parses its output, and `pactl
# subscribe` is a long-lived child the dock respawns with backoff when it dies.
# Emitting nothing and blocking is the honest stand-in for "a server with no
# sinks"; exiting instead would exercise the respawn loop on every test.
cat > "$dir/pactl" <<EOF
#!/bin/sh
echo "pactl <- \$*" >> "$dir/calls.log"
case "\$1" in
    subscribe) exec sleep 86400 ;;
    *) exit 0 ;;
esac
EOF
chmod +x "$dir/pactl"

touch "$dir/.stamp"
