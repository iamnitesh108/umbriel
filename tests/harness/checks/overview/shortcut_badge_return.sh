#!/usr/bin/env bash
# A shortcut badge hidden while a typed sequence leaves its label unmatched comes back when the sequence is cleared,
# here by a window mapping during the overview, even though its label stays the same.
set -euo pipefail

readonly POINTER="${UMBRIEL_POINTER_CLIENT:-./build-debug/tests/pointer-client}"
readonly CLIENT="${UMBRIEL_UNMAP_CLIENT:-./build-debug/tests/unmap-client}"
readonly IMAGE="$UMBRIEL_RUNTIME_DIR/shortcut-badge-return.png"
readonly RED='r > 0.9 && g < 0.1 && b < 0.1'
readonly MAGENTA='r > 0.9 && g < 0.1 && b > 0.9'

wait_for_count() {
  local want=$1
  for _ in $(seq 60); do
    [[ $("$UMBRIEL" windows --json | jq 'length') -eq $want ]] && return 0
    sleep 0.1
  done
  echo "expected $want windows, got: $("$UMBRIEL" windows --json)"
  return 1
}

# Magenta badge fill inside the red card, the one labeled 1.
first_badge() {
  grim "$IMAGE"
  local x y w h
  read -r x y w h < <("$UMBRIEL_PIXEL_PROBE" "$IMAGE" bbox "$RED")
  "$UMBRIEL_PIXEL_PROBE" "$IMAGE" count "$MAGENTA" "${w}x${h}+${x}+${y}"
}

cat >> "$UMBRIEL_CONFIG" << 'EOF'

[overview]
shortcut_keys = "12"

[colors.overview]
badge_background = "#FF00FFFF"
EOF
"$UMBRIEL" msg config-reload > /dev/null

"$UMBRIEL" clock-freeze
FILL_COLOR=0xFFFF0000 "$CLIENT" first 600 600 > /dev/null 2>&1 &
wait_for_count 1
FILL_COLOR=0xFF808080 "$CLIENT" second 600 600 > /dev/null 2>&1 &
wait_for_count 2
FILL_COLOR=0xFF808080 "$CLIENT" third 600 600 > /dev/null 2>&1 &
wait_for_count 3
"$UMBRIEL" msg "window-focus:$("$UMBRIEL" windows --json | jq -r '.[] | select(.title == "first") | .id')" > /dev/null
"$UMBRIEL" clock-advance 2000
"$UMBRIEL" msg overview-open > /dev/null
"$UMBRIEL" clock-advance 2000
if (($(first_badge) == 0)); then
  echo "the first card has no shortcut badge to begin with"
  exit 1
fi

# Labels are 1, 21 and 22 from the left, so typing 2 leaves the first card's label unmatched.
"$POINTER" 1280 720 tap 3
"$UMBRIEL" clock-advance 2000
if (($(first_badge) != 0)); then
  echo "the unmatched first card still shows its badge"
  exit 1
fi

FILL_COLOR=0xFF808080 "$CLIENT" fourth 600 600 > /dev/null 2>&1 &
wait_for_count 4
"$UMBRIEL" clock-advance 2000
if (($(first_badge) == 0)); then
  echo "the first card's badge did not come back after a window mapped and cleared the typed sequence"
  exit 1
fi

echo "a badge hidden by an unmatched sequence returns once the sequence clears"
