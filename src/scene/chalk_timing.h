#pragma once

#include <algorithm>
#include <cstdint>

namespace umbriel {
  struct ChalkTicks {
    int64_t pixels = 0;
    int64_t segments = 0;
    bool operator==(const ChalkTicks&) const = default;
  };

  struct ChalkMaskFrame {
    int row;
    bool inverted;
    bool alternate;
  };

  // Each base row appears once normally and once inverted in a complete set.
  constexpr ChalkMaskFrame chalkMaskFrame(int64_t tick, int sets, int patternsPerSet) {
    const int half = patternsPerSet / 2;
    const int set = static_cast<int>((tick / patternsPerSet) % sets);
    const int step = static_cast<int>(tick % patternsPerSet);
    return {set * half + step % half, step >= half, tick % 2 != 0};
  }

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
        current,
        {before / pixels, before / segments},
        update,
        std::min((current.pixels + 1) * pixels, (current.segments + 1) * segments),
    };
  }
} // namespace umbriel
