#!/usr/bin/env bash
# harness: xwayland=true
# An opt-in Xwayland rule retains X focus signals across native handoff without leaking native keystrokes.
# Removing the opt-in on reload restores normal cleanup.
set -euo pipefail

source "$UMBRIEL_HARNESS_LIB"

readonly X_CLIENT="${UMBRIEL_XWAYLAND_FOCUS_CLIENT:-./build-debug/tests/xwayland-focus-client}"
readonly NATIVE_CLIENT="${UMBRIEL_SEAT_LOG_CLIENT:-./build-debug/tests/seat-log-client}"
readonly KEYBOARD="${UMBRIEL_POINTER_CLIENT:-./build-debug/tests/pointer-client}"
readonly X_LOG="$UMBRIEL_RUNTIME_DIR/xwayland-focus.log"
readonly NATIVE_LOG="$UMBRIEL_RUNTIME_DIR/xwayland-native.log"
readonly X_CONTROL="$UMBRIEL_RUNTIME_DIR/xwayland-focus-control"
readonly X_TITLE_A=xwayland-focus-a
readonly NATIVE_TITLE=xwayland-native

if [[ ! -x $X_CLIENT || ! -x $NATIVE_CLIENT || ! -x $KEYBOARD || -z ${DISPLAY:-} ]]; then
  echo "Xwayland focus helpers or the private DISPLAY are not available"
  exit 1
fi

cat >> "$UMBRIEL_CONFIG" <<'EOF'

[animation]
enabled = false
[input.focus]
follows_mouse = false
[layout.scrolling]
default_extent_fraction = 0.75
[[window_rule]]
match.is_xwayland = true
match.title = "^xwayland-focus-a$"
xwayland_preserve_focus = true
EOF
"$UMBRIEL" msg config-reload > /dev/null

mkfifo "$X_CONTROL"
exec {x_control_fd}<>"$X_CONTROL"
"$X_CLIENT" "$X_TITLE_A" <&"$x_control_fd" > "$X_LOG" 2>&1 &
"$NATIVE_CLIENT" "$NATIVE_TITLE" > "$NATIVE_LOG" 2>&1 &

wait_for_windows() {
  for _ in $(seq 60); do
    local windows
    windows=$("$UMBRIEL" windows --json)
    if [[ $(jq -r --arg x "$X_TITLE_A" --arg n "$NATIVE_TITLE" \
      '[.[] | select(.title == $x or .title == $n)] | length' <<< "$windows") == 2 ]]; then
      return 0
    fi
    sleep 0.1
  done
  echo "the X11 and native focus fixtures did not all map: $("$UMBRIEL" windows --json)"
  echo "X11 log: $(tr '\n' '|' < "$X_LOG")"
  echo "native log: $(tr '\n' '|' < "$NATIVE_LOG")"
  return 1
}

window_id() {
  "$UMBRIEL" windows --json | jq -r --arg title "$1" '.[] | select(.title == $title) | .id'
}

focus_window() {
  local id=$1 title=$2
  "$UMBRIEL" msg window-focus "$id" > /dev/null
  for _ in $(seq 60); do
    if [[ $("$UMBRIEL" windows --json | jq -r --arg id "$id" \
      '.[] | select(.id == $id) | (.focused and .active)') == true ]]; then
      "$UMBRIEL" settle > /dev/null
      return 0
    fi
    sleep 0.1
  done
  echo "$title did not become focused and active: $("$UMBRIEL" windows --json)"
  return 1
}

state_sequence=0
await_x_state() {
  local phase=$1 expected_input=$2 expected_active=$3
  local last="no response"
  for attempt in $(seq 60); do
    state_sequence=$((state_sequence + 1))
    local label="${phase}_${state_sequence}"
    printf 'state %s\n' "$label" >&"$x_control_fd"
    for _ in $(seq 50); do
      last=$(grep "^state label=$label " "$X_LOG" | tail -1 || true)
      [[ -n $last ]] && break
      sleep 0.01
    done
    if [[ $last == "state label=$label input=$expected_input active=$expected_active" ]]; then
      return 0
    fi
    sleep 0.05
  done
  echo "$phase did not reach X input=$expected_input active=$expected_active, last response: $last"
  echo "X11 log: $(tr '\n' '|' < "$X_LOG")"
  return 1
}

await_new_event() {
  local file=$1 pattern=$2 before=$3 label=$4
  for _ in $(seq 60); do
    local count
    count=$(grep -c "$pattern" "$file" || true)
    if ((count > before)); then
      return 0
    fi
    sleep 0.1
  done
  echo "$label did not arrive: $(tr '\n' '|' < "$file")"
  return 1
}

wait_for_windows
readonly X_ID_A=$(window_id "$X_TITLE_A")
readonly NATIVE_ID=$(window_id "$NATIVE_TITLE")

# Establish the X11 side and verify the helper sees real key delivery. The
# second focus call exercises the no-transfer path used by chrome refreshes.
focus_window "$X_ID_A" "$X_TITLE_A"
await_x_state x-focused "$X_TITLE_A" "$X_TITLE_A"
x_keys_before=$(grep -c "^key window=$X_TITLE_A .* state=pressed$" "$X_LOG" || true)
"$KEYBOARD" 1280 720 tap 30 > /dev/null
await_new_event "$X_LOG" "^key window=$X_TITLE_A .* state=pressed$" "$x_keys_before" "the initial X11 key"

focus_window "$X_ID_A" "$X_TITLE_A"
await_x_state x-refreshed "$X_TITLE_A" "$X_TITLE_A"

# Native keyboard delivery must transfer even though the X focus signals are retained.
native_enters_before=$(events "$NATIVE_LOG" keyboard-enter)
focus_window "$NATIVE_ID" "$NATIVE_TITLE"
await_events "$NATIVE_LOG" keyboard-enter "$((native_enters_before + 1))" "$NATIVE_TITLE"
await_x_state native-focused "$X_TITLE_A" "$X_TITLE_A"

x_keys_before=$(grep -c "^key window=$X_TITLE_A .* state=pressed$" "$X_LOG" || true)
native_keys_before=$(events "$NATIVE_LOG" "keyboard-key code=31 state=pressed")
"$KEYBOARD" 1280 720 tap 31 > /dev/null
await_events "$NATIVE_LOG" "keyboard-key code=31 state=pressed" "$((native_keys_before + 1))" "$NATIVE_TITLE"
x_keys_after=$(grep -c "^key window=$X_TITLE_A .* state=pressed$" "$X_LOG" || true)
if ((x_keys_after != x_keys_before)); then
  echo "the native key also reached the old X11 window: $(tr '\n' '|' < "$X_LOG")"
  exit 1
fi

# Returning to X11 must restore the managed window in both focus channels and
# resume key delivery there.
focus_window "$X_ID_A" "$X_TITLE_A"
await_x_state x-restored "$X_TITLE_A" "$X_TITLE_A"
x_keys_before=$(grep -c "^key window=$X_TITLE_A .* state=pressed$" "$X_LOG" || true)
"$KEYBOARD" 1280 720 tap 32 > /dev/null
await_new_event "$X_LOG" "^key window=$X_TITLE_A .* state=pressed$" "$x_keys_before" "the restored X11 key"

# A keyboard handoff while the cursor remains over the X11 window must deliver leave and keep subsequent
# pointer refreshes from sending motion to that background window. X activation must still be retained.
focus_window "$X_ID_A" "$X_TITLE_A"
read -r x_center y_center < <("$UMBRIEL" windows --json | jq -r --arg id "$X_ID_A" \
  '.[] | select(.id == $id) | "\(.x + .w / 2 | floor) \(.y + .h / 2 | floor)"')
"$KEYBOARD" 1280 720 move 0 0 > /dev/null
x_enters_before=$(grep -c "^pointer enter window=$X_TITLE_A$" "$X_LOG" || true)
"$KEYBOARD" 1280 720 move "$x_center" "$y_center" > /dev/null
await_new_event "$X_LOG" "^pointer enter window=$X_TITLE_A$" "$x_enters_before" "X11 pointer enter"
x_leaves_before=$(grep -c "^pointer leave window=$X_TITLE_A$" "$X_LOG" || true)
focus_window "$NATIVE_ID" "$NATIVE_TITLE"
await_new_event "$X_LOG" "^pointer leave window=$X_TITLE_A$" "$x_leaves_before" "X11 pointer leave on keyboard handoff"
await_x_state pointer-released "$X_TITLE_A" "$X_TITLE_A"
x_motion_before=$(grep -c "^pointer motion window=$X_TITLE_A " "$X_LOG" || true)
"$KEYBOARD" 1280 720 relative 1 0 relative -1 0 > /dev/null
# Allow asynchronous input and scene refresh to run before checking that the game stayed detached.
sleep 0.2
await_x_state pointer-detached "$X_TITLE_A" "$X_TITLE_A"
x_motion_after=$(grep -c "^pointer motion window=$X_TITLE_A " "$X_LOG" || true)
if ((x_motion_after != x_motion_before)); then
  echo "background X11 window received pointer motion after keyboard handoff"
  exit 1
fi
# Explicit focus must re-enable pointer delivery.
focus_window "$X_ID_A" "$X_TITLE_A"
"$KEYBOARD" 1280 720 relative 1 0 relative 1 0 > /dev/null
await_new_event "$X_LOG" "^pointer motion window=$X_TITLE_A " "$x_motion_after" "restored X11 pointer motion"

# Exercise a game-like hidden-cursor, grab, query-and-recenter loop. No physical motion must mean no camera
# movement both in overview (the known-working case) and after a same-workspace handoff to the native window.
camera_snapshot() {
  local label=$1 line
  printf 'camera state %s\n' "$label" >&"$x_control_fd"
  for _ in $(seq 100); do
    line=$(grep "^camera label=$label " "$X_LOG" | tail -1 || true)
    if [[ $line =~ moves=([0-9]+)\ samples=([0-9]+)$ ]]; then
      printf '%s %s\n' "${BASH_REMATCH[1]}" "${BASH_REMATCH[2]}"
      return 0
    fi
    sleep 0.01
  done
  echo "camera snapshot $label timed out" >&2
  return 1
}
assert_camera_idle() {
  local phase=$1 before_moves before_samples after_moves after_samples
  read -r before_moves before_samples < <(camera_snapshot "${phase}_before")
  sleep 0.3
  read -r after_moves after_samples < <(camera_snapshot "${phase}_after")
  if ((after_moves != before_moves || after_samples <= before_samples)); then
    echo "$phase: camera must keep sampling without movement; before=$before_moves/$before_samples after=$after_moves/$after_samples"
    exit 1
  fi
}
camera_starts_before=$(grep -c '^camera-started$' "$X_LOG" || true)
printf 'camera start\n' >&"$x_control_fd"
await_new_event "$X_LOG" '^camera-started$' "$camera_starts_before" "game-like camera loop"
sleep 0.2
read -r moves_before _ < <(camera_snapshot camera_input_before)
"$KEYBOARD" 1280 720 relative 8 0 > /dev/null
sleep 0.2
read -r moves_after _ < <(camera_snapshot camera_input_after)
if ((moves_after <= moves_before)); then
  echo "camera fixture did not react to real pointer input"
  exit 1
fi
"$UMBRIEL" msg overview-open > /dev/null
"$UMBRIEL" settle > /dev/null
assert_camera_idle overview
"$UMBRIEL" msg overview-close > /dev/null
"$UMBRIEL" settle > /dev/null
focus_window "$X_ID_A" "$X_TITLE_A"
"$UMBRIEL" msg window-focus-warp "$X_ID_A" > /dev/null
sleep 0.2
camera_snapshot camera_layout_before > /dev/null
scene_x_before=$("$UMBRIEL" windows --json | jq -r --arg id "$X_ID_A" '.[] | select(.id == $id) | .x')
focus_window "$NATIVE_ID" "$NATIVE_TITLE"
await_x_state camera-native-retained "$X_TITLE_A" "$X_TITLE_A"
assert_camera_idle native_handoff
scene_x_after=$("$UMBRIEL" windows --json | jq -r --arg id "$X_ID_A" '.[] | select(.id == $id) | .x')
x_geometry_before=$(sed -n 's/^camera label=camera_layout_before x=\([-0-9]*\) .*$/\1/p' "$X_LOG")
x_geometry_after=$(sed -n 's/^camera label=native_handoff_after x=\([-0-9]*\) .*$/\1/p' "$X_LOG")
if [[ $scene_x_before == "$scene_x_after" || -z $x_geometry_before || $x_geometry_before != "$x_geometry_after" ]]; then
  echo "same-workspace scroll must move the scene, while retained-focus X coordinates stay fixed: scene=$scene_x_before/$scene_x_after X=$x_geometry_before/$x_geometry_after"
  exit 1
fi
read -r moves_before _ < <(camera_snapshot native_mouse_before)
read -r native_x native_y < <("$UMBRIEL" windows --json | jq -r --arg id "$NATIVE_ID" \
  '.[] | select(.id == $id) | "\(.x + .w / 2 | floor) \(.y + .h / 2 | floor)"')
"$KEYBOARD" 1280 720 move "$native_x" "$native_y" relative 8 0 > /dev/null
sleep 0.2
read -r moves_after _ < <(camera_snapshot native_mouse_after)
if ((moves_after != moves_before)); then
  echo "game-like camera moved while input belonged to the native window"
  exit 1
fi
return_moves=$moves_after
focus_window "$X_ID_A" "$X_TITLE_A"
"$UMBRIEL" msg window-focus-warp "$X_ID_A" > /dev/null
sleep 0.2
read -r moves_before _ < <(camera_snapshot camera_restored_before)
if ((moves_before != return_moves)); then
  echo "returning keyboard focus or the follows-focus warp changed the camera without physical mouse movement"
  exit 1
fi
assert_camera_idle restored_capture
"$KEYBOARD" 1280 720 relative 8 0 > /dev/null
sleep 0.2
read -r moves_after _ < <(camera_snapshot camera_restored_after)
if ((moves_after <= moves_before)); then
  echo "camera fixture did not resume input after refocus"
  exit 1
fi
printf 'camera stop\n' >&"$x_control_fd"
await_new_event "$X_LOG" '^camera-stopped$' 0 "camera loop shutdown"
sleep 0.2
# Once capture is released, ordinary menu-style pointer motion must work again.
menu_motion_before=$(grep -c "^pointer motion window=$X_TITLE_A " "$X_LOG" || true)
"$KEYBOARD" 1280 720 relative 4 0 relative 4 0 > /dev/null
await_new_event "$X_LOG" "^pointer motion window=$X_TITLE_A " "$menu_motion_before" "pointer motion after capture release"

# Disable while focus stays on the native client: stale X focus must now clear.
focus_window "$NATIVE_ID" "$NATIVE_TITLE"
await_x_state native-retained "$X_TITLE_A" "$X_TITLE_A"
sed -i 's/^xwayland_preserve_focus = true$/xwayland_preserve_focus = false/' "$UMBRIEL_CONFIG"
"$UMBRIEL" msg config-reload > /dev/null
await_x_state rule-disabled none none

echo "opt-in retains X focus without native key leakage; disabling it restores cleanup"
