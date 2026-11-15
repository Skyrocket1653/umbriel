#pragma once

#include <optional>

namespace umbriel {

  struct MouseActivitySettings {
    bool enabled = true;
    double thresholdPx = 0.0;
    int windowMs = 250;
    bool operator==(const MouseActivitySettings&) const = default;
  };

  struct MouseActivityOverride {
    std::optional<bool> enabled;
    std::optional<double> thresholdPx;
    std::optional<int> windowMs;

    MouseActivitySettings resolve(const MouseActivitySettings& defaults) const {
      return {
          .enabled = enabled.value_or(defaults.enabled),
          .thresholdPx = thresholdPx.value_or(defaults.thresholdPx),
          .windowMs = windowMs.value_or(defaults.windowMs),
      };
    }

    bool operator==(const MouseActivityOverride&) const = default;
  };

  struct MouseActivity {
    MouseActivitySettings sleep;
    MouseActivitySettings wake;
    bool operator==(const MouseActivity&) const = default;
  };

  struct MouseActivityOverrides {
    MouseActivityOverride sleep;
    MouseActivityOverride wake;
    bool operator==(const MouseActivityOverrides&) const = default;
  };

} // namespace umbriel
