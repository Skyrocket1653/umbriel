#include "input/motion_scale.h"

#include "check.h"

#include <limits>

using umbriel::MotionScaleIssue;
using umbriel::scalePointerInput;
using umbriel::scalePointerMotion;
using umbriel::scaleWheelMotion;

UMBRIEL_TEST(rawMultiplierDoesNotScaleAcceleratedPointerInput) {
  const auto normal = scalePointerInput(10.0, -20.0, 100.0, -200.0, 1.0, 1.0);
  const auto slowRaw = scalePointerInput(10.0, -20.0, 100.0, -200.0, 1.0, 0.01);
  CHECK_EQ(slowRaw.pointer.issue, MotionScaleIssue::None);
  CHECK_EQ(slowRaw.pointer.dx, 10.0);
  CHECK_EQ(slowRaw.pointer.dy, -20.0);
  CHECK_EQ(slowRaw.pointer.dx, normal.pointer.dx);
  CHECK_EQ(slowRaw.pointer.dy, normal.pointer.dy);
  CHECK_EQ(slowRaw.raw.dx, 1.0);
  CHECK_EQ(slowRaw.raw.dy, -2.0);
}

UMBRIEL_TEST(pointerMultiplierDoesNotScaleRawInput) {
  const auto normal = scalePointerInput(10.0, -20.0, 100.0, -200.0, 1.0, 0.5);
  const auto fastPointer = scalePointerInput(10.0, -20.0, 100.0, -200.0, 2.0, 0.5);
  CHECK_EQ(fastPointer.pointer.dx, 20.0);
  CHECK_EQ(fastPointer.pointer.dy, -40.0);
  CHECK_EQ(fastPointer.raw.issue, MotionScaleIssue::None);
  CHECK_EQ(fastPointer.raw.dx, normal.raw.dx);
  CHECK_EQ(fastPointer.raw.dy, normal.raw.dy);
  CHECK_EQ(fastPointer.raw.dx, 50.0);
  CHECK_EQ(fastPointer.raw.dy, -100.0);
}

UMBRIEL_TEST(invalidRawMotionDoesNotDiscardValidPointerMotion) {
  const auto motion = scalePointerInput(10.0, -20.0, std::numeric_limits<double>::max(), 2.0, 1.0, 2.0);
  CHECK_EQ(motion.pointer.issue, MotionScaleIssue::None);
  CHECK_EQ(motion.pointer.dx, 10.0);
  CHECK_EQ(motion.pointer.dy, -20.0);
  CHECK_EQ(motion.raw.issue, MotionScaleIssue::NonFinite);
  CHECK_EQ(motion.raw.dx, 0.0);
  CHECK_EQ(motion.raw.dy, 4.0);
}

UMBRIEL_TEST(sensitivityAllowsSmallLargeZeroAndNegativeMultipliers) {
  const auto small = scalePointerMotion(10.0, -20.0, 0.001);
  CHECK_EQ(small.issue, MotionScaleIssue::None);
  CHECK_NEAR(small.dx, 0.01, 1e-12);
  CHECK_NEAR(small.dy, -0.02, 1e-12);
  const auto large = scalePointerMotion(10.0, -20.0, 100.0);
  CHECK_EQ(large.issue, MotionScaleIssue::None);
  CHECK_EQ(large.dx, 1000.0);
  CHECK_EQ(large.dy, -2000.0);
  const auto zero = scalePointerMotion(10.0, -20.0, 0.0);
  CHECK_EQ(zero.issue, MotionScaleIssue::None);
  CHECK_EQ(zero.dx, 0.0);
  CHECK_EQ(zero.dy, 0.0);
  const auto reversed = scalePointerMotion(10.0, -20.0, -2.0);
  CHECK_EQ(reversed.issue, MotionScaleIssue::None);
  CHECK_EQ(reversed.dx, -20.0);
  CHECK_EQ(reversed.dy, 40.0);
}

UMBRIEL_TEST(multiplicationOverflowDiscardsOnlyTheNonFiniteComponent) {
  const double huge = std::numeric_limits<double>::max();
  const auto positive = scalePointerMotion(huge, 2.0, 2.0);
  CHECK_EQ(positive.issue, MotionScaleIssue::NonFinite);
  CHECK_EQ(positive.dx, 0.0);
  CHECK_EQ(positive.dy, 4.0);
  const auto negative = scalePointerMotion(2.0, -huge, 2.0);
  CHECK_EQ(negative.issue, MotionScaleIssue::NonFinite);
  CHECK_EQ(negative.dx, 4.0);
  CHECK_EQ(negative.dy, 0.0);
}

UMBRIEL_TEST(aHugeMultiplierCanStillProduceOrdinaryMotion) {
  const auto motion = scalePointerMotion(1e-300, -1e-300, 1e300);
  CHECK_EQ(motion.issue, MotionScaleIssue::None);
  CHECK_NEAR(motion.dx, 1.0, 1e-12);
  CHECK_NEAR(motion.dy, -1.0, 1e-12);
}

UMBRIEL_TEST(nonFiniteInputCannotReachMotionConsumers) {
  for (const double invalid :
       {std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::quiet_NaN()}) {
    const auto motion = scalePointerMotion(invalid, 2.0, 1.0);
    CHECK_EQ(motion.issue, MotionScaleIssue::NonFinite);
    CHECK_EQ(motion.dx, 0.0);
    CHECK_EQ(motion.dy, 2.0);
  }
}

UMBRIEL_TEST(finiteMotionIsLimitedOnlyAtTheWaylandEncodingBoundary) {
  const auto boundary = scalePointerMotion(umbriel::kMaxPointerDelta, umbriel::kMinPointerDelta, 1.0);
  CHECK_EQ(boundary.issue, MotionScaleIssue::None);
  CHECK_EQ(boundary.dx, umbriel::kMaxPointerDelta);
  CHECK_EQ(boundary.dy, umbriel::kMinPointerDelta);
  const auto oversized = scalePointerMotion(1e100, -1e100, 2.0);
  CHECK_EQ(oversized.issue, MotionScaleIssue::OutOfRange);
  CHECK_EQ(oversized.dx, umbriel::kMaxPointerDelta);
  CHECK_EQ(oversized.dy, umbriel::kMinPointerDelta);
}

UMBRIEL_TEST(scaledMotionKeepsFractionsUntilProtocolEncoding) {
  const auto motion = scalePointerInput(1.0, -2.0, 3.0, -4.0, 0.001, 0.0001);
  CHECK_NEAR(motion.pointer.dx, 0.001, 1e-12);
  CHECK_NEAR(motion.pointer.dy, -0.002, 1e-12);
  CHECK_NEAR(motion.raw.dx, 0.0003, 1e-12);
  CHECK_NEAR(motion.raw.dy, -0.0004, 1e-12);
}

UMBRIEL_TEST(wheelScalesDistanceAndRoundsOnlyIntegerCounts) {
  const auto motion = scaleWheelMotion(15.0, 120, 0.01);
  CHECK_EQ(motion.issue, MotionScaleIssue::None);
  CHECK_NEAR(motion.delta, 0.15, 1e-12);
  CHECK_EQ(motion.value120, 1);
  const auto tiny = scaleWheelMotion(15.0, 120, 0.0001);
  CHECK_NEAR(tiny.delta, 0.0015, 1e-12);
  CHECK_EQ(tiny.value120, 0);
  CHECK_EQ(scaleWheelMotion(15.0, 120, 0.0125).value120, 2);
  CHECK_EQ(scaleWheelMotion(15.0, 120, -0.0125).value120, -2);
}

UMBRIEL_TEST(wheelCanStopReverseAndPreserveDefaultInput) {
  const auto normal = scaleWheelMotion(15.0, 120, 1.0);
  CHECK_EQ(normal.delta, 15.0);
  CHECK_EQ(normal.value120, 120);
  const auto reversed = scaleWheelMotion(15.0, 120, -0.5);
  CHECK_EQ(reversed.delta, -7.5);
  CHECK_EQ(reversed.value120, -60);
  const auto stopped = scaleWheelMotion(15.0, 120, 0.0);
  CHECK_EQ(stopped.delta, 0.0);
  CHECK_EQ(stopped.value120, 0);
  const auto axisStop = scaleWheelMotion(0.0, 0, 0.5);
  CHECK_EQ(axisStop.delta, 0.0);
  CHECK_EQ(axisStop.value120, 0);
}

UMBRIEL_TEST(wheelOverflowCannotOverflowIntegerValue120Conversion) {
  const auto huge = scaleWheelMotion(15.0, 120, std::numeric_limits<double>::max());
  CHECK_EQ(huge.issue, MotionScaleIssue::NonFinite);
  CHECK_EQ(huge.delta, 0.0);
  CHECK_EQ(huge.value120, 0);
  const auto large = scaleWheelMotion(15.0, std::numeric_limits<int32_t>::max(), 1.0);
  CHECK_EQ(large.issue, MotionScaleIssue::OutOfRange);
  CHECK_EQ(large.value120, std::numeric_limits<int32_t>::max() - 119);
  const auto negative = scaleWheelMotion(-15.0, std::numeric_limits<int32_t>::min(), 1.0);
  CHECK_EQ(negative.issue, MotionScaleIssue::OutOfRange);
  CHECK_EQ(negative.value120, std::numeric_limits<int32_t>::min() + 119);
  const auto continuous = scaleWheelMotion(std::numeric_limits<double>::max(), 1, 2.0);
  CHECK_EQ(continuous.issue, MotionScaleIssue::NonFinite);
  CHECK_EQ(continuous.delta, 0.0);
  CHECK_EQ(continuous.value120, 2);
}

int main() { return RUN_TESTS(); }
