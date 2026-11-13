#include "input/motion_scale.h"

#include "check.h"

#include <limits>

using umbriel::MotionScaleIssue;
using umbriel::RelativeMotionRemainder;
using umbriel::scalePointerInput;
using umbriel::scalePointerMotion;
using umbriel::WheelMotionRemainder;

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

UMBRIEL_TEST(fractionalRelativeMotionPreservesTotalsInsteadOfRoundingEachEventIndependently) {
  RelativeMotionRemainder remainder;
  double x = 0;
  double y = 0;
  for (int i = 0; i < 100; ++i) {
    const auto encoded = remainder.encode(scalePointerMotion(1.0, -1.0, 0.01), 0.01);
    CHECK_EQ(encoded.dx * 256.0, std::round(encoded.dx * 256.0));
    CHECK_EQ(encoded.dy * 256.0, std::round(encoded.dy * 256.0));
    x += encoded.dx;
    y += encoded.dy;
    CHECK_NEAR(x, (i + 1) * 0.01, 1.0 / 512.0 + 1e-12);
  }
  CHECK_EQ(x, 1.0);
  CHECK_EQ(y, -1.0);
}

UMBRIEL_TEST(subQuantumMotionEventuallyReachesTheClient) {
  RelativeMotionRemainder remainder;
  double total = 0;
  int nonzeroEvents = 0;
  for (int i = 0; i < 1000; ++i) {
    const auto encoded = remainder.encode(scalePointerMotion(1.0, 0.0, 0.001), 0.001);
    total += encoded.dx;
    nonzeroEvents += encoded.dx != 0.0;
  }
  CHECK_EQ(total, 1.0);
  CHECK(nonzeroEvents > 0);
}

UMBRIEL_TEST(relativeRemaindersAreIndependentForEachStreamAndAxis) {
  RelativeMotionRemainder pointer;
  RelativeMotionRemainder raw;
  double pointerX = 0;
  double pointerY = 0;
  double rawX = 0;
  double rawY = 0;
  for (int i = 0; i < 1000; ++i) {
    const auto input = scalePointerInput(1.0, 2.0, 3.0, -4.0, 0.01, 0.001);
    const auto p = pointer.encode(input.pointer, 0.01);
    const auto r = raw.encode(input.raw, 0.001);
    pointerX += p.dx;
    pointerY += p.dy;
    rawX += r.dx;
    rawY += r.dy;
  }
  CHECK_EQ(pointerX, 10.0);
  CHECK_EQ(pointerY, 20.0);
  CHECK_EQ(rawX, 3.0);
  CHECK_EQ(rawY, -4.0);
}

UMBRIEL_TEST(contextResetAndMultiplierChangesDiscardOldFractions) {
  RelativeMotionRemainder remainder;
  CHECK_EQ(remainder.encode(scalePointerMotion(1.0, 0.0, 0.0015), 0.0015).dx, 0.0);
  remainder.reset();
  CHECK_EQ(remainder.encode(scalePointerMotion(1.0, 0.0, 0.0015), 0.0015).dx, 0.0);
  CHECK_EQ(remainder.encode(scalePointerMotion(1.0, 0.0, 0.001), 0.001).dx, 0.0);
  CHECK_EQ(remainder.encode(scalePointerMotion(1.0, 0.0, 0.0), 0.0).dx, 0.0);
}

UMBRIEL_TEST(idleAxesNeverMoveFromAnOldFraction) {
  RelativeMotionRemainder remainder;
  remainder.encode({.dx = 1.0 / 512.0, .dy = -1.0 / 512.0}, 1.0);
  const auto idle = remainder.encode({}, 1.0);
  CHECK_EQ(idle.dx, 0.0);
  CHECK_EQ(idle.dy, 0.0);
}

UMBRIEL_TEST(directionReversalsPreserveNetMotion) {
  RelativeMotionRemainder remainder;
  double total = 0;
  for (int direction : {1, -1}) {
    for (int i = 0; i < 1000; ++i) {
      total += remainder.encode(scalePointerMotion(direction, 0.0, 0.001), 0.001).dx;
    }
  }
  CHECK_EQ(total, 0.0);
}

UMBRIEL_TEST(invalidMotionClearsFractionsWithoutReplayingBadInput) {
  RelativeMotionRemainder remainder;
  remainder.encode(scalePointerMotion(1.0, 0.0, 0.0015), 0.0015);
  const auto bad = remainder.encode(scalePointerMotion(std::numeric_limits<double>::infinity(), 0.0, 0.0015), 0.0015);
  CHECK_EQ(bad.issue, MotionScaleIssue::NonFinite);
  CHECK_EQ(bad.dx, 0.0);
  CHECK_EQ(remainder.encode(scalePointerMotion(1.0, 0.0, 0.0015), 0.0015).dx, 0.0);
}

UMBRIEL_TEST(wheelScalingPreservesFractionalValue120CountsAndContinuousTotals) {
  WheelMotionRemainder wheel;
  double distance = 0;
  int64_t ticks = 0;
  for (int i = 0; i < 125; ++i) {
    const auto motion = wheel.scale(15.0, 120, 0.01);
    CHECK_EQ(motion.issue, MotionScaleIssue::None);
    CHECK(motion.delta == 0.0 || motion.value120 != 0);
    CHECK(motion.value120 == 0 || motion.delta != 0.0);
    distance += motion.delta;
    ticks += motion.value120;
  }
  CHECK_EQ(ticks, int64_t{150});
  CHECK_EQ(distance, 18.75);
}

UMBRIEL_TEST(tinyWheelStepsAccumulateInsteadOfBeingDiscarded) {
  WheelMotionRemainder wheel;
  int64_t ticks = 0;
  double distance = 0;
  for (int i = 0; i < 1000; ++i) {
    const auto motion = wheel.scale(15.0, 120, 0.0001);
    CHECK(motion.value120 == 0 || motion.delta != 0.0);
    ticks += motion.value120;
    distance += motion.delta;
  }
  CHECK_NEAR(ticks, 12.0, 1.0);
  CHECK_NEAR(distance, 1.5, 0.125 + 1.0 / 256.0);
}

UMBRIEL_TEST(wheelCanStopReverseAndResetWithoutMovingThePointer) {
  WheelMotionRemainder wheel;
  const auto reversed = wheel.scale(15.0, 120, -0.5);
  CHECK_EQ(reversed.delta, -7.5);
  CHECK_EQ(reversed.value120, -60);
  const auto stopped = wheel.scale(15.0, 120, 0.0);
  CHECK_EQ(stopped.delta, 0.0);
  CHECK_EQ(stopped.value120, 0);
  wheel.scale(15.0, 120, 0.001);
  wheel.reset();
  CHECK_EQ(wheel.scale(15.0, 120, 0.001).value120, 0);
}

UMBRIEL_TEST(wheelOverflowCannotOverflowIntegerValue120Accumulators) {
  WheelMotionRemainder wheel;
  const auto huge = wheel.scale(15.0, 120, std::numeric_limits<double>::max());
  CHECK_EQ(huge.issue, MotionScaleIssue::NonFinite);
  CHECK_EQ(huge.delta, 0.0);
  CHECK_EQ(huge.value120, 0);
  const auto large = wheel.scale(15.0, std::numeric_limits<int32_t>::max(), 1.0);
  CHECK_EQ(large.issue, MotionScaleIssue::OutOfRange);
  CHECK(large.value120 <= std::numeric_limits<int32_t>::max() - 119);
  const auto negative = wheel.scale(-15.0, std::numeric_limits<int32_t>::min(), 1.0);
  CHECK_EQ(negative.issue, MotionScaleIssue::OutOfRange);
  CHECK(negative.value120 >= std::numeric_limits<int32_t>::min() + 119);
}

int main() { return RUN_TESTS(); }
