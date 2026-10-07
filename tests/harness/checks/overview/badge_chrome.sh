#!/usr/bin/env bash
# harness: xdg-data=true
# Overview badges place against the card as it shows: a tab group's icon measures from the top of its tab bar, not
# from the content below it. A short card shrinks a badge that does not fit, while a tiny card, which the shrunken
# badge would mostly cover, hides it.
set -euo pipefail

readonly CLIENT="${UMBRIEL_UNMAP_CLIENT:-./build-debug/tests/unmap-client}"
readonly IMAGE="$UMBRIEL_RUNTIME_DIR/badge-chrome.png"
readonly THEME="$XDG_DATA_HOME/icons/hicolor"
readonly ICON=24
# The badge's distance from the card edge to its icon: the card margin plus the icon inset inside the badge.
readonly EDGE=10
readonly MAGENTA='r > 0.9 && g < 0.1 && b > 0.9'
readonly GREEN='g > 0.9 && r < 0.1 && b < 0.1'
readonly YELLOW='r > 0.9 && g > 0.9 && b < 0.1'
readonly CYAN='g > 0.9 && b > 0.9 && r < 0.1'

mkdir -p "$THEME/48x48/apps"
printf '[Icon Theme]\nName=Hicolor\nDirectories=48x48/apps\n\n[48x48/apps]\nSize=48\nContext=Applications\nType=Threshold\n' \
  > "$THEME/index.theme"
python3 - "$THEME/48x48/apps" << 'PY'
import struct, sys, zlib
def chunk(kind, data):
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
for name, pixel in (
    ("harness-tabbed", b"\x00\xff\x00\xff"),
    ("harness-small", b"\xff\xff\x00\xff"),
    ("harness-tiny", b"\x00\xff\xff\xff"),
):
    rows = b"".join(b"\0" + pixel * 48 for _ in range(48))
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 48, 48, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b"")
    open(f"{sys.argv[1]}/{name}.png", "wb").write(png)
PY

# The tab bar is all magenta, so its bounding box is the bar.
cat >> "$UMBRIEL_CONFIG" << 'EOF'

[animation]
enabled = false

[appearance]
border_width = 0
outer_border_width = 0
corner_radius = 0

[appearance.shadow]
enabled = false

[appearance.tab_bar]
height = 24

[colors.tab_bar]
background = "#FF00FFFF"
active = "#FF00FFFF"
active_unfocused = "#FF00FFFF"
text = "#FF00FFFF"
active_text = "#FF00FFFF"
active_unfocused_text = "#FF00FFFF"

[overview]
zoom = 0.5
background_blur = false
shortcuts = false
app_icons = true
icon_position = [0, 0]

[[window_rule]]
match.title = "^small$"
default_floating = true
default_floating_size_px = { width = 600, height = 60 }
default_position = { x = 300, y = 600, anchor = "top_left" }

[[window_rule]]
match.title = "^tiny$"
default_floating = true
default_floating_size_px = { width = 64, height = 64 }
default_position = { x = 1100, y = 100, anchor = "top_left" }
EOF
"$UMBRIEL" msg config-reload > /dev/null

window_of() { "$UMBRIEL" windows --json | jq -c --arg title "$1" '.[] | select(.title == $title)'; }

spawn() {
  local title=$1 app=$2 width=${3:-600} height=${4:-400}
  FILL_COLOR=0xFFFF0000 APP_ID="$app" "$CLIENT" "$title" "$width" "$height" > "$UMBRIEL_RUNTIME_DIR/$title.log" 2>&1 &
  for _ in $(seq 80); do
    [[ -n "$(window_of "$title")" ]] && return 0
    sleep 0.025
  done
  echo "window '$title' never appeared: $(cat "$UMBRIEL_RUNTIME_DIR/$title.log")"
  return 1
}

spawn tab-a harness-tabbed
spawn tab-b harness-tabbed
"$UMBRIEL" msg window-consume-left > /dev/null
"$UMBRIEL" msg column-toggle-tabbed > /dev/null
spawn small harness-small 600 60
spawn tiny harness-tiny 64 64
"$UMBRIEL" settle
"$UMBRIEL" msg overview-open > /dev/null
"$UMBRIEL" settle
grim "$IMAGE"

read -r bar_x bar_y bar_w bar_h < <("$UMBRIEL_PIXEL_PROBE" "$IMAGE" bbox "$MAGENTA")
read -r icon_x icon_y icon_w icon_h < <("$UMBRIEL_PIXEL_PROBE" "$IMAGE" bbox "$GREEN")
if ((bar_w == 0 || icon_w == 0)); then
  echo "expected a tab bar and an icon on the tab group's card: bar ${bar_w}x${bar_h}, icon ${icon_w}x${icon_h}"
  exit 1
fi
if ((icon_y < bar_y + EDGE - 1 || icon_y > bar_y + EDGE + 1 || icon_x < bar_x + EDGE - 1 || icon_x > bar_x + EDGE + 1)); then
  echo "the tab group's icon is at ${icon_x},${icon_y}, want ${EDGE}px inside its tab bar's corner at ${bar_x},${bar_y}"
  exit 1
fi

read -r _ _ small_w small_h < <("$UMBRIEL_PIXEL_PROBE" "$IMAGE" bbox "$YELLOW")
if ((small_w == 0 || small_h == 0 || small_h >= ICON)); then
  echo "the small card's icon should shrink below ${ICON}px and stay visible, got ${small_w}x${small_h}"
  exit 1
fi

if (($("$UMBRIEL_PIXEL_PROBE" "$IMAGE" count "$CYAN") != 0)); then
  echo "the tiny card's icon would cover most of its window and should stay hidden"
  exit 1
fi

echo "badges measure from the top of a tab bar, shrink on a short card, and stay hidden on a tiny one"
