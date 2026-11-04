#include "config/chalk.h"

#include "check.h"
#include "scene/chalk_timing.h"

using umbriel::ChalkColor;
using umbriel::ChalkSettings;

UMBRIEL_TEST(defaultChalkPaletteIsOpaque) { CHECK(ChalkSettings{}.opaque()); }

UMBRIEL_TEST(transparentBackgroundExposesTitleFillEvenWithOpaqueForeground) {
  ChalkSettings settings;
  settings.background = ChalkColor{0, 0, 0, 0};
  settings.foreground = std::vector<ChalkColor>{{1, 1, 1, 1}};
  CHECK(!settings.opaque());
  settings.background = ChalkColor{0, 0, 0, 0.5F};
  CHECK(!settings.opaque());
}

UMBRIEL_TEST(everyForegroundColorMustBeOpaqueToHideTitleFill) {
  ChalkSettings settings;
  settings.foreground = std::vector<ChalkColor>{{1, 0, 0, 1}, {0, 1, 0, 1}};
  CHECK(settings.opaque());
  settings.foreground->push_back({0, 0, 1, 0.5F});
  CHECK(!settings.opaque());
  settings.foreground->back()[3] = 0;
  CHECK(!settings.opaque());
}

UMBRIEL_TEST(windowPaletteOverridesDetermineTitleFillCoverage) {
  ChalkSettings settings;
  settings.background = ChalkColor{0, 0, 0, 1};
  ChalkSettings override;
  override.background = ChalkColor{0, 0, 0, 0};
  settings.overlay(override);
  CHECK(!settings.opaque());
  override.background = ChalkColor{0, 0, 0, 1};
  settings.overlay(override);
  CHECK(settings.opaque());
}

UMBRIEL_TEST(chalkSetsPairEveryMaskWithItsInverseAndWrap) {
  for (int sets = 1; sets <= 16; ++sets) {
    for (int patterns = 2; patterns <= 16; patterns += 2) {
      const int half = patterns / 2;
      for (int set = 0; set < sets; ++set) {
        for (int step = 0; step < half; ++step) {
          const int tick = set * patterns + step;
          const auto first = umbriel::chalkMaskFrame(tick, sets, patterns);
          const auto inverse = umbriel::chalkMaskFrame(tick + half, sets, patterns);
          const auto repeated = umbriel::chalkMaskFrame(tick + sets * patterns, sets, patterns);
          CHECK(first.row == set * half + step);
          CHECK(inverse.row == first.row);
          CHECK(!first.inverted && inverse.inverted);
          CHECK(repeated.row == first.row && repeated.inverted == first.inverted);
        }
        for (int step = 0; step < patterns; ++step) {
          CHECK(umbriel::chalkMaskFrame(set * patterns + step, sets, patterns).alternate == (step % 2 != 0));
        }
      }
    }
  }
}

int main() { return RUN_TESTS(); }
