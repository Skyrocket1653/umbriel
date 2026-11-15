#pragma once

#include "config/mouse_activity.h"

#include <cmath>
#include <cstdint>
#include <optional>

namespace umbriel {

  // Accumulate net displacement within one bounded interval, independently of cursor warps and window multipliers.
  class MotionActivity {
  public:
    bool update(double x, double y, bool absolute, uint32_t time, const MouseActivitySettings& settings) {
      if (settings != m_settings || absolute != m_absolute) {
        *this = MotionActivity{};
        m_settings = settings;
        m_absolute = absolute;
      }
      if (!settings.enabled) {
        return false;
      }
      if (!std::isfinite(x) || !std::isfinite(y)) {
        m_start.reset();
        m_lastX.reset();
        return false;
      }
      double dx = x;
      double dy = y;
      if (absolute) {
        dx = m_lastX ? x - *m_lastX : 0.0;
        dy = m_lastX ? y - m_lastY : 0.0;
        m_lastX = x;
        m_lastY = y;
      }
      if (settings.thresholdPx == 0.0) {
        return true;
      }
      if (!m_start || time - *m_start >= static_cast<uint32_t>(settings.windowMs)) {
        m_start = time;
        m_x = 0;
        m_y = 0;
      }
      m_x += dx;
      m_y += dy;
      if (std::hypot(m_x, m_y) <= settings.thresholdPx) {
        return false;
      }
      m_start.reset();
      return true;
    }

  private:
    MouseActivitySettings m_settings;
    bool m_absolute = false;
    std::optional<uint32_t> m_start;
    double m_x = 0;
    double m_y = 0;
    std::optional<double> m_lastX;
    double m_lastY = 0;
  };

} // namespace umbriel
