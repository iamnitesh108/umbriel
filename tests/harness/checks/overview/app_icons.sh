#!/usr/bin/env bash
# harness: xdg-data=true
# Overview badges show application icons with `app_icons`: one a desktop entry names and one found by app id, both PNGs
# in the hicolor theme. Without it no icon is drawn. Beside a label, an icon stays when a typed sequence leaves the
# label unmatched, and only the label goes. A reload to another `icon_theme` switches to that theme's icons and keeps
# the ones it inherits.
set -euo pipefail

readonly OUTPUT_W=1280
readonly OUTPUT_H=720
readonly POINTER="${UMBRIEL_POINTER_CLIENT:-./build-debug/tests/pointer-client}"
readonly CLIENT="${UMBRIEL_UNMAP_CLIENT:-./build-debug/tests/unmap-client}"
readonly IMAGE="$UMBRIEL_RUNTIME_DIR/app-icons.png"
readonly THEME="$XDG_DATA_HOME/icons/hicolor"
readonly GREEN='g > 0.9 && r < 0.1 && b < 0.1'
readonly BLUE='b > 0.9 && r < 0.1 && g < 0.1'
readonly YELLOW='r > 0.9 && g > 0.9 && b < 0.1'
readonly NOT_RED='!(r > 0.9 && g < 0.1 && b < 0.1)'
BASELINE=$(< "$UMBRIEL_CONFIG")

mkdir -p "$THEME/48x48/apps" "$XDG_DATA_HOME/icons/harness/48x48/apps" "$XDG_DATA_HOME/applications"
printf '[Icon Theme]\nName=Hicolor\nDirectories=48x48/apps\n\n[48x48/apps]\nSize=48\nContext=Applications\nType=Threshold\n' \
  > "$THEME/index.theme"
printf '[Icon Theme]\nName=Harness\nInherits=hicolor\nDirectories=48x48/apps\n\n[48x48/apps]\nSize=48\nContext=Applications\nType=Threshold\n' \
  > "$XDG_DATA_HOME/icons/harness/index.theme"
# Solid 48x48 PNGs. In hicolor: green for the desktop entry's icon, blue for the one named after its app id. The
# harness theme replaces the green icon with a yellow one and inherits the blue one.
python3 - "$XDG_DATA_HOME/icons" << 'PY'
import struct, sys, zlib
def chunk(kind, data):
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
for path, pixel in (
    ("hicolor/48x48/apps/harness-green", b"\x00\xff\x00\xff"),
    ("hicolor/48x48/apps/harness-blue", b"\x00\x00\xff\xff"),
    ("harness/48x48/apps/harness-green", b"\xff\xff\x00\xff"),
):
    rows = b"".join(b"\0" + pixel * 48 for _ in range(48))
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 48, 48, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b"")
    open(f"{sys.argv[1]}/{path}.png", "wb").write(png)
PY
printf '[Desktop Entry]\nType=Application\nName=Harness PNG\nIcon=harness-green\n' \
  > "$XDG_DATA_HOME/applications/harness-png.desktop"

pointer() {
  "$POINTER" "$OUTPUT_W" "$OUTPUT_H" "$@"
}

wait_for_count() {
  local want=$1
  for _ in $(seq 60); do
    [[ $("$UMBRIEL" windows --json | jq 'length') -eq $want ]] && return 0
    sleep 0.1
  done
  echo "expected $want windows, got: $("$UMBRIEL" windows --json)"
  return 1
}

write_config() {
  printf '%s\n\n[overview]\n%s\n' "$BASELINE" "$1" > "$UMBRIEL_CONFIG"
  "$UMBRIEL" msg config-reload > /dev/null
  "$UMBRIEL" clock-advance 2000
}

open_overview() {
  "$UMBRIEL" msg overview-open > /dev/null
  "$UMBRIEL" clock-advance 2000
  grim "$IMAGE"
}

count() {
  "$UMBRIEL_PIXEL_PROBE" "$IMAGE" count "$1"
}

# Width of the badge around the green icon: everything not red in the icon's rows, from the badge edge rightwards.
green_badge_width() {
  local x y w h
  read -r x y w h < <("$UMBRIEL_PIXEL_PROBE" "$IMAGE" bbox "$GREEN")
  read -r _ _ w _ < <("$UMBRIEL_PIXEL_PROBE" "$IMAGE" bbox "$NOT_RED" "200x${h}+$((x - 4))+${y}")
  echo "$w"
}

"$UMBRIEL" clock-freeze
FILL_COLOR=0xFFFF0000 APP_ID=harness-png "$CLIENT" png 600 600 > /dev/null 2>&1 &
wait_for_count 1
FILL_COLOR=0xFFFF0000 APP_ID=harness-blue "$CLIENT" blue 600 600 > /dev/null 2>&1 &
wait_for_count 2
FILL_COLOR=0xFFFF0000 APP_ID=harness-none "$CLIENT" none 600 600 > /dev/null 2>&1 &
wait_for_count 3
"$UMBRIEL" msg "window-focus:$("$UMBRIEL" windows --json | jq -r '.[] | select(.title == "png") | .id')" > /dev/null
"$UMBRIEL" clock-advance 2000

write_config 'shortcut_keys = "12"'
open_overview
if (($(count "$GREEN") != 0 || $(count "$BLUE") != 0)); then
  echo "icons were drawn without app_icons: green $(count "$GREEN"), blue $(count "$BLUE")"
  exit 1
fi
"$UMBRIEL" msg overview-close > /dev/null

write_config $'shortcuts = false\napp_icons = true'
open_overview
green=$(count "$GREEN")
blue=$(count "$BLUE")
if ((green < 400 || blue < 400)); then
  echo "expected two icons of about 24x24 pixels, got green $green, blue $blue"
  exit 1
fi
icon_only=$(green_badge_width)
"$UMBRIEL" msg overview-close > /dev/null

write_config $'shortcut_keys = "12"\napp_icons = true'
open_overview
labeled=$(green_badge_width)
if ((labeled <= icon_only)); then
  echo "the labeled badge ($labeled px) is not wider than the icon-only one ($icon_only px)"
  exit 1
fi
# Labels are 1, 21 and 22 from the left, so typing 2 leaves the first card's label unmatched.
pointer tap 3
"$UMBRIEL" clock-advance 2000
grim "$IMAGE"
unmatched=$(green_badge_width)
if (($(count "$GREEN") < 400 || unmatched != icon_only)); then
  echo "an unmatched label should leave only its icon: green $(count "$GREEN"), badge $unmatched px, icon-only $icon_only px"
  exit 1
fi

"$UMBRIEL" msg overview-close > /dev/null

# Icons resolved in hicolor give way to the new theme's after a reload.
write_config $'shortcuts = false\napp_icons = true\nicon_theme = "harness"'
open_overview
if (($(count "$YELLOW") < 400 || $(count "$GREEN") != 0 || $(count "$BLUE") < 400)); then
  echo "icon_theme should replace the green icon and inherit the blue one: yellow $(count "$YELLOW"), green $(count "$GREEN"), blue $(count "$BLUE")"
  exit 1
fi

echo "overview badges draw application icons only with app_icons, keep them past an unmatched label, and follow icon_theme"
