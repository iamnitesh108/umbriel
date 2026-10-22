#!/usr/bin/env bash
# layout.scrolling.wrap_around carries focus and column moves past either end of the strip to the other end. The
# -or-output actions still stop at the ends, and with the option off nothing wraps.
set -euo pipefail

readonly CLIENT="${UMBRIEL_UNMAP_CLIENT:-./build-debug/tests/unmap-client}"

cat >> "$UMBRIEL_CONFIG" <<'EOF'

[layout.scrolling]
wrap_around = true
EOF
"$UMBRIEL" msg config-reload > /dev/null

for title in w1 w2 w3; do
  "$CLIENT" "$title" 300 200 > /dev/null 2>&1 &
  for _ in $(seq 80); do
    [[ $("$UMBRIEL" windows --json | jq --arg t "$title" 'map(select(.title == $t)) | length') == 1 ]] && break
    sleep 0.05
  done
done

active() { "$UMBRIEL" windows --json | jq -r '.[] | select(.active) | .title'; }
order() { "$UMBRIEL" windows --json | jq -r 'sort_by(.x) | map(.title) | join(" ")'; }

expect() {
  "$UMBRIEL" msg "$1" > /dev/null 2>&1 || true
  "$UMBRIEL" settle
  local actual
  actual=$([[ $2 == order:* ]] && order || active)
  if [[ $actual != "${2#order:}" ]]; then
    echo "after $1 expected [${2#order:}], got [$actual]"
    exit 1
  fi
}

expect "window-focus:$("$UMBRIEL" windows --json | jq -r '.[] | select(.title == "w1") | .id')" w1
expect window-focus-left w3
expect window-focus-right w1
expect column-move-left "order:w2 w3 w1"
expect column-move-right "order:w1 w2 w3"
expect window-focus-or-output-left w1

sed -i 's/^wrap_around = true$/wrap_around = false/' "$UMBRIEL_CONFIG"
"$UMBRIEL" msg config-reload > /dev/null
expect window-focus-left w1
expect column-move-left "order:w1 w2 w3"

echo "wrap_around wrapped strip focus and column moves, and left the -or-output actions and the default alone"
