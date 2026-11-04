#include "config/chalk.h"

#include "check.h"

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

int main() { return RUN_TESTS(); }
