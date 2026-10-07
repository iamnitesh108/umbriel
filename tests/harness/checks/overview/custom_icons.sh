#!/usr/bin/env bash
# harness: xdg-data=true
# A window rule's `overview_icon` replaces the application's own icon, by icon name or by a path under ~/, and one
# that cannot be found leaves the application's own. `fallback_icon` stands in for an application without an icon or
# with one that does not decode, until one is installed.
set -euo pipefail

readonly CLIENT="${UMBRIEL_UNMAP_CLIENT:-./build-debug/tests/unmap-client}"
readonly IMAGE="$UMBRIEL_RUNTIME_DIR/custom-icons.png"
readonly ICONS="$XDG_DATA_HOME/icons/hicolor/48x48/apps"
readonly GREEN='g > 0.9 && r < 0.1 && b < 0.1'
readonly MAGENTA='r > 0.9 && g < 0.1 && b > 0.9'
readonly YELLOW='r > 0.9 && g > 0.9 && b < 0.1'
readonly BLUE='b > 0.9 && r < 0.1 && g < 0.1'
readonly CYAN='g > 0.9 && b > 0.9 && r < 0.1'
readonly WHITE='r > 0.9 && g > 0.9 && b > 0.9'

mkdir -p "$ICONS"
printf '[Icon Theme]\nName=Hicolor\nDirectories=48x48/apps\n\n[48x48/apps]\nSize=48\nContext=Applications\nType=Threshold\n' \
  > "$XDG_DATA_HOME/icons/hicolor/index.theme"

# Writes a solid 48x48 PNG of the given RGBA bytes.
write_icon() {
  python3 - "$1" "$2" << 'PY'
import struct, sys, zlib
def chunk(kind, data):
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
rows = b"".join(b"\0" + bytes.fromhex(sys.argv[2]) * 48 for _ in range(48))
png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 48, 48, 8, 6, 0, 0, 0))
png += chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b"")
open(sys.argv[1], "wb").write(png)
PY
}

write_icon "$ICONS/harness-own.png" 00ff00ff
write_icon "$ICONS/harness-custom.png" ff00ffff
write_icon "$HOME/custom.png" ffff00ff
write_icon "$ICONS/harness-kept.png" 0000ffff
write_icon "$ICONS/harness-fallback.png" 00ffffff
printf 'not a png\n' > "$ICONS/harness-broken.png"

cat >> "$UMBRIEL_CONFIG" << 'EOF'

[animation]
enabled = false

[layout.scrolling]
default_extent_fraction = 0.25

[overview]
shortcuts = false
app_icons = true
fallback_icon = "harness-fallback"

[[window_rule]]
match.app_id = "^harness-own$"
overview_icon = "harness-custom"

[[window_rule]]
match.app_id = "^harness-path$"
overview_icon = "~/custom.png"

[[window_rule]]
match.app_id = "^harness-kept$"
overview_icon = "harness-missing"
EOF
"$UMBRIEL" msg config-reload > /dev/null

window_of() { "$UMBRIEL" windows --json | jq -c --arg title "$1" '.[] | select(.title == $title)'; }

spawn() {
  FILL_COLOR=0xFFFF0000 APP_ID="$2" "$CLIENT" "$1" 300 400 > "$UMBRIEL_RUNTIME_DIR/$1.log" 2>&1 &
  for _ in $(seq 80); do
    [[ -n "$(window_of "$1")" ]] && return 0
    sleep 0.025
  done
  echo "window '$1' never appeared: $(cat "$UMBRIEL_RUNTIME_DIR/$1.log")"
  return 1
}

# Opens the overview, captures it, and closes it again.
shot() {
  "$UMBRIEL" msg overview-open > /dev/null
  "$UMBRIEL" settle
  grim "$IMAGE"
  "$UMBRIEL" msg overview-close > /dev/null
  "$UMBRIEL" settle
}

count() { "$UMBRIEL_PIXEL_PROBE" "$IMAGE" count "$1"; }

spawn own harness-own
spawn path harness-path
spawn kept harness-kept
spawn bare harness-bare
spawn broken harness-broken
"$UMBRIEL" settle
shot

if (($(count "$MAGENTA") < 400 || $(count "$GREEN") != 0)); then
  echo "a rule's icon name should replace the app's own: magenta $(count "$MAGENTA"), green $(count "$GREEN")"
  exit 1
fi
if (($(count "$YELLOW") < 400)); then
  echo "a rule's ~/ path drew no icon: yellow $(count "$YELLOW")"
  exit 1
fi
if (($(count "$BLUE") < 400)); then
  echo "a rule naming a missing icon should leave the app's own: blue $(count "$BLUE")"
  exit 1
fi
if (($(count "$CYAN") < 2 * 400)); then
  echo "fallback_icon should stand in for the app without an icon and the one whose icon does not decode: cyan $(count "$CYAN")"
  exit 1
fi

mkdir -p "$XDG_DATA_HOME/applications"
write_icon "$ICONS/harness-bare.png" ffffffff
printf '[Desktop Entry]\nType=Application\nName=Harness Bare\nIcon=harness-bare\n' \
  > "$XDG_DATA_HOME/applications/harness-bare.desktop"
shot
if (($(count "$WHITE") < 400 || $(count "$CYAN") >= 2 * 400)); then
  echo "an icon installed later should replace the fallback: white $(count "$WHITE"), cyan $(count "$CYAN")"
  exit 1
fi

echo "window rules replace application icons, a missing one keeps the app's own, and the fallback gives way to a real icon"
