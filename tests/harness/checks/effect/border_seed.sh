#!/usr/bin/env bash
# A seed-only shader gets per-window colour, stable across frames, focus, reloads and overview copies, without
# requesting continuous effect frames. Reading the built-in seed also proves that the border preamble declares it.
set -euo pipefail
readonly IMAGE="$UMBRIEL_RUNTIME_DIR/border-seed.png"
cat > "$UMBRIEL_RUNTIME_DIR/seed.glsl" <<'GLSL'
vec4 border(vec2 uv) {
  return vec4(vec3(0.2) + 0.6 * umbriel_random_seed.xyz, 1.0);
}
GLSL
cat >> "$UMBRIEL_CONFIG" <<'TOML'

[animation]
enabled = false
[input.focus]
follows_mouse = false
[appearance]
border_width = 4
outer_border_width = 0
corner_radius = 0
[appearance.shadow]
enabled = false
[appearance.blur]
enabled = false
[overview]
zoom = 0.5
[effects]
border = "seed"
border_unfocused = true
[effects.preset.seed]
kind = "border"
shader = "seed.glsl"
padding = 20
[[window_rule]]
match.title = "^seed-one$"
default_floating = true
default_position = { x = 100, y = 100, anchor = "top_left" }
[[window_rule]]
match.title = "^seed-two$"
default_floating = true
default_position = { x = 700, y = 100, anchor = "top_left" }
TOML
"$UMBRIEL" msg config-reload > /dev/null
spawn() {
  FILL_COLOR=0xFF0000FF "$UMBRIEL_UNMAP_CLIENT" "$1" 300 200 > "$UMBRIEL_RUNTIME_DIR/$1.log" 2>&1 &
  for _ in $(seq 80); do
    window=$("$UMBRIEL" windows --json | jq -c --arg title "$1" '.[] | select(.title == $title)')
    [[ -n $window ]] && break
    sleep 0.025
  done
  [[ -n $window ]]
  "$UMBRIEL" settle > /dev/null
}
sample() {
  grim "$IMAGE"
  "$UMBRIEL_PIXEL_PROBE" "$IMAGE" pixel "$1" "$2"
}
same_colour() {
  awk -v a="$1" -v b="$2" 'BEGIN {
    split(a,x); split(b,y);
    for (i=1;i<=3;i++) if (x[i]-y[i]>2 || y[i]-x[i]>2) exit 1;
  }'
}
spawn seed-one
read -r x1 y1 w1 id1 < <(jq -r '"\(.x) \(.y) \(.w) \(.id)"' <<< "$window")
first=$(sample "$((x1 + w1 / 2))" "$((y1 - 12))")
# The shader always paints all channels in [0.2,0.8]; a missing program leaves transparent padding.
awk -v a="$first" 'BEGIN { split(a,x); for(i=1;i<=3;i++) if(x[i]<49 || x[i]>206) exit 1; }'
spawn seed-two
read -r x2 y2 w2 _ < <(jq -r '"\(.x) \(.y) \(.w) \(.id)"' <<< "$window")
second=$(sample "$((x2 + w2 / 2))" "$((y2 - 12))")
[[ $first != "$second" ]] || { echo "windows share the same seed colour"; exit 1; }
same_colour "$first" "$(sample "$((x1 + w1 / 2))" "$((y1 - 12))")"
"$UMBRIEL" msg window-focus "$id1" > /dev/null
"$UMBRIEL" msg config-reload > /dev/null
"$UMBRIEL" settle > /dev/null
same_colour "$first" "$(sample "$((x1 + w1 / 2))" "$((y1 - 12))")"
frames() { "$UMBRIEL" effect-frames --json | jq -er '.outputs[0].effect_frames'; }
before=$(frames)
sleep 0.3
[[ $(frames) == "$before" ]] || { echo "seed-only shader requested continuous frames"; exit 1; }
read -r out_w out_h < <("$UMBRIEL" outputs | awk '/Usable area:/ {split($3,a,/[x+]/); print a[1],a[2]; exit}')
"$UMBRIEL" msg overview-open > /dev/null
"$UMBRIEL" settle > /dev/null
row_x=$(((out_w - out_w / 2) / 2))
row_y=$(((out_h - out_h / 2) / 2))
same_colour "$first" "$(sample "$((row_x + x1 / 2 + w1 / 4))" "$((row_y + y1 / 2 - 6))")"
same_colour "$second" "$(sample "$((row_x + x2 / 2 + w2 / 4))" "$((row_y + y2 / 2 - 6))")"
echo "border random seeds differ per window, stay stable on reload and overview, and do not request frames"
