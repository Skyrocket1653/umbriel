#!/usr/bin/env bash
# Regression for #558: previous/next corners must work with a fullscreen target in overview, both without shell
# chrome and with an exclusive-keyboard layer. The fullscreen desktop must still suppress the same corners.
set -euo pipefail

readonly POINTER="${UMBRIEL_POINTER_CLIENT:-./build-debug/tests/pointer-client}"
readonly CLIENT="${UMBRIEL_UNMAP_CLIENT:-./build-debug/tests/unmap-client}"
readonly LAYER="${UMBRIEL_LAYER_CLIENT:-./build-debug/tests/layer-client}"
pointer() { "$POINTER" 1280 720 "$@"; }
wait_window() {
  local title=$1 query=$2
  for _ in $(seq 80); do
    if "$UMBRIEL" windows --json | jq -e --arg title "$title" \
      "any(.[]; .title == \$title and ($query))" > /dev/null; then
      return 0
    fi
    sleep 0.05
  done
  echo "window $title did not satisfy $query: $("$UMBRIEL" windows --json)"
  return 1
}

pointer move 640 360
cat >> "$UMBRIEL_CONFIG" <<'TOML'

[animation]
enabled = false
[input.focus]
follows_mouse = false
[hot_corners.bottom_left]
enabled = true
delay_ms = 70
action = "window-focus-previous"
[hot_corners.bottom_right]
enabled = true
delay_ms = 70
action = "window-focus-next"
TOML
"$UMBRIEL" msg config-reload > /dev/null
"$CLIENT" corner-first > "$UMBRIEL_RUNTIME_DIR/corner-first.log" 2>&1 &
wait_window corner-first '.focused'
"$CLIENT" corner-fullscreen > "$UMBRIEL_RUNTIME_DIR/corner-fullscreen.log" 2>&1 &
wait_window corner-fullscreen '.focused'
"$UMBRIEL" msg window-toggle-fullscreen > /dev/null
"$UMBRIEL" settle > /dev/null
"$UMBRIEL" tearing --json | jq -e \
  'any(.surfaces[]; .title == "corner-fullscreen" and .fullscreen)' > /dev/null

pointer move 0 719 pause 250
wait_window corner-fullscreen '.focused'
pointer move 640 360

for chrome in absent exclusive; do
  if [[ $chrome == exclusive ]]; then
    "$LAYER" HEADLESS-1 40 keyboard=exclusive > "$UMBRIEL_RUNTIME_DIR/corner-layer.log" 2>&1 &
    for _ in $(seq 60); do
      "$UMBRIEL" layers --json | jq -e 'any(.[]; .mapped and .keyboard_interactivity == "exclusive")' > /dev/null && break
      sleep 0.05
    done
    "$UMBRIEL" layers --json | jq -e 'any(.[]; .mapped and .keyboard_interactivity == "exclusive")' > /dev/null
  fi
  "$UMBRIEL" msg overview-open > /dev/null
  "$UMBRIEL" settle > /dev/null
  pointer move 0 719 pause 250
  wait_window corner-first '.focused'
  pointer move 640 360
  pointer move 1279 719 pause 250
  wait_window corner-fullscreen '.focused'
  pointer move 640 360
  "$UMBRIEL" msg overview-close > /dev/null
  "$UMBRIEL" settle > /dev/null
  wait_window corner-fullscreen '.focused'
  "$UMBRIEL" tearing --json | jq -e \
    'any(.surfaces[]; .title == "corner-fullscreen" and .fullscreen)' > /dev/null
  pointer move 0 719 pause 250
  wait_window corner-fullscreen '.focused'
  pointer move 640 360
done

echo "previous/next corners navigate fullscreen overview cards and remain suppressed on the fullscreen desktop"
