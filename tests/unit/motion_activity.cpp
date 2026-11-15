#include "input/motion_activity.h"

#include "check.h"

#include <limits>

using umbriel::MotionActivity;
using umbriel::MouseActivityOverride;
using umbriel::MouseActivitySettings;

UMBRIEL_TEST(zeroThresholdPreservesEveryMotionEvent) {
  MotionActivity motion;
  const MouseActivitySettings settings;
  CHECK(motion.update(0, 0, false, 0, settings));
  CHECK(motion.update(0.001, 0, false, 1, settings));
  CHECK(motion.update(50, 60, true, 2, settings));
}

UMBRIEL_TEST(netDisplacementMustExceedThresholdWithinWindow) {
  MotionActivity motion;
  const MouseActivitySettings settings{.thresholdPx = 5, .windowMs = 250};
  CHECK(!motion.update(3, 0, false, 0, settings));
  CHECK(!motion.update(2, 0, false, 100, settings));
  CHECK(motion.update(0.1, 0, false, 200, settings));
  CHECK(!motion.update(1, 0, false, 201, settings));
}

UMBRIEL_TEST(opposingMicroMovementsDoNotAccumulateTravel) {
  MotionActivity motion;
  const MouseActivitySettings settings{.thresholdPx = 5};
  for (uint32_t time = 0; time < 10000; ++time) {
    CHECK(!motion.update(time % 2 == 0 ? 0.5 : -0.5, 0, false, time, settings));
  }
}

UMBRIEL_TEST(expiredMovementDoesNotResetIdleOrWake) {
  MotionActivity motion;
  const MouseActivitySettings settings{.thresholdPx = 5, .windowMs = 250};
  CHECK(!motion.update(3, 0, false, 0, settings));
  CHECK(!motion.update(3, 0, false, 250, settings));
  CHECK(motion.update(3, 0, false, 251, settings));
}

UMBRIEL_TEST(diagonalDisplacementUsesEuclideanDistance) {
  MotionActivity motion;
  const MouseActivitySettings settings{.thresholdPx = 5};
  CHECK(!motion.update(3, 4, false, 0, settings));
  CHECK(motion.update(0.1, 0, false, 1, settings));
}

UMBRIEL_TEST(disabledStateNeverCountsMotion) {
  MotionActivity motion;
  MouseActivitySettings settings{.enabled = false};
  CHECK(!motion.update(100, 0, false, 0, settings));
  CHECK(!motion.update(200, 300, true, 1, settings));
  settings.enabled = true;
  settings.thresholdPx = 5;
  CHECK(!motion.update(1, 0, false, 2, settings));
}

UMBRIEL_TEST(absoluteInputUsesDevicePositionsAndFirstEventIsBaseline) {
  MotionActivity motion;
  const MouseActivitySettings settings{.thresholdPx = 5};
  CHECK(!motion.update(100, 200, true, 0, settings));
  CHECK(!motion.update(103, 204, true, 1, settings));
  CHECK(motion.update(104, 204, true, 2, settings));
}

UMBRIEL_TEST(configurationAndPowerStateChangesDiscardPendingMovement) {
  MotionActivity motion;
  MouseActivitySettings settings{.thresholdPx = 5};
  CHECK(!motion.update(4, 0, false, 0, settings));
  settings.windowMs = 300;
  CHECK(!motion.update(2, 0, false, 1, settings));
  settings.thresholdPx = 6;
  CHECK(!motion.update(5, 0, false, 2, settings));
  // The compositor clears each device's accumulator on a successful DPMS transition.
  motion = MotionActivity{};
  CHECK(!motion.update(2, 0, false, 3, settings));
}

UMBRIEL_TEST(deviceAccumulatorsAndInputModesStayIndependent) {
  MotionActivity first;
  MotionActivity second;
  const MouseActivitySettings settings{.thresholdPx = 5};
  CHECK(!first.update(4, 0, false, 0, settings));
  CHECK(!second.update(2, 0, false, 1, settings));
  CHECK(!first.update(100, 200, true, 2, settings));
  CHECK(!first.update(102, 200, true, 3, settings));
  CHECK(second.update(4, 0, false, 4, settings));
}

UMBRIEL_TEST(eventTimestampWrapPreservesWindowExpiry) {
  MotionActivity motion;
  const MouseActivitySettings settings{.thresholdPx = 5, .windowMs = 250};
  CHECK(!motion.update(3, 0, false, std::numeric_limits<uint32_t>::max() - 100, settings));
  CHECK(motion.update(3, 0, false, 50, settings));
  CHECK(!motion.update(3, 0, false, 60, settings));
  CHECK(!motion.update(3, 0, false, 310, settings));
}

UMBRIEL_TEST(nonfiniteInputClearsPendingDisplacement) {
  MotionActivity motion;
  const MouseActivitySettings settings{.thresholdPx = 5};
  CHECK(!motion.update(4, 0, false, 0, settings));
  CHECK(!motion.update(std::numeric_limits<double>::quiet_NaN(), 0, false, 1, settings));
  CHECK(!motion.update(2, 0, false, 2, settings));
  CHECK(!motion.update(0, std::numeric_limits<double>::infinity(), false, 3, settings));
  CHECK(!motion.update(4, 0, false, 4, settings));
}

UMBRIEL_TEST(deviceOverridesInheritEachFieldAndPreserveFalseAndZero) {
  const MouseActivitySettings defaults{.enabled = false, .thresholdPx = 5, .windowMs = 300};
  MouseActivityOverride override;
  CHECK(override.resolve(defaults) == defaults);
  override.thresholdPx = 8;
  CHECK(!override.resolve(defaults).enabled);
  CHECK_EQ(override.resolve(defaults).thresholdPx, 8);
  CHECK_EQ(override.resolve(defaults).windowMs, 300);
  override.enabled = false;
  override.thresholdPx = 0;
  override.windowMs = 1;
  const auto resolved = override.resolve({.enabled = true, .thresholdPx = 10, .windowMs = 250});
  CHECK(!resolved.enabled);
  CHECK_EQ(resolved.thresholdPx, 0);
  CHECK_EQ(resolved.windowMs, 1);
}

int main() { return RUN_TESTS(); }
