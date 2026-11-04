#pragma once

#include <array>
#include <optional>
#include <vector>

namespace umbriel {
  using ChalkColor = std::array<float, 4>;
  enum class BorderStyle { Off, Solid, Chalk };
  enum class BorderPlacement { Outside, Inset };

  struct ChalkSettings {
    std::optional<double> pixelSecondsPerUpdate, segmentSecondsPerUpdate;
    std::optional<double> secondsPerUpdate, fadeSeconds, boundaryJumpFraction, cellPx, paletteRepeats;
    std::optional<int> fadeFps, alwaysFlipSpacing;
    std::optional<bool> randomizeInitialPattern;
    std::optional<ChalkColor> background;
    std::optional<std::vector<ChalkColor>> foreground;

    void overlay(const ChalkSettings& other) {
      if (other.secondsPerUpdate) {
        secondsPerUpdate = other.secondsPerUpdate;
        pixelSecondsPerUpdate.reset();
        segmentSecondsPerUpdate.reset();
      }
      if (other.pixelSecondsPerUpdate) {
        pixelSecondsPerUpdate = other.pixelSecondsPerUpdate;
      }
      if (other.segmentSecondsPerUpdate) {
        segmentSecondsPerUpdate = other.segmentSecondsPerUpdate;
      }
      if (other.fadeSeconds) {
        fadeSeconds = other.fadeSeconds;
      }
      if (other.boundaryJumpFraction) {
        boundaryJumpFraction = other.boundaryJumpFraction;
      }
      if (other.cellPx) {
        cellPx = other.cellPx;
      }
      if (other.paletteRepeats) {
        paletteRepeats = other.paletteRepeats;
      }
      if (other.fadeFps) {
        fadeFps = other.fadeFps;
      }
      if (other.alwaysFlipSpacing) {
        alwaysFlipSpacing = other.alwaysFlipSpacing;
      }
      if (other.randomizeInitialPattern) {
        randomizeInitialPattern = other.randomizeInitialPattern;
      }
      if (other.background) {
        background = other.background;
      }
      if (other.foreground) {
        foreground = other.foreground;
      }
    }

    bool operator==(const ChalkSettings&) const = default;
  };

  struct ScreenBorderState {
    BorderStyle style = BorderStyle::Off;
    int width = 4;
    ChalkColor color{0.878F, 0.333F, 0.380F, 1.0F};
    ChalkSettings chalk;
    ChalkSettings unfocusedChalk;
    bool operator==(const ScreenBorderState&) const = default;
  };

  struct ScreenInnerBorder {
    bool inCapture = false;
    ScreenBorderState whenCasting;
    ScreenBorderState whenNotCasting;
    bool operator==(const ScreenInnerBorder&) const = default;
  };
} // namespace umbriel
