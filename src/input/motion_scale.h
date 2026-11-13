#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

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

  // Retain the fraction that cannot be represented in this event's 24.8 protocol value for the next event.
  class RelativeMotionRemainder {
  public:
    ScaledMotion encode(const ScaledMotion& motion, double multiplier) {
      if (m_multiplier != multiplier || motion.issue != MotionScaleIssue::None) {
        m_x = 0;
        m_y = 0;
      }
      m_multiplier = multiplier;
      const ScaledMotion encoded{
          .dx = quantize(motion.dx, m_x),
          .dy = quantize(motion.dy, m_y),
          .issue = motion.issue,
      };
      if (motion.issue != MotionScaleIssue::None) {
        m_x = 0;
        m_y = 0;
      }
      return encoded;
    }

    void reset() {
      m_x = 0;
      m_y = 0;
      m_multiplier.reset();
    }

  private:
    static double quantize(double delta, double& remainder) {
      // An idle axis must not emit motion from a rounding remainder alone.
      if (delta == 0.0) {
        return 0.0;
      }
      const double accumulated = delta + remainder;
      const double encoded = std::clamp(std::round(accumulated * 256.0) / 256.0, kMinPointerDelta, kMaxPointerDelta);
      remainder = accumulated < kMinPointerDelta || accumulated > kMaxPointerDelta ? 0.0 : accumulated - encoded;
      return encoded;
    }

    double m_x = 0;
    double m_y = 0;
    std::optional<double> m_multiplier;
  };

  struct ScaledWheelMotion {
    double delta = 0;
    int32_t value120 = 0;
    MotionScaleIssue issue = MotionScaleIssue::None;
  };

  class WheelMotionRemainder {
  public:
    ScaledWheelMotion scale(double delta, int32_t value120, double multiplier) {
      const bool moving = delta != 0.0 || value120 != 0;
      const bool discreteMode = value120 != 0;
      if (m_multiplier != multiplier || (moving && m_discreteMode != discreteMode)) {
        reset();
      }
      m_multiplier = multiplier;
      if (moving) {
        m_discreteMode = discreteMode;
      }
      const auto continuous = m_continuous.encode(scalePointerMotion(delta, 0.0, multiplier), multiplier);
      // Map one value120 unit to one protocol quantum, so the same encoder retains fractions of integer counts.
      const auto discrete =
          m_discrete.encode(scalePointerMotion(static_cast<double>(value120) / 256.0, 0.0, multiplier), multiplier);
      auto issue = std::max(continuous.issue, discrete.issue);
      // wlroots adds up to 119 carried value120 units before converting to legacy whole notches.
      constexpr int64_t minimum = static_cast<int64_t>(std::numeric_limits<int32_t>::min()) + 119;
      constexpr int64_t maximum = static_cast<int64_t>(std::numeric_limits<int32_t>::max()) - 119;
      const int64_t pending = static_cast<int64_t>(m_pendingValue120) + static_cast<int64_t>(discrete.dx * 256.0);
      m_pendingValue120 = static_cast<int32_t>(std::clamp(pending, minimum, maximum));
      if (pending < minimum || pending > maximum) {
        issue = std::max(issue, MotionScaleIssue::OutOfRange);
        m_discrete.reset();
      }
      const double accumulatedDelta = m_pendingDelta + continuous.dx;
      m_pendingDelta = std::clamp(accumulatedDelta, kMinPointerDelta, kMaxPointerDelta);
      if (accumulatedDelta != m_pendingDelta) {
        issue = std::max(issue, MotionScaleIssue::OutOfRange);
      }
      // Keep the two representations together: wlroots emits value120 only alongside an axis value, and older
      // clients accumulate discrete counts into whole notches rather than accepting alternating smooth events.
      ScaledWheelMotion result{.issue = issue};
      if (!m_discreteMode.value_or(false) || (m_pendingValue120 != 0 && m_pendingDelta != 0.0)) {
        result.delta = m_pendingDelta;
        result.value120 = m_pendingValue120;
        m_pendingDelta = 0;
        m_pendingValue120 = 0;
      }
      if (issue != MotionScaleIssue::None) {
        reset();
      }
      return result;
    }

    void reset() {
      m_continuous.reset();
      m_discrete.reset();
      m_pendingValue120 = 0;
      m_pendingDelta = 0;
      m_multiplier.reset();
      m_discreteMode.reset();
    }

  private:
    RelativeMotionRemainder m_continuous;
    RelativeMotionRemainder m_discrete;
    int32_t m_pendingValue120 = 0;
    double m_pendingDelta = 0;
    std::optional<double> m_multiplier;
    std::optional<bool> m_discreteMode;
  };

} // namespace umbriel
