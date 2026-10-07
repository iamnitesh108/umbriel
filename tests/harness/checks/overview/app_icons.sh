#!/usr/bin/env bash
# harness: xdg-data=true
# Overview cards show application icons with `app_icons`: a PNG named by the desktop entry named after the app id, and
# an SVG named by the entry whose StartupWMClass matches it. Without `app_icons` no icon is drawn. Typing a sequence
# that leaves a card's label unmatched hides the label and keeps the card's icon. A reload to another `icon_theme`
# switches to that theme's icons and keeps the ones it inherits. An icon installed after an app was found without one
# shows on the next open.
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
readonly MAGENTA='r > 0.9 && g < 0.1 && b > 0.9'
BASELINE=$(< "$UMBRIEL_CONFIG")

mkdir -p "$THEME/48x48/apps" "$THEME/scalable/apps" "$XDG_DATA_HOME/icons/harness/48x48/apps" \
  "$XDG_DATA_HOME/applications"
cat > "$THEME/index.theme" << 'EOF'
[Icon Theme]
Name=Hicolor
Directories=48x48/apps,scalable/apps

[48x48/apps]
Size=48
Context=Applications
Type=Threshold

[scalable/apps]
Size=48
MinSize=16
MaxSize=512
Context=Applications
Type=Scalable
EOF
printf '[Icon Theme]\nName=Harness\nInherits=hicolor\nDirectories=48x48/apps\n\n[48x48/apps]\nSize=48\nContext=Applications\nType=Threshold\n' \
  > "$XDG_DATA_HOME/icons/harness/index.theme"
# Solid 48x48 icons. In hicolor: a green PNG for the entry named after its app id, a blue SVG for the entry matched by
# StartupWMClass. The harness theme replaces the green icon with a yellow one and inherits the blue one.
python3 - "$XDG_DATA_HOME/icons" << 'PY'
import struct, sys, zlib
def chunk(kind, data):
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
for path, pixel in (
    ("hicolor/48x48/apps/harness-green", b"\x00\xff\x00\xff"),
    ("harness/48x48/apps/harness-green", b"\xff\xff\x00\xff"),
):
    rows = b"".join(b"\0" + pixel * 48 for _ in range(48))
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 48, 48, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b"")
    open(f"{sys.argv[1]}/{path}.png", "wb").write(png)
PY
printf '<svg xmlns="http://www.w3.org/2000/svg" width="48" height="48"><rect width="48" height="48" fill="#0000ff"/></svg>\n' \
  > "$THEME/scalable/apps/harness-blue.svg"
printf '[Desktop Entry]\nType=Application\nName=Harness PNG\nIcon=harness-green\n' \
  > "$XDG_DATA_HOME/applications/harness-png.desktop"
printf '[Desktop Entry]\nType=Application\nName=Harness SVG\nIcon=harness-blue\nStartupWMClass=Harness-Class\n' \
  > "$XDG_DATA_HOME/applications/vendor-harness-1234.desktop"

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

"$UMBRIEL" clock-freeze
FILL_COLOR=0xFFFF0000 APP_ID=harness-png "$CLIENT" png 600 600 > /dev/null 2>&1 &
wait_for_count 1
FILL_COLOR=0xFFFF0000 APP_ID=harness-class "$CLIENT" blue 600 600 > /dev/null 2>&1 &
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
"$UMBRIEL" msg overview-close > /dev/null

# Every badge fills magenta, so the magenta drawn shrinks by one label when typing hides it.
write_config $'shortcut_keys = "12"\napp_icons = true\n\n[colors.overview]\nbadge_background = "#FF00FFFF"'
open_overview
labeled=$(count "$MAGENTA")
# Labels are 1, 21 and 22 from the left, so typing 2 leaves the first card's label unmatched.
pointer tap 3
"$UMBRIEL" clock-advance 2000
grim "$IMAGE"
if (($(count "$GREEN") < 400 || $(count "$MAGENTA") >= labeled)); then
  echo "an unmatched label should go and leave its card's icon: green $(count "$GREEN"), magenta $(count "$MAGENTA") of $labeled"
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

"$UMBRIEL" msg overview-close > /dev/null
"$UMBRIEL" clock-advance 2000

# The third window's app had no icon on every open so far; one installed now shows on the next open.
readonly CYAN='g > 0.9 && b > 0.9 && r < 0.1'
if (($(count "$CYAN") != 0)); then
  echo "the app without an icon drew one: cyan $(count "$CYAN")"
  exit 1
fi
python3 - "$XDG_DATA_HOME/icons/hicolor/48x48/apps/harness-none.png" << 'PY'
import struct, sys, zlib
def chunk(kind, data):
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
rows = b"".join(b"\0" + b"\x00\xff\xff\xff" * 48 for _ in range(48))
png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 48, 48, 8, 6, 0, 0, 0))
png += chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b"")
open(sys.argv[1], "wb").write(png)
PY
open_overview
if (($(count "$CYAN") < 400)); then
  echo "an icon installed after its app was first looked up never showed: cyan $(count "$CYAN")"
  exit 1
fi

echo "overview cards draw application icons only with app_icons, keep them past an unmatched label, follow icon_theme, and pick up icons installed later"
