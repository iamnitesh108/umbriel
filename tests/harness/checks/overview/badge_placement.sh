#!/usr/bin/env bash
# harness: xdg-data=true
# Overview icon badges take their size from `icon_size` and sit at `icon_position` as fractions of the room inside the
# card: [0, 0] hugs the top-left margin, [1, 1] the bottom-right one, [0.5, 0.5] centers. A badge whose position falls
# past the output edge slides back inside it. The fill behind it takes `colors.overview.badge_background` scaled by
# `badge_background_opacity`, and disappears at 0. The shortcut label is placed and sized on its own, by
# `shortcut_position` and `shortcut_size`.
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

# Reloads with `zoom`, `icon_position`, and optional extra config appended after [overview], then opens the overview
# and captures it.
capture() {
  printf '%s\n\n[appearance]\nborder_width = 0\nouter_border_width = 0\ncorner_radius = 0\n\n[layout.scrolling]\ndefault_extent_fraction = 0.5\n\n[overview]\nzoom = %s\nshortcuts = false\napp_icons = true\nicon_size = %d\nicon_position = %s\n%s\n' \
    "$BASELINE" "$1" "$ICON" "$2" "${3:-}" > "$UMBRIEL_CONFIG"
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

# The badge fill is the ring between the icon and the badge edge, inset 4 pixels on each side.
readonly RING=$(((ICON + 8) * (ICON + 8) - ICON * ICON))
ring_count() {
  "$UMBRIEL_PIXEL_PROBE" "$IMAGE" count "$1" "$((ICON + 8))x$((ICON + 8))+$((ICON_X - 4))+$((ICON_Y - 4))"
}
capture 0.5 '[0, 0]' $'\n[colors.overview]\nbadge_background = "#FF00FFFF"'
expect "opaque fill" "$(ring_count 'r > 0.9 && g < 0.1 && b > 0.9')" "$RING"
"$UMBRIEL" msg overview-close > /dev/null
capture 0.5 '[0, 0]' $'badge_background_opacity = 0.5\n\n[colors.overview]\nbadge_background = "#FF00FFFF"'
expect "half-opaque fill over the red card" "$(ring_count 'r > 0.9 && g < 0.1 && b > 0.4 && b < 0.6')" "$RING"
"$UMBRIEL" msg overview-close > /dev/null
capture 0.5 '[0, 0]' 'badge_background_opacity = 0'
expect "transparent fill" "$(ring_count "!($RED) && !($GREEN)")" 0
expect "icon width without fill" "$ICON_W" "$ICON"
"$UMBRIEL" msg overview-close > /dev/null

capture 0.5 '[1, 1]'
expect "bottom-right icon right edge" $((ICON_X + ICON_W)) $((CARD_X + CARD_W - EDGE))
expect "bottom-right icon bottom edge" $((ICON_Y + ICON_H)) $((CARD_Y + CARD_H - EDGE))
"$UMBRIEL" msg overview-close > /dev/null

capture 0.5 '[0.5, 0.5]'
expect "centered icon x" $((2 * ICON_X + ICON_W)) $((2 * CARD_X + CARD_W))
expect "centered icon y" $((2 * ICON_Y + ICON_H)) $((2 * CARD_Y + CARD_H))
"$UMBRIEL" msg overview-close > /dev/null

# The shortcut label is a badge of its own: with a magenta fill, its box on the red card is the magenta inside it.
capture_label() {
  printf '%s\n\n[appearance]\nborder_width = 0\nouter_border_width = 0\ncorner_radius = 0\n\n[layout.scrolling]\ndefault_extent_fraction = 0.5\n\n[overview]\nzoom = 0.5\nshortcut_size = %d\nshortcut_position = [1, 1]\n\n[colors.overview]\nbadge_background = "#FF00FFFF"\n' \
    "$BASELINE" "$1" > "$UMBRIEL_CONFIG"
  "$UMBRIEL" msg config-reload > /dev/null
  "$UMBRIEL" clock-advance 2000
  "$UMBRIEL" msg overview-open > /dev/null
  "$UMBRIEL" clock-advance 2000
  grim "$IMAGE"
  read -r CARD_X CARD_Y CARD_W CARD_H < <("$UMBRIEL_PIXEL_PROBE" "$IMAGE" bbox "$RED")
  read -r LABEL_X LABEL_Y LABEL_W LABEL_H < <(
    "$UMBRIEL_PIXEL_PROBE" "$IMAGE" bbox 'r > 0.9 && g < 0.1 && b > 0.9' "${CARD_W}x${CARD_H}+${CARD_X}+${CARD_Y}"
  )
  "$UMBRIEL" msg overview-close > /dev/null
}
capture_label 19
expect "bottom-right label right edge" $((LABEL_X + LABEL_W)) $((CARD_X + CARD_W - 6))
expect "bottom-right label bottom edge" $((LABEL_Y + LABEL_H)) $((CARD_Y + CARD_H - 6))
small_label=$LABEL_H
capture_label 38
if ((LABEL_H < small_label * 3 / 2)); then
  echo "shortcut_size 38 drew a ${LABEL_H}px label, not clearly taller than the ${small_label}px one at 19"
  exit 1
fi

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

echo "icon and shortcut badges follow their own position and size and the fill color and opacity, and stay on screen"
