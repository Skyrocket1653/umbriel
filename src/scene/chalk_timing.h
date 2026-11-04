#pragma once

#include <algorithm>
#include <cstdint>

namespace umbriel {
  struct ChalkTicks {
    int64_t pixels = 0;
    int64_t segments = 0;
    bool operator==(const ChalkTicks&) const = default;
  };

  struct ChalkTiming {
    ChalkTicks current;
    ChalkTicks previous;
    int64_t update;
    int64_t nextUpdate;
  };

  // All intervals are positive milliseconds on the same clock. Coincident
  // boundaries produce one transition that advances both pattern components.
  constexpr ChalkTiming chalkTiming(int64_t elapsed, int64_t pixels, int64_t segments) {
    const ChalkTicks current{elapsed / pixels, elapsed / segments};
    const auto update = std::max(current.pixels * pixels, current.segments * segments);
    const auto before = std::max<int64_t>(0, update - 1);
    return {
        current, {before / pixels, before / segments}, update,
        std::min((current.pixels + 1) * pixels, (current.segments + 1) * segments),
    };
  }
}
