#!/usr/bin/env bash
# harness: xdg-data=true
# Overview badges take their icon size from `icon_size` and sit at `badge_position` as fractions of the room inside the
# card: [0, 0] hugs the top-left margin, [1, 1] the bottom-right one, [0.5, 0.5] centers. A badge whose position falls
# past the output edge slides back inside it.
set -euo pipefail

readonly CLIENT="${UMBRIEL_UNMAP_CLIENT:-./build-debug/tests/unmap-client}"
readonly IMAGE="$UMBRIEL_RUNTIME_DIR/badge-placement.png"
readonly THEME="$XDG_DATA_HOME/icons/hicolor"
readonly OUTPUT_W=1280
readonly ICON=48
# The badge's distance from the card edge to its icon: the card margin plus the icon inset inside the badge.
readonly EDGE=10
readonly GREEN='g > 0.9 && r < 0.1 && b < 0.1'
readonly RED='r > 0.9 && g < 0.1 && b < 0.1'
BASELINE=$(< "$UMBRIEL_CONFIG")

mkdir -p "$THEME/48x48/apps"
printf '[Icon Theme]\nName=Hicolor\nDirectories=48x48/apps\n\n[48x48/apps]\nSize=48\nContext=Applications\nType=Threshold\n' \
  > "$THEME/index.theme"
python3 - "$THEME/48x48/apps/harness-green.png" << 'PY'
import struct, sys, zlib
def chunk(kind, data):
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
rows = b"".join(b"\0" + b"\x00\xff\x00\xff" * 48 for _ in range(48))
png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 48, 48, 8, 6, 0, 0, 0))
png += chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b"")
open(sys.argv[1], "wb").write(png)
PY

wait_for_count() {
  local want=$1
  for _ in $(seq 60); do
    [[ $("$UMBRIEL" windows --json | jq 'length') -eq $want ]] && return 0
    sleep 0.1
  done
  echo "expected $want windows, got: $("$UMBRIEL" windows --json)"
  return 1
}

focus_title() {
  "$UMBRIEL" msg "window-focus:$("$UMBRIEL" windows --json | jq -r --arg title "$1" '.[] | select(.title == $title) | .id')" \
    > /dev/null
}

# Reloads with `zoom` and `badge_position`, then opens the overview and captures it.
capture() {
  printf '%s\n\n[appearance]\nborder_width = 0\nouter_border_width = 0\ncorner_radius = 0\n\n[layout.scrolling]\ndefault_extent_fraction = 0.5\n\n[overview]\nzoom = %s\nshortcuts = false\napp_icons = true\nicon_size = %d\nbadge_position = %s\n' \
    "$BASELINE" "$1" "$ICON" "$2" > "$UMBRIEL_CONFIG"
  "$UMBRIEL" msg config-reload > /dev/null
  "$UMBRIEL" clock-advance 2000
  "$UMBRIEL" msg overview-open > /dev/null
  "$UMBRIEL" clock-advance 2000
  grim "$IMAGE"
  read -r CARD_X CARD_Y CARD_W CARD_H < <("$UMBRIEL_PIXEL_PROBE" "$IMAGE" bbox "$RED")
  read -r ICON_X ICON_Y ICON_W ICON_H < <("$UMBRIEL_PIXEL_PROBE" "$IMAGE" bbox "$GREEN")
}

expect() {
  local what=$1 got=$2 want=$3
  if ((got < want - 1 || got > want + 1)); then
    echo "$what: got $got, want $want (card ${CARD_X},${CARD_Y} ${CARD_W}x${CARD_H}, icon ${ICON_X},${ICON_Y} ${ICON_W}x${ICON_H})"
    exit 1
  fi
}

"$UMBRIEL" clock-freeze
# Only the right-hand window is red and has an icon, so the red bounding box is its card.
FILL_COLOR=0xFF808080 "$CLIENT" left 600 600 > /dev/null 2>&1 &
wait_for_count 1
FILL_COLOR=0xFF808080 "$CLIENT" middle 600 600 > /dev/null 2>&1 &
wait_for_count 2
FILL_COLOR=0xFFFF0000 APP_ID=harness-green "$CLIENT" right 600 600 > /dev/null 2>&1 &
wait_for_count 3
focus_title right
"$UMBRIEL" clock-advance 2000

capture 0.5 '[0, 0]'
expect "icon width" "$ICON_W" "$ICON"
expect "icon height" "$ICON_H" "$ICON"
expect "top-left icon x" "$ICON_X" $((CARD_X + EDGE))
expect "top-left icon y" "$ICON_Y" $((CARD_Y + EDGE))
"$UMBRIEL" msg overview-close > /dev/null

capture 0.5 '[1, 1]'
expect "bottom-right icon right edge" $((ICON_X + ICON_W)) $((CARD_X + CARD_W - EDGE))
expect "bottom-right icon bottom edge" $((ICON_Y + ICON_H)) $((CARD_Y + CARD_H - EDGE))
"$UMBRIEL" msg overview-close > /dev/null

capture 0.5 '[0.5, 0.5]'
expect "centered icon x" $((2 * ICON_X + ICON_W)) $((2 * CARD_X + CARD_W))
expect "centered icon y" $((2 * ICON_Y + ICON_H)) $((2 * CARD_Y + CARD_H))
"$UMBRIEL" msg overview-close > /dev/null

# Focusing the left window pushes the right card past the output edge; its top-right badge slides back on screen.
focus_title left
"$UMBRIEL" clock-advance 2000
capture 0.75 '[1, 0]'
if ((CARD_X + CARD_W != OUTPUT_W)); then
  echo "expected the red card to run past the output edge, got ${CARD_X},${CARD_Y} ${CARD_W}x${CARD_H}"
  exit 1
fi
expect "clipped icon width" "$ICON_W" "$ICON"
expect "clipped icon right edge" $((ICON_X + ICON_W)) $((OUTPUT_W - EDGE))
expect "clipped icon y" "$ICON_Y" $((CARD_Y + EDGE))

echo "badges follow icon_size and badge_position, and stay on screen when the card does not"
