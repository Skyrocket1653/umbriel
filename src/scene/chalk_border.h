#pragma once

#include "config/chalk.h"

#include <cstdint>
#include <functional>
#include <memory>

struct fx_gles_render_pass;
struct wlr_scene_border_pattern;
struct wl_event_loop;
struct wlr_renderer;

namespace umbriel {
  class ChalkBorder {
  public:
    ChalkBorder(wl_event_loop* loop, std::function<bool()> damage);
    ~ChalkBorder();

    void setOpacity(float opacity);
    void inheritPhase(const ChalkBorder& source);
    bool configure(ChalkSettings settings, bool enabled);
    static bool draw(
        void* data, fx_gles_render_pass* pass, wlr_renderer* renderer, const wlr_scene_border_pattern* border
    );

  private:
    struct State;
    std::unique_ptr<State> m_state;
  };
}
