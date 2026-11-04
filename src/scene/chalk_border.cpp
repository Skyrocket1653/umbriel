#include "scene/chalk_border.h"
#include "scene/cairo_buffer.h"
#include "wlr.h"

extern "C" {
#include <wlr/render/wlr_texture.h>
}

#include <algorithm>
#include <chrono>
#include <cmath>
#include <random>
#include <tuple>
#include <utility>

namespace umbriel {
  namespace {
    using Clock = std::chrono::steady_clock;
    using Color = ChalkColor;

    float fract(float x) { return x - std::floor(x); }

    float chalkHash(float x, float y, float salt, const Color& seed) {
      x = fract((x + seed[0] * 127.0F) * 0.1031F);
      y = fract((y + seed[1] * 127.0F) * 0.1030F);
      float z = fract((salt + seed[2] * 127.0F) * 0.0973F);
      const float dot = x * (y + 33.33F) + y * (z + 33.33F) + z * (x + 33.33F);
      return fract(((x + dot) + (y + dot)) * (z + dot));
    }

    float roundedDistance(float x, float y, float w, float h, const std::array<float, 4>& corners) {
      x -= w / 2;
      y -= h / 2;
      const float radius = std::min(
          std::min(w, h) / 2, y < 0 ? (x < 0 ? corners[0] : corners[1])
                                   : (x < 0 ? corners[2] : corners[3])
      );
      const float dx = std::abs(x) - w / 2 + radius;
      const float dy = std::abs(y) - h / 2 + radius;
      return std::hypot(std::max(dx, 0.0F), std::max(dy, 0.0F)) + std::min(std::max(dx, dy), 0.0F) - radius;
    }

    struct Ring {
      int width, height;
      float x, y, holeWidth, holeHeight, thickness;
      std::array<float, 4> inner, outer;

      auto key() const {
        return std::tuple(width, height, x, y, holeWidth, holeHeight, thickness, inner[0], inner[1],
                          inner[2], inner[3], outer[0], outer[1],
                          outer[2], outer[3]);
      }
    };

    struct Strip {
      wlr_box box{};
      std::vector<uint32_t> oldPixels, pixels;
      wlr_texture* texture = nullptr;
    int pairAxis = 0;
    };

    struct TextureWatch {
      wl_listener listener{};
      std::array<Strip, 4>* strips = nullptr;
    };
  }

  struct ChalkBorder::State {
    ChalkSettings settings;
    std::function<bool()> damage;
    wl_event_source* timer = nullptr;
    Clock::time_point epoch = Clock::now();
    Color randomSeed;
    wlr_renderer* renderer = nullptr;
    TextureWatch watch;
    std::array<Strip, 4> strips;
    std::optional<Ring> ring;
    int64_t generatedTick = -1;
    bool uploaded = false;
    bool enabled = false;
    float opacity = 1.0F;

    State(wl_event_loop* loop, std::function<bool()> fn) : damage(std::move(fn)) {
      std::mt19937 random(std::random_device{}());
      std::uniform_real_distribution<float> uniform(0.0F, 1.0F);
      for (float& value : randomSeed) value = uniform(random);
      watch.strips = &strips;
      watch.listener.notify = [](wl_listener* listener, void*) {
        auto* watch = reinterpret_cast<TextureWatch*>(listener);
        for (auto& strip : *watch->strips) {
          if (strip.texture != nullptr) wlr_texture_destroy(strip.texture);
          strip.texture = nullptr;
        }
        wl_list_remove(&listener->link);
        wl_list_init(&listener->link);
      };
      wl_list_init(&watch.listener.link);
      timer = wl_event_loop_add_timer(loop, [](void* data) {
        auto& state = *static_cast<State*>(data);
        if (state.enabled) state.damage();
        return 0;
      }, this);
    }

    ~State() {
      if (timer != nullptr) wl_event_source_remove(timer);
      clearTextures();
      wl_list_remove(&watch.listener.link);
    }

    void clearTextures() {
      for (auto& strip : strips) {
        if (strip.texture != nullptr) wlr_texture_destroy(strip.texture);
        strip.texture = nullptr;
      }
      uploaded = false;
    }

    uint32_t pixel(const Ring& ring, int x, int y, int64_t tick) const {
      const float px = x + 0.5F, py = y + 0.5F;
      const float inner = roundedDistance(px - ring.x, py - ring.y, ring.holeWidth, ring.holeHeight, ring.inner);
      const float outer = roundedDistance(px - ring.x + ring.thickness, py - ring.y + ring.thickness,
                                          ring.holeWidth + 2 * ring.thickness,
                                          ring.holeHeight + 2 * ring.thickness, ring.outer);
      const float coverage = std::min(std::clamp(inner + 0.5F, 0.0F, 1.0F),
                                      std::clamp(0.5F - outer, 0.0F, 1.0F));
      if (coverage == 0.0F) return 0;
      static const std::vector<Color> fallback{{0.671F, 0.698F, 0.749F, 1.0F}};
      const auto& palette = settings.foreground ? *settings.foreground : fallback;
      const bool randomized = settings.randomizeInitialPattern.value_or(true);
      const auto hash = [&](float x, float y, float salt) {
        return randomized ? chalkHash(x, y, salt, randomSeed) : chalkHash(x, y, salt + 7.0F, {});
      };
      const float cell = static_cast<float>(settings.cellPx.value_or(1.0));
      const float cx = std::floor(px / cell), cy = std::floor(py / cell);
      const auto pair = tick / 4;
      const auto layout = tick % 2;
      const auto count = static_cast<int64_t>(palette.size());
      bool foreground = (hash(cx, cy, static_cast<float>(2 * (pair % 4093) + layout)) >= 0.5F)
          != ((tick / 2) % 2 != 0);
      const int spacing = settings.alwaysFlipSpacing.value_or(8);
      if (spacing > 0 && std::fmod(cx + cy, spacing) == 0.0F) {
        foreground = ((hash(cx, cy, -1.0F) >= 0.5F) + layout + (pair / count) % 2) % 2 != 0;
      }
      Color color = settings.background.value_or(Color{0, 0, 0, 1});
      if (foreground) {
        const float right = ring.width - px, bottom = ring.height - py;
        const float nearest = std::min({px, py, right, bottom});
        const float distance = nearest == py ? px : nearest == right ? ring.width + py
            : nearest == bottom ? ring.width + ring.height + right : 2 * ring.width + ring.height + bottom;
        const float progress = distance / (2 * (ring.width + ring.height));
        const auto slot = pair % count;
        const auto jump = slot % 2 == 0 ? slot / 2 : count - 1 - slot / 2;
        const float offset = randomized ? randomSeed[3] : hash(19, 73, -2);
        const float start = randomized ? randomSeed[0] : hash(31, 97, -3);
        const auto segment = static_cast<int64_t>(std::floor(
            fract(progress * static_cast<float>(settings.paletteRepeats.value_or(1.0))) * count
            + offset + layout * settings.boundaryJumpFraction.value_or(0.4)));
        color = palette[(segment + static_cast<int64_t>(start * count) + jump + layout * (count / 2)) % count];
      }
      const float alpha = color[3] * coverage;
      const auto channel = [](float value) { return static_cast<uint32_t>(std::lround(value * 255.0F)); };
      return channel(alpha) << 24 | channel(color[0] * alpha) << 16 | channel(color[1] * alpha) << 8
          | channel(color[2] * alpha);
    }

    void generate(const Ring& geometry, int64_t tick, bool fading) {
      const int left = std::clamp(static_cast<int>(std::ceil(geometry.x)) +
          static_cast<int>(std::ceil(std::max(geometry.inner[0], geometry.inner[2]))) + 1, 0, geometry.width);
      const int right = std::clamp(static_cast<int>(std::floor(geometry.x + geometry.holeWidth)) -
          static_cast<int>(std::ceil(std::max(geometry.inner[1], geometry.inner[3]))) - 1, left, geometry.width);
      const int top = std::clamp(static_cast<int>(std::ceil(geometry.y)) + 1, 0, geometry.height);
      const int bottom = std::clamp(static_cast<int>(std::floor(geometry.y + geometry.holeHeight)) - 1, top, geometry.height);
      const std::array<wlr_box, 4> boxes{{
          {0, 0, geometry.width, top}, {0, bottom, geometry.width, geometry.height - bottom},
          {0, top, left, bottom - top}, {right, top, geometry.width - right, bottom - top},
      }};
      const bool reuseOld = fading && ring && ring->key() == geometry.key() && generatedTick == tick - 1;
      clearTextures();
      for (size_t i = 0; i < strips.size(); ++i) {
        auto& strip = strips[i];
        const auto& box = boxes[i];
        strip.box = box;
        strip.oldPixels.clear();
        if (reuseOld) strip.oldPixels.swap(strip.pixels);
        strip.pixels.clear();
        strip.pixels.reserve(static_cast<size_t>(box.width) * box.height);
        for (int y = 0; y < box.height; ++y) {
          for (int x = 0; x < box.width; ++x) {
            strip.pixels.push_back(pixel(geometry, box.x + x, box.y + y, tick));
            if (fading && !reuseOld) strip.oldPixels.push_back(pixel(geometry, box.x + x, box.y + y, tick - 1));
          }
        }
      }
      ring = geometry;
      generatedTick = tick;
    }

    bool upload() {
      clearTextures();
      for (auto& strip : strips) {
        if (strip.pixels.empty()) continue;
        // Interleave old and new texels along the short axis. A subtexel
        // source offset makes the normal GPU bilinear sampler blend the pair.
        const bool paired = !strip.oldPixels.empty();
        const bool pairX = paired && strip.box.width <= strip.box.height;
        const bool pairY = paired && !pairX;
        strip.pairAxis = pairX ? 1 : pairY ? 2 : 0;
        auto* image = cairo_image_surface_create(
            CAIRO_FORMAT_ARGB32, strip.box.width * (pairX ? 2 : 1), strip.box.height * (pairY ? 2 : 1));
        if (cairo_surface_status(image) != CAIRO_STATUS_SUCCESS) {
          cairo_surface_destroy(image);
          return false;
        }
        auto* data = cairo_image_surface_get_data(image);
        const int stride = cairo_image_surface_get_stride(image) / sizeof(uint32_t);
        auto* pixels = reinterpret_cast<uint32_t*>(data);
        size_t i = 0;
        for (int y = 0; y < strip.box.height; ++y) {
          for (int x = 0; x < strip.box.width; ++x, ++i) {
            const int offset = y * (pairY ? 2 : 1) * stride + x * (pairX ? 2 : 1);
            if (paired) pixels[offset] = strip.oldPixels[i];
            pixels[offset + (pairX ? 1 : pairY ? stride : 0)] = strip.pixels[i];
          }
        }
        cairo_surface_mark_dirty(image);
        auto* buffer = createCairoBuffer(image);
        strip.texture = wlr_texture_from_buffer(renderer, buffer);
        wlr_buffer_drop(buffer);
        if (strip.texture == nullptr) return false;
      }
      uploaded = true;
      return true;
    }
  };

  ChalkBorder::ChalkBorder(wl_event_loop* loop, std::function<bool()> damage)
      : m_state(std::make_unique<State>(loop, std::move(damage))) {}

  ChalkBorder::~ChalkBorder() = default;

  void ChalkBorder::setOpacity(float opacity) { m_state->opacity = opacity; }

  void ChalkBorder::inheritPhase(const ChalkBorder& source) {
    m_state->epoch = source.m_state->epoch;
    m_state->randomSeed = source.m_state->randomSeed;
    m_state->generatedTick = -1;
  }


  bool ChalkBorder::configure(ChalkSettings settings, bool enabled) {
    auto& state = *m_state;
    if (state.enabled == enabled && state.settings == settings) return false;
    if (!state.enabled && enabled) state.epoch = Clock::now();
    state.enabled = enabled;
    state.settings = std::move(settings);
    state.generatedTick = -1;
    state.clearTextures();
    if (state.timer != nullptr) wl_event_source_timer_update(state.timer, 0);
    return true;
  }

  bool ChalkBorder::draw(void* data, fx_gles_render_pass* pass, wlr_renderer* renderer,
                         const wlr_scene_border_pattern* border) {
    auto& state = *static_cast<ChalkBorder*>(data)->m_state;
    const auto& box = border->box;
    const auto& hole = border->hole;
    Ring ring{box.width, box.height, static_cast<float>(hole.x - box.x), static_cast<float>(hole.y - box.y),
              static_cast<float>(hole.width), static_cast<float>(hole.height), border->thickness, {}, {}};
    std::ranges::copy(border->inner_radii, ring.inner.begin());
    std::ranges::copy(border->outer_radii, ring.outer.begin());
    if (!state.enabled || ring.width <= 0 || ring.height <= 0 || ring.thickness <= 0) return false;

    if (state.renderer != renderer || wl_list_empty(&state.watch.listener.link)) {
      state.clearTextures();
      wl_list_remove(&state.watch.listener.link);
      wl_signal_add(&renderer->events.destroy, &state.watch.listener);
      state.renderer = renderer;
    }
    const double interval = state.settings.secondsPerUpdate.value_or(8.0);
    const double fade = std::min(interval, state.settings.fadeSeconds.value_or(2.0));
    const int fps = state.settings.fadeFps.value_or(12);
    const double seconds = std::chrono::duration<double>(Clock::now() - state.epoch).count();
    const auto tick = static_cast<int64_t>(std::floor(seconds / interval));
    const double elapsed = seconds - tick * interval;
    const bool fading = tick > 0 && elapsed < fade;
    const auto step = fading ? static_cast<int64_t>(std::floor(elapsed * fps)) : -2;
    const double next = fading ? std::min(fade, (step + 1.0) / fps) : interval;
    if (state.timer != nullptr) {
      wl_event_source_timer_update(state.timer, std::max(1, static_cast<int>(std::ceil((next - elapsed) * 1000))));
    }
    if (!state.ring || state.ring->key() != ring.key() || state.generatedTick != tick) {
      state.generate(ring, tick, fading);
    }
    if (!state.uploaded && !state.upload()) return false;
    const double t = fading ? step / (fps * fade) : 1.0;
    const double blend = t * t * (3 - 2 * t);
    for (const auto& strip : state.strips) {
      if (strip.texture == nullptr) continue;
      const wlr_box dst{box.x + strip.box.x, box.y + strip.box.y, strip.box.width, strip.box.height};
      wlr_scene_border_draw_strip(pass, strip.texture, &dst, border->clip, state.opacity, strip.pairAxis, blend);
    }
    return true;
  }
}
