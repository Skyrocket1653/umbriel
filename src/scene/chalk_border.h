#pragma once

#include "config/chalk.h"

#include <cstdint>
#include <functional>
#include <memory>

struct fx_gles_render_pass;
struct fx_render_border_options;
struct wl_event_loop;
struct wlr_renderer;

namespace umbriel {
  struct Config;
  void prepareChalkMasks(wlr_renderer* renderer, const Config& config);

  class ChalkBorder {
  public:
    ChalkBorder(wl_event_loop* loop, std::function<bool()> damage);
    ~ChalkBorder();

    void setOpacity(float opacity);
    bool opaque() const;
    void inheritPhase(const ChalkBorder& source);
    bool configure(ChalkSettings settings, bool enabled);
    static bool
    draw(void* data, fx_gles_render_pass* pass, wlr_renderer* renderer, const fx_render_border_options* border);

  private:
    struct State;
    std::unique_ptr<State> m_state;
  };
} // namespace umbriel
