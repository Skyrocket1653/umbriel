#!/usr/bin/env bash
# A fullscreen tile dropped over an ordinary tile must open a column, not cover a new sibling row.
set -euo pipefail
source "$UMBRIEL_HARNESS_LIB"

spawn_client() {
  foot --title="$1" sh -c 'sleep 120' > /dev/null 2>&1 &
}

wait_for_windows() {
  for _ in $(seq 60); do
    [[ $("$UMBRIEL" windows --json | jq length) == "$1" ]] && return 0
    sleep 0.1
  done
  echo "expected $1 windows: $("$UMBRIEL" windows --json)"
  return 1
}

window() {
  "$UMBRIEL" windows --json | jq -r --arg title "$1" ".[] | select(.title == \$title) | $2"
}

cat >> "$UMBRIEL_CONFIG" <<'CONFIG'

[layout.scrolling]
default_extent_fraction = 0.5
center_focused = "never"

[animation]
enabled = false
CONFIG
"$UMBRIEL" msg config-reload > /dev/null
spawn_client fullscreen-neighbor
wait_for_windows 1
spawn_client fullscreen-dragged
wait_for_windows 2
"$UMBRIEL" settle

# Both halves of the target's middle band used to offer a row. They must now choose the nearer column gap.
for side in left right; do
  "$UMBRIEL" msg window-focus "$(window fullscreen-dragged .id)" > /dev/null
  "$UMBRIEL" msg window-toggle-fullscreen > /dev/null
  "$UMBRIEL" settle
  read -r x y < <(window fullscreen-dragged '"\(.x + .w / 2 | floor) \(.y + .h / 2 | floor)"')
  # Hold after detachment so the destination geometry reflects the remaining strip.
  pointer_hold 1280 720 move "$x" "$y" mod logo press 272 move "$((x + 40))" "$y" -- release 272 mod none
  "$UMBRIEL" settle
  fraction=0.4
  [[ $side == right ]] && fraction=0.6
  read -r tx ty < <(window fullscreen-neighbor "\"\(.x + .w * $fraction | floor) \(.y + .h / 2 | floor)\"")
  "$UMBRIEL_POINTER_CLIENT" 1280 720 move "$tx" "$ty"
  "$UMBRIEL" settle
  pointer_release
  "$UMBRIEL" settle
  windows=$("$UMBRIEL" windows --json)
  if ! jq -e '
    (.[] | select(.title == "fullscreen-dragged")) as $dragged
    | (.[] | select(.title == "fullscreen-neighbor")) as $neighbor
    | $dragged.fullscreen and ($dragged.column != $neighbor.column)
  ' <<< "$windows" > /dev/null; then
    echo "fullscreen drop joined the neighbor instead of opening a column: $windows"
    exit 1
  fi
  "$UMBRIEL" msg window-toggle-fullscreen > /dev/null
  "$UMBRIEL" settle
  windows=$("$UMBRIEL" windows --json)
  if ! jq -e --arg side "$side" '
    (.[] | select(.title == "fullscreen-dragged")) as $dragged
    | (.[] | select(.title == "fullscreen-neighbor")) as $neighbor
    | ($dragged.column != $neighbor.column)
      and (if $side == "left" then $dragged.column < $neighbor.column else $dragged.column > $neighbor.column end)
  ' <<< "$windows" > /dev/null; then
    echo "fullscreen drop lost its separate column or chose the wrong side: $windows"
    exit 1
  fi
done

echo "fullscreen dragging preserves state and opens the nearer adjacent column"
