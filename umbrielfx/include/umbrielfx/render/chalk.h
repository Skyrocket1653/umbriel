#ifndef UMBRIELFX_RENDER_CHALK_H
#define UMBRIELFX_RENDER_CHALK_H

#include <stdbool.h>

struct fx_gles_render_pass;
struct fx_render_border_options;
struct wlr_texture;

// One immutable mask atlas per renderer. Only the selected rows change during animation.
#define FX_CHALK_MASK_WIDTH 1024
#define FX_CHALK_MAX_MASK_ROWS 128

struct fx_chalk_frame {
  int row;
  bool inverted;
  bool alternate;
  float palette_shift;
  float palette_boundary;
};

struct fx_chalk_pattern {
  struct wlr_texture* palette;
  struct fx_chalk_frame current, previous;
  float opacity, blend, cell_px, palette_repeats;
  int always_flip_spacing;
  float mask_offset, palette_offset;
};

bool fx_render_pass_add_chalk_border(
    struct fx_gles_render_pass* pass, const struct fx_render_border_options* options,
    const struct fx_chalk_pattern* chalk
);

#endif
