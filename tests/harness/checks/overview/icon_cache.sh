#!/usr/bin/env bash
# harness: xdg-data=true
# A decoded application icon stays for the next overview open while its app has a window, so a file changed on disk in
# between is not read again. The app's last window closing drops it, and its next window reads the file afresh.
set -euo pipefail

readonly CLIENT="${UMBRIEL_UNMAP_CLIENT:-./build-debug/tests/unmap-client}"
readonly IMAGE="$UMBRIEL_RUNTIME_DIR/icon-cache.png"
readonly ICONS="$XDG_DATA_HOME/icons/hicolor/48x48/apps"
readonly GREEN='g > 0.9 && r < 0.1 && b < 0.1'
readonly BLUE='b > 0.9 && r < 0.1 && g < 0.1'

mkdir -p "$ICONS"
printf '[Icon Theme]\nName=Hicolor\nDirectories=48x48/apps\n\n[48x48/apps]\nSize=48\nContext=Applications\nType=Threshold\n' \
  > "$XDG_DATA_HOME/icons/hicolor/index.theme"

# Writes a solid 48x48 harness-cache icon of the given RGBA bytes.
write_icon() {
  python3 - "$ICONS/harness-cache.png" "$1" << 'PY'
import struct, sys, zlib
def chunk(kind, data):
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
rows = b"".join(b"\0" + bytes.fromhex(sys.argv[2]) * 48 for _ in range(48))
png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 48, 48, 8, 6, 0, 0, 0))
png += chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b"")
open(sys.argv[1], "wb").write(png)
PY
}

cat >> "$UMBRIEL_CONFIG" << 'EOF'

[animation]
enabled = false

[overview]
shortcuts = false
app_icons = true
EOF
"$UMBRIEL" msg config-reload > /dev/null

window_count() { "$UMBRIEL" windows --json | jq --arg title "$1" '[.[] | select(.title == $title)] | length'; }

spawn() {
  FILL_COLOR=0xFFFF0000 APP_ID=harness-cache "$CLIENT" "$1" 600 400 > "$UMBRIEL_RUNTIME_DIR/$1.log" 2>&1 &
  for _ in $(seq 80); do
    (($(window_count "$1") == 1)) && return 0
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

write_icon 00ff00ff
spawn first
shot
if (($(count "$GREEN") < 400)); then
  echo "the first open drew no icon: green $(count "$GREEN")"
  exit 1
fi

write_icon 0000ffff
shot
if (($(count "$GREEN") < 400 || $(count "$BLUE") != 0)); then
  echo "the next open read the icon again instead of reusing it: green $(count "$GREEN"), blue $(count "$BLUE")"
  exit 1
fi

"$UMBRIEL" msg "window-close:$("$UMBRIEL" windows --json | jq -r '.[] | select(.title == "first") | .id')" > /dev/null
for _ in $(seq 80); do
  (($(window_count first) == 0)) && break
  sleep 0.025
done
spawn second
shot
if (($(count "$BLUE") < 400 || $(count "$GREEN") != 0)); then
  echo "the app's next window kept the dropped icon: blue $(count "$BLUE"), green $(count "$GREEN")"
  exit 1
fi

echo "a drawn icon is reused across opens and dropped with its app's last window"
