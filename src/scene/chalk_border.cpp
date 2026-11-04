#include "scene/chalk_border.h"

#include "config/config.h"
#include "core/log.h"
#include "scene/cairo_buffer.h"
#include "scene/chalk_timing.h"
#include "wlr.h"

extern "C" {
#include <umbrielfx/render/chalk.h>
#include <wlr/render/wlr_texture.h>
}

#include <algorithm>
#include <chrono>
#include <cmath>
#include <random>
#include <span>
#include <utility>

namespace umbriel {
  namespace {
    using Clock = std::chrono::steady_clock;

    constexpr ChalkColor kDefaultBackground{0, 0, 0, 1};
    constexpr ChalkColor kDefaultForeground{0.671F, 0.698F, 0.749F, 1.0F};

    std::span<const ChalkColor> foreground(const ChalkSettings& settings) {
      return settings.foreground ? std::span<const ChalkColor>(*settings.foreground)
                                 : std::span<const ChalkColor>(&kDefaultForeground, 1);
    }

    uint32_t palettePixel(const ChalkColor& color) {
      const auto channel = [](float value) { return static_cast<uint32_t>(std::lround(value * 255.0F)); };
      const float alpha = color[3];
      return channel(alpha) << 24
          | channel(color[0] * alpha) << 16
          | channel(color[1] * alpha) << 8
          | channel(color[2] * alpha);
    }
  } // namespace

  void prepareChalkMasks(wlr_renderer* renderer, const Config& config) {
    const auto& appearance = config.appearance;
    const auto& base = appearance.border.chalk;
    int sets = base.patternSets.value_or(kDefaultChalkPatternSets);
    int patterns = base.patternsPerSet.value_or(kDefaultChalkPatternsPerSet);
    // Rules can match together and override either dimension independently.
    for (const auto& rule : config.windowRules) {
      sets = std::max(sets, rule.borderChalk.patternSets.value_or(0));
      patterns = std::max(patterns, rule.borderChalk.patternsPerSet.value_or(0));
    }
    int rows = sets * patterns / 2;
    const auto& screen = appearance.screen.innerBorder;
    for (const auto* state : {&screen.whenCasting, &screen.whenNotCasting}) {
      if (state->style == BorderStyle::Chalk) {
        const int screenSets = state->chalk.patternSets.value_or(base.patternSets.value_or(kDefaultChalkPatternSets));
        const int screenPatterns =
            state->chalk.patternsPerSet.value_or(base.patternsPerSet.value_or(kDefaultChalkPatternsPerSet));
        rows = std::max(rows, screenSets * screenPatterns / 2);
        rows = std::max(
            rows,
            state->unfocusedChalk.patternSets.value_or(screenSets)
                * state->unfocusedChalk.patternsPerSet.value_or(screenPatterns)
                / 2
        );
      }
    }
    if (!fx_renderer_prepare_chalk_masks(renderer, rows)) {
      Logger("chalk").warn("could not prepare chalk masks for {} rows", rows);
    }
  }

  struct ChalkBorder::State {
    ChalkSettings settings;
    std::function<bool()> damage;
    wl_event_source* timer = nullptr;
    Clock::time_point epoch = Clock::now();
    float maskOffset = 0.0F;
    float paletteOffset = 0.0F;
    struct Palette {
      wl_listener rendererDestroy{};
      wlr_texture* texture = nullptr;
    } palette;
    bool enabled = false;
    float opacity = 1.0F;

    State(wl_event_loop* loop, std::function<bool()> fn) : damage(std::move(fn)) {
      std::mt19937 random(std::random_device{}());
      maskOffset = static_cast<float>(std::uniform_int_distribution(0, FX_CHALK_MASK_WIDTH - 1)(random));
      paletteOffset = std::uniform_real_distribution(0.0F, 1.0F)(random);
      palette.rendererDestroy.notify = [](wl_listener* listener, void*) {
        Palette* palette = wl_container_of(listener, palette, rendererDestroy);
        wlr_texture_destroy(std::exchange(palette->texture, nullptr));
        wl_list_remove(&listener->link);
        wl_list_init(&listener->link);
      };
      wl_list_init(&palette.rendererDestroy.link);
      timer = wl_event_loop_add_timer(
          loop,
          [](void* data) {
            auto& state = *static_cast<State*>(data);
            if (state.enabled) {
              state.damage();
            }
            return 0;
          },
          this
      );
    }

    ~State() {
      if (timer != nullptr) {
        wl_event_source_remove(timer);
      }
      clearPalette();
    }

    void clearPalette() {
      wl_list_remove(&palette.rendererDestroy.link);
      wl_list_init(&palette.rendererDestroy.link);
      if (palette.texture != nullptr) {
        wlr_texture_destroy(std::exchange(palette.texture, nullptr));
      }
    }

    bool ensurePalette(wlr_renderer* renderer) {
      if (palette.texture != nullptr && palette.texture->renderer == renderer) {
        return true;
      }
      clearPalette();
      const auto colors = foreground(settings);
      auto* image = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, static_cast<int>(colors.size()) + 1, 1);
      if (cairo_surface_status(image) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(image);
        return false;
      }
      auto* pixels = reinterpret_cast<uint32_t*>(cairo_image_surface_get_data(image));
      pixels[0] = palettePixel(settings.background.value_or(kDefaultBackground));
      std::ranges::transform(colors, pixels + 1, palettePixel);
      cairo_surface_mark_dirty(image);
      auto* buffer = createCairoBuffer(image);
      palette.texture = wlr_texture_from_buffer(renderer, buffer);
      wlr_buffer_drop(buffer);
      if (palette.texture == nullptr) {
        return false;
      }
      wl_signal_add(&renderer->events.destroy, &palette.rendererDestroy);
      return true;
    }

    fx_chalk_frame frame(ChalkTicks ticks) const {
      const auto mask = chalkMaskFrame(
          ticks.pixels, settings.patternSets.value_or(kDefaultChalkPatternSets),
          settings.patternsPerSet.value_or(kDefaultChalkPatternsPerSet)
      );
      const auto count = static_cast<int64_t>(foreground(settings).size());
      const auto slot = (ticks.segments / 4) % count;
      const auto jump = slot % 2 == 0 ? slot / 2 : count - 1 - slot / 2;
      const bool shiftBoundary = ticks.segments % 2 != 0;
      return {
          .row = mask.row,
          .inverted = mask.inverted,
          .alternate = mask.alternate,
          .palette_shift = static_cast<float>(jump + (shiftBoundary ? count / 2 : 0)),
          .palette_boundary = shiftBoundary ? static_cast<float>(settings.boundaryJumpFraction.value_or(0.4)) : 0.0F,
      };
    }
  };

  ChalkBorder::ChalkBorder(wl_event_loop* loop, std::function<bool()> damage)
      : m_state(std::make_unique<State>(loop, std::move(damage))) {}

  ChalkBorder::~ChalkBorder() = default;

  void ChalkBorder::setOpacity(float opacity) { m_state->opacity = opacity; }

  bool ChalkBorder::opaque() const {
    return m_state->enabled && m_state->opacity >= 1.0F && m_state->settings.opaque();
  }

  void ChalkBorder::inheritPhase(const ChalkBorder& source) {
    m_state->epoch = source.m_state->epoch;
    m_state->maskOffset = source.m_state->maskOffset;
    m_state->paletteOffset = source.m_state->paletteOffset;
  }

  bool ChalkBorder::configure(ChalkSettings settings, bool enabled) {
    auto& state = *m_state;
    if (state.enabled == enabled && state.settings == settings) {
      return false;
    }
    if (!state.enabled && enabled) {
      state.epoch = Clock::now();
    }
    if (!enabled
        || state.settings.background != settings.background
        || state.settings.foreground != settings.foreground) {
      state.clearPalette();
    }
    state.enabled = enabled;
    state.settings = std::move(settings);
    if (state.timer != nullptr) {
      wl_event_source_timer_update(state.timer, 0);
    }
    return true;
  }

  bool ChalkBorder::draw(
      void* data, fx_gles_render_pass* pass, wlr_renderer* renderer, const fx_render_border_options* border
  ) {
    auto& state = *static_cast<ChalkBorder*>(data)->m_state;
    if (!state.enabled || !state.ensurePalette(renderer)) {
      return false;
    }
    const double interval = state.settings.secondsPerUpdate.value_or(8.0);
    const auto millis = [](double seconds) { return std::llround(seconds * 1000.0); };
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - state.epoch).count();
    const ChalkTiming timing = chalkTiming(
        elapsed, millis(state.settings.pixelSecondsPerUpdate.value_or(interval)),
        millis(state.settings.segmentSecondsPerUpdate.value_or(interval))
    );
    const double fade =
        std::min<double>(millis(state.settings.fadeSeconds.value_or(2.0)), timing.nextUpdate - timing.update);
    const auto sinceUpdate = elapsed - timing.update;
    const bool fading = timing.update > 0 && sinceUpdate < fade;
    const double frameMs = 1000.0 / state.settings.fadeFps.value_or(12);
    const double step = fading ? std::floor(sinceUpdate / frameMs) : 0.0;
    const double next = fading ? timing.update + std::min(fade, (step + 1) * frameMs) : timing.nextUpdate;
    const double t = fading ? step * frameMs / fade : 1.0;
    const bool randomized = state.settings.randomizeInitialPattern.value_or(true);
    const fx_chalk_pattern pattern{
        .palette = state.palette.texture,
        .current = state.frame(timing.current),
        .previous = state.frame(timing.previous),
        .opacity = state.opacity,
        .blend = static_cast<float>(t * t * (3 - 2 * t)),
        .cell_px = static_cast<float>(state.settings.cellPx.value_or(1.0)),
        .palette_repeats = static_cast<float>(state.settings.paletteRepeats.value_or(1.0)),
        .always_flip_spacing = state.settings.alwaysFlipSpacing.value_or(8),
        .mask_offset = randomized ? state.maskOffset : 0.0F,
        .palette_offset = randomized ? state.paletteOffset : 0.0F,
    };
    if (!fx_render_pass_add_chalk_border(pass, border, &pattern)) {
      return false;
    }
    if (state.timer != nullptr) {
      wl_event_source_timer_update(state.timer, std::max(1, static_cast<int>(std::ceil(next - elapsed))));
    }
    return true;
  }
} // namespace umbriel
