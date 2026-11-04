#extension GL_OES_standard_derivatives : enable

precision highp float;

varying vec4 v_color;
varying vec2 v_texcoord;

uniform vec2 clip_size;
uniform vec2 clip_position;
uniform float inner_radius_top_left;
uniform float inner_radius_top_right;
uniform float inner_radius_bottom_left;
uniform float inner_radius_bottom_right;
uniform float seam_radius_top_left;
uniform float seam_radius_top_right;
uniform float seam_radius_bottom_left;
uniform float seam_radius_bottom_right;
uniform float outer_radius_top_left;
uniform float outer_radius_top_right;
uniform float outer_radius_bottom_left;
uniform float outer_radius_bottom_right;

uniform float inner_width;
uniform float outer_width;
uniform vec4 inner_color;

uniform bool chalk_enabled;
uniform sampler2D chalk_masks;
uniform sampler2D chalk_palette;
uniform vec2 chalk_mask_size;
uniform vec4 chalk_style; // Cell size, forced flip spacing, palette repeats, opacity.
uniform vec2 chalk_offsets;
uniform float chalk_palette_size;
uniform vec4 chalk_current; // Atlas row, inversion, palette shift, boundary offset.
uniform vec4 chalk_previous;
uniform vec3 chalk_blend; // Fade weight, current forced flip phase, previous forced flip phase.
uniform int chalk_decode;

const float quarter_turn = 1.57079632679;

float clockwise_arc(float across, float ahead, float radius) {
  return radius * atan(max(ahead, 0.0), max(across, 0.00001));
}

// Walk clockwise. Each edge owns the corner at its end, including square corners.
vec3 chalk_perimeter(vec2 p, vec2 size, vec4 radius, float thickness, float depth) {
  float tl = radius.x, tr = radius.y, bl = radius.z, br = radius.w;
  float square = min(thickness, min(size.x, size.y) * 0.5);
  vec4 span = mix(vec4(square), radius, sign(radius));
  float right = size.x - span.x - tr + quarter_turn * tr;
  float bottom = right + size.y - span.y - br + quarter_turn * br;
  float left = bottom + size.x - span.w - bl + quarter_turn * bl;
  float length = left + size.y - span.z - tl + quarter_turn * tl;
  vec2 opposite = size - p;
  float along;
  if (opposite.x <= span.y && p.y <= span.y) {
    along = right - quarter_turn * tr + clockwise_arc(tr - p.y, tr - opposite.x, tr);
    if (tr == 0.0) {
      along -= opposite.x;
      depth = p.y;
    }
  } else if (opposite.x <= span.w && opposite.y <= span.w) {
    along = bottom - quarter_turn * br + clockwise_arc(br - opposite.x, br - opposite.y, br);
    if (br == 0.0) {
      along -= opposite.y;
      depth = opposite.x;
    }
  } else if (p.x <= span.z && opposite.y <= span.z) {
    along = left - quarter_turn * bl + clockwise_arc(bl - opposite.y, bl - p.x, bl);
    if (bl == 0.0) {
      along -= p.x;
      depth = opposite.y;
    }
  } else if (p.x <= span.x && p.y <= span.x) {
    along = length - quarter_turn * tl + clockwise_arc(tl - p.x, tl - p.y, tl);
    if (tl == 0.0) {
      along -= p.y;
      depth = p.x;
    }
  } else {
    float nearest = min(min(p.x, p.y), min(opposite.x, opposite.y));
    if (nearest == p.y) {
      along = p.x - span.x;
    } else if (nearest == opposite.x) {
      along = right + p.y - span.y;
    } else if (nearest == opposite.y) {
      along = bottom + opposite.x - span.w;
    } else {
      along = left + opposite.y - span.z;
    }
  }
  return vec3(clamp(along, 0.0, length), max(length, 1.0), max(depth, 0.0));
}

vec4 chalk_color(vec4 frame, float alternate, vec2 cell, float progress) {
  float column = mod(cell.x + 131.0 * cell.y + chalk_offsets.x, chalk_mask_size.x);
  vec2 uv = (vec2(column, frame.x) + 0.5) / chalk_mask_size;
  float foreground = abs(step(0.5, texture2D(chalk_masks, uv).r) - frame.y);
  // Forced dots alternate every tick, independently of the selected base mask.
  if (chalk_style.y > 0.0 && mod(cell.x + cell.y, chalk_style.y) == 0.0) {
    float base = step(0.5, texture2D(chalk_masks, vec2(uv.x, 0.5 / chalk_mask_size.y)).r);
    foreground = abs(base - alternate);
  }
  float count = chalk_palette_size - 1.0;
  float index = 0.0;
  if (foreground > 0.5) {
    float band = fract(progress * chalk_style.z) * count;
    float shift = chalk_offsets.y * (count + 1.0) + frame.z + frame.w;
    index = 1.0 + mod(floor(band + shift), count);
  }
  return texture2D(chalk_palette, vec2((index + 0.5) / chalk_palette_size, 0.5));
}

vec4 decode_chalk(vec4 color) {
  if (chalk_decode == 0 || color.a == 0.0) {
    return color;
  }
  vec3 rgb = color.rgb / color.a;
  if (chalk_decode == 2) {
    rgb = pow(rgb, vec3(2.2));
  } else {
    vec3 lo = rgb / 12.92;
    vec3 hi = pow((rgb + 0.055) / 1.055, vec3(2.4));
    rgb = mix(lo, hi, step(vec3(0.04045), rgb));
  }
  return vec4(rgb * color.a, color.a);
}

float rounded_rect_distance(
    vec2 size, vec2 position, float radius_tl, float radius_tr, float radius_bl, float radius_br
);

float antialias_width(float distance) {
  vec2 gradient = vec2(dFdx(distance), dFdy(distance));
  return max(length(gradient), 1.0) * 0.5;
}

float contour_distance(float outset, float radius_tl, float radius_tr, float radius_bl, float radius_br) {
  vec2 expansion = vec2(outset);
  return rounded_rect_distance(
      clip_size + expansion * 2.0, clip_position - expansion, radius_tl, radius_tr, radius_bl, radius_br
  );
}

void main() {
  float total_width = inner_width + outer_width;
  float inner_distance = contour_distance(
      0.0, inner_radius_top_left, inner_radius_top_right, inner_radius_bottom_left, inner_radius_bottom_right
  );
  float outer_distance = contour_distance(
      total_width, outer_radius_top_left, outer_radius_top_right, outer_radius_bottom_left, outer_radius_bottom_right
  );

  float inner_antialias = antialias_width(inner_distance);
  float outer_antialias = antialias_width(outer_distance);
  float inner_coverage = smoothstep(-inner_antialias, inner_antialias, inner_distance);
  float outer_coverage = 1.0 - smoothstep(-outer_antialias, outer_antialias, outer_distance);

  if (chalk_enabled) {
    vec2 outer_size = clip_size + 2.0 * total_width;
    vec2 outer_pos = gl_FragCoord.xy - clip_position + total_width;
    vec4 radius =
        min(vec4(outer_radius_top_left, outer_radius_top_right, outer_radius_bottom_left, outer_radius_bottom_right),
            vec4(min(outer_size.x, outer_size.y) * 0.5));
    vec3 perimeter = chalk_perimeter(outer_pos, outer_size, radius, total_width, -outer_distance);
    vec2 cell = floor(vec2(mod(perimeter.x, perimeter.y), perimeter.z) / chalk_style.x);
    float progress = perimeter.x / perimeter.y;
    vec4 color = chalk_color(chalk_current, chalk_blend.y, cell, progress);
    if (chalk_blend.x < 1.0) {
      color = mix(chalk_color(chalk_previous, chalk_blend.z, cell, progress), color, chalk_blend.x);
    }
    gl_FragColor = decode_chalk(color) * (chalk_style.w * min(inner_coverage, outer_coverage));
    return;
  }

  float seam_distance = contour_distance(
      inner_width, seam_radius_top_left, seam_radius_top_right, seam_radius_bottom_left, seam_radius_bottom_right
  );
  float seam_antialias = antialias_width(seam_distance);
  float outer_mix = 0.0;
  if (outer_width > 0.0) {
    outer_mix = inner_width > 0.0 ? smoothstep(-seam_antialias, seam_antialias, seam_distance) : 1.0;
  }
  vec4 color = mix(inner_color, v_color, outer_mix);
  gl_FragColor = color * min(inner_coverage, outer_coverage);
}
