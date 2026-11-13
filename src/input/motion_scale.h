#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace umbriel {

  enum class MotionScaleIssue : uint8_t { None, OutOfRange, NonFinite };

  struct ScaledMotion {
    double dx = 0;
    double dy = 0;
    MotionScaleIssue issue = MotionScaleIssue::None;
  };

  // Wayland motion uses signed 24.8 fixed point. Keep even finite products representable before wlroots encodes them.
  inline constexpr double kMinPointerDelta = static_cast<double>(std::numeric_limits<int32_t>::min()) / 256.0;
  inline constexpr double kMaxPointerDelta = static_cast<double>(std::numeric_limits<int32_t>::max()) / 256.0;

  inline ScaledMotion scalePointerMotion(double dx, double dy, double factor) {
    ScaledMotion motion;
    const auto scale = [&](double delta) {
      const double result = delta * factor;
      if (!std::isfinite(result)) {
        motion.issue = MotionScaleIssue::NonFinite;
        return 0.0;
      }
      if (result < kMinPointerDelta || result > kMaxPointerDelta) {
        motion.issue = std::max(motion.issue, MotionScaleIssue::OutOfRange);
        return std::clamp(result, kMinPointerDelta, kMaxPointerDelta);
      }
      return result;
    };
    motion.dx = scale(dx);
    motion.dy = scale(dy);
    return motion;
  }

  struct ScaledPointerInput {
    ScaledMotion pointer;
    ScaledMotion raw;
  };

  // Accelerated relative motion and cursor movement share one result. Only unaccelerated input uses rawFactor.
  inline ScaledPointerInput
  scalePointerInput(double dx, double dy, double unaccelDx, double unaccelDy, double pointerFactor, double rawFactor) {
    return {
        .pointer = scalePointerMotion(dx, dy, pointerFactor),
        .raw = scalePointerMotion(unaccelDx, unaccelDy, rawFactor),
    };
  }

  struct ScaledWheelMotion {
    double delta = 0;
    int32_t value120 = 0;
    MotionScaleIssue issue = MotionScaleIssue::None;
  };

  inline ScaledWheelMotion scaleWheelMotion(double delta, int32_t value120, double factor) {
    const auto continuous = scalePointerMotion(delta, 0.0, factor);
    ScaledWheelMotion motion{.delta = continuous.dx, .issue = continuous.issue};
    const double discrete = static_cast<double>(value120) * factor;
    if (!std::isfinite(discrete)) {
      motion.issue = MotionScaleIssue::NonFinite;
      return motion;
    }
    // wlroots carries up to 119 value120 units when converting to legacy whole notches.
    constexpr double minimum = std::numeric_limits<int32_t>::min() + 119.0;
    constexpr double maximum = std::numeric_limits<int32_t>::max() - 119.0;
    if (discrete < minimum || discrete > maximum) {
      motion.issue = std::max(motion.issue, MotionScaleIssue::OutOfRange);
    }
    motion.value120 = static_cast<int32_t>(std::round(std::clamp(discrete, minimum, maximum)));
    return motion;
  }

} // namespace umbriel
