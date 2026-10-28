#!/usr/bin/env bash
# Growing the window below a tab group stops where the group still fits its bar and its tab's minimum height, as it
# stops at a window's minimum: the bar stays in place and the tab never overlaps the window below.
set -euo pipefail

spawn_client() {
  foot --title="$1" sh -c 'sleep 120' > /dev/null 2>&1 &
}

wait_for_windows() {
  for _ in $(seq 60); do
    [[ $("$UMBRIEL" windows --json | jq 'length') == "$1" ]] && return 0
    sleep 0.1
  done
  echo "expected $1 window(s): $("$UMBRIEL" windows --json)"
  return 1
}

spawn_client tab-a
wait_for_windows 1
spawn_client tab-b
wait_for_windows 2
"$UMBRIEL" msg window-consume-left > /dev/null
"$UMBRIEL" msg column-toggle-tabbed > /dev/null
spawn_client below
wait_for_windows 3
"$UMBRIEL" msg window-consume-left > /dev/null
"$UMBRIEL" msg window-move-down > /dev/null
"$UMBRIEL" settle
tab_y=$("$UMBRIEL" windows --json | jq -r '.[] | select(.title == "tab-b") | .y')

for _ in $(seq 10); do
  "$UMBRIEL" msg window-modify-secondary-extent:0.1 > /dev/null
done
"$UMBRIEL" settle

windows=$("$UMBRIEL" windows --json)
if ! jq -e --argjson tab_y "$tab_y" '
  (.[] | select(.title == "tab-b")) as $tab
  | (.[] | select(.title == "below")) as $below
  | $tab.y == $tab_y and $tab.y + $tab.h < $below.y
' <<< "$windows" > /dev/null; then
  echo "expected tab-b to stay at y $tab_y above 'below': $windows"
  exit 1
fi

echo "the group kept room for its bar and its tab"
