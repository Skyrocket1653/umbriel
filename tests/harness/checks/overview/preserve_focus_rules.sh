#!/usr/bin/env bash
# Opening overview retains focused rule selection, including with shell keyboard capture; navigation transfers it.
set -euo pipefail
readonly CLIENT="${UMBRIEL_UNMAP_CLIENT:-./build-debug/tests/unmap-client}"
readonly LAYER="${UMBRIEL_LAYER_CLIENT:-./build-debug/tests/layer-client}"
cat > "$UMBRIEL_RUNTIME_DIR/focus-rule.glsl" <<'GLSL'
vec4 border(vec2 uv) { return umbriel_sample(uv); }
GLSL
cat >> "$UMBRIEL_CONFIG" <<'TOML'

[animation]
enabled = false
[input.focus]
follows_mouse = false
[effects.preset.selected]
kind = "border"
shader = "focus-rule.glsl"
[effects.preset.other]
kind = "border"
shader = "focus-rule.glsl"
[[window_rule]]
match.is_focused = true
border_effect = "selected"
[[window_rule]]
match.is_focused = false
border_effect = "other"
TOML
"$UMBRIEL" msg config-reload > /dev/null
wait_rule() {
  for _ in $(seq 80); do
    "$UMBRIEL" windows --json | jq -e --arg title "$1" --arg name "$2" \
      'any(.[]; .title == $title and .border_effect.name == $name)' > /dev/null && return 0
    sleep 0.05
  done
  echo "wrong focus rules: $("$UMBRIEL" windows --json)"
  return 1
}
"$CLIENT" focus-rule-first > /dev/null 2>&1 &
wait_rule focus-rule-first selected
"$CLIENT" focus-rule-second > /dev/null 2>&1 &
wait_rule focus-rule-second selected
wait_rule focus-rule-first other
"$UMBRIEL" msg overview-open > /dev/null
"$UMBRIEL" settle > /dev/null
wait_rule focus-rule-second selected
wait_rule focus-rule-first other
"$LAYER" HEADLESS-1 40 keyboard=exclusive > /dev/null 2>&1 &
layer_pid=$!
for _ in $(seq 60); do
  "$UMBRIEL" layers --json | jq -e 'any(.[]; .mapped and .keyboard_interactivity == "exclusive")' > /dev/null && break
  sleep 0.05
done
"$UMBRIEL" layers --json | jq -e 'any(.[]; .mapped and .keyboard_interactivity == "exclusive")' > /dev/null
wait_rule focus-rule-second selected
"$UMBRIEL" msg window-focus-previous > /dev/null
wait_rule focus-rule-first selected
wait_rule focus-rule-second other
kill "$layer_pid"
for _ in $(seq 60); do
  "$UMBRIEL" layers --json | jq -e 'any(.[]; .mapped and .keyboard_interactivity == "exclusive")' > /dev/null || break
  sleep 0.05
done
"$UMBRIEL" layers --json | jq -e 'all(.[]; (.mapped and .keyboard_interactivity == "exclusive") | not)' > /dev/null
"$UMBRIEL" msg overview-close > /dev/null
"$UMBRIEL" settle > /dev/null
wait_rule focus-rule-first selected
wait_rule focus-rule-second other
echo "overview retains focused rules, shell keyboard capture preserves them, and navigation transfers them"
