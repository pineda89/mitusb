#include <unity.h>
#include "../../src/mouse_action.h"

#include <math.h>
#include <stdint.h>

#include <vector>

namespace {

void putU16(int8_t packet[17], int offset, uint16_t value) {
  packet[offset] = static_cast<int8_t>(value >> 8);
  packet[offset + 1] = static_cast<int8_t>(value);
}

void putI16(int8_t packet[17], int offset, int16_t value) {
  putU16(packet, offset, static_cast<uint16_t>(value));
}

void testDecodesMode3Packet() {
  int8_t packet[17] = {0};
  putI16(packet, 0, 1000);
  putI16(packet, 2, -200);
  putU16(packet, 4, 300);
  packet[6] = 16;
  packet[7] = 12;
  packet[8] = 1;
  packet[9] = 40;
  packet[10] = 3;
  putU16(packet, 11, 120);
  putI16(packet, 13, 100);
  putI16(packet, 15, -50);

  MouseActionCommand command{};
  TEST_ASSERT(decodeMouseActionPacket(packet, command));
  TEST_ASSERT(command.shotX == 1000);
  TEST_ASSERT(command.shotY == -200);
  TEST_ASSERT(command.approachTicks == 300);
  TEST_ASSERT(command.curvature == 16);
  TEST_ASSERT(command.dwellTicks == 12);
  TEST_ASSERT(command.buttonMask == 1);
  TEST_ASSERT(command.holdTicks == 40);
  TEST_ASSERT(command.correctionTicks == 120);
  TEST_ASSERT(command.correctionX == 100);
  TEST_ASSERT(command.correctionY == -50);
}

void testRejectsInvalidPackets() {
  int8_t packet[17] = {0};
  packet[10] = 2;
  MouseActionCommand command{};
  TEST_ASSERT(!decodeMouseActionPacket(packet, command));

  packet[10] = 3;
  packet[6] = static_cast<int8_t>(-128);
  TEST_ASSERT(!decodeMouseActionPacket(packet, command));

  packet[6] = 0;
  putI16(packet, 0, 1000);
  putU16(packet, 4, 2);
  TEST_ASSERT(!decodeMouseActionPacket(packet, command));
}

void testNormalizesShortDurationsAndIgnoresEmptyCorrectionDuration() {
  int8_t packet[17] = {0};
  packet[10] = 3;
  putI16(packet, 0, 10);
  putU16(packet, 4, 1);
  putU16(packet, 11, 99);

  MouseActionCommand command{};
  TEST_ASSERT(decodeMouseActionPacket(packet, command));
  TEST_ASSERT(command.approachTicks == 2);
  TEST_ASSERT(command.correctionTicks == 0);
}

void testMinimumDurationIncludesCurvature() {
  TEST_ASSERT(minimumSegmentTicks(100, 0, 0) == 2);
  TEST_ASSERT(minimumSegmentTicks(100, 0, 127) == 3);
}

void testFrameClockExtendsRawWrap() {
  FrameClock clock;
  TEST_ASSERT(clock.update(2046) == 0);
  TEST_ASSERT(clock.update(2047) == 1);
  TEST_ASSERT(clock.update(0) == 2);
  TEST_ASSERT(clock.update(3) == 5);
}

void testQuadraticCurveUsesMinimumJerkProgress() {
  MouseAction action;
  const MouseActionCommand command{100, 0, 0, 0, 20, 0, 16, 0, 0, 0};
  TEST_ASSERT(action.start(command, 0));
  action.update(0);
  const MouseActionUpdate midpoint = action.update(10);
  TEST_ASSERT(midpoint.dx == 50);
  TEST_ASSERT(midpoint.dy == 3);
  const MouseActionUpdate endpoint = action.update(20);
  TEST_ASSERT(midpoint.dx + endpoint.dx == 100);
  TEST_ASSERT(midpoint.dy + endpoint.dy == 0);
  TEST_ASSERT(!action.active());
}

void testPhaseWarpProducesSensorLikeTemporalTexture() {
  MouseAction action;
  const MouseActionCommand command{300, 0, 0, 0, 140, 0, 0, 0, 0, 0};
  TEST_ASSERT(action.start(command, 100));

  std::vector<int32_t> deltas;
  int32_t totalX = 0;
  bool sawPositivePhase = false;
  bool sawNegativePhase = false;
  for (uint32_t tick = 100; tick <= 240; ++tick) {
    const MouseActionUpdate update = action.update(tick);
    deltas.push_back(update.dx);
    totalX += update.dx;
    sawPositivePhase |= update.phaseOffset256 > 0;
    sawNegativePhase |= update.phaseOffset256 < 0;
    if (tick == 100 || tick == 240) {
      TEST_ASSERT(update.phaseOffset256 == 0);
    }
  }

  double speedSum = 0.0;
  double changeSum = 0.0;
  for (size_t index = 0; index < deltas.size(); ++index) {
    speedSum += fabs(static_cast<double>(deltas[index]));
    if (index != 0) {
      changeSum += fabs(static_cast<double>(deltas[index] - deltas[index - 1]));
    }
  }
  const double meanSpeed = speedSum / deltas.size();
  const double meanChange = changeSum / (deltas.size() - 1);
  const double roughness = meanChange / meanSpeed;

  TEST_ASSERT(totalX == 300);
  TEST_ASSERT(sawPositivePhase);
  TEST_ASSERT(sawNegativePhase);
  TEST_ASSERT(roughness >= 0.45);
  TEST_ASSERT(roughness <= 0.80);
}

void testActionReachesShotClicksAndCorrects() {
  MouseAction action;
  const MouseActionCommand command{100, 0, 20, 10, 20, 10, 16, 2, 1, 2};
  TEST_ASSERT(action.start(command, 0));
  int32_t x = 0;
  int32_t y = 0;
  bool sawPress = false;
  bool sawRelease = false;
  for (uint32_t tick = 0; tick <= 40; ++tick) {
    const MouseActionUpdate update = action.update(tick);
    x += update.dx;
    y += update.dy;
    sawPress |= update.buttons == 1;
    sawRelease |= sawPress && update.buttons == 0;
  }
  TEST_ASSERT(x == 120);
  TEST_ASSERT(y == 10);
  TEST_ASSERT(sawPress);
  TEST_ASSERT(sawRelease);
  TEST_ASSERT(!action.active());
}

void testActionEdgesFollowTheExactSchedule() {
  MouseAction action;
  const MouseActionCommand command{100, 0, 20, 10, 20, 10, 0, 2, 1, 3};
  TEST_ASSERT(action.start(command, 50));
  uint32_t pressTick = 0;
  uint32_t releaseTick = 0;
  uint32_t endTick = 0;
  for (uint32_t tick = 50; tick <= 90; ++tick) {
    const MouseActionUpdate update = action.update(tick);
    if (update.buttons == 1 && pressTick == 0) pressTick = tick;
    if (pressTick != 0 && update.buttons == 0 && releaseTick == 0) {
      releaseTick = tick;
    }
    if (!action.active() && endTick == 0) endTick = tick;
  }
  TEST_ASSERT(pressTick == 72);
  TEST_ASSERT(releaseTick == 75);
  TEST_ASSERT(endTick == 85);
}

void testMinimumAcceptedDurationStaysWithinSyntheticBudget() {
  MouseAction action;
  const uint16_t duration = minimumSegmentTicks(1000, 500, 64);
  const MouseActionCommand command{1000, 500, 0, 0, duration, 0, 64, 0, 0, 0};
  TEST_ASSERT(action.start(command, 0));
  for (uint32_t tick = 0; tick <= duration; ++tick) {
    const MouseActionUpdate update = action.update(tick);
    TEST_ASSERT(update.dx >= -100 && update.dx <= 100);
    TEST_ASSERT(update.dy >= -100 && update.dy <= 100);
  }
}

void testSkippedTickStillProducesDistinctButtonEdges() {
  MouseAction action;
  const MouseActionCommand command{1, 0, 0, 0, 2, 0, 0, 2, 1, 2};
  TEST_ASSERT(action.start(command, 0));
  action.update(0);
  const MouseActionUpdate press = action.update(100);
  const MouseActionUpdate release = action.update(101);
  TEST_ASSERT(press.buttons == 1);
  TEST_ASSERT(release.buttons == 0);
  TEST_ASSERT(!action.active());
}

void testCancelReleasesHeldButton() {
  MouseAction action;
  const MouseActionCommand command{1, 0, 0, 0, 2, 0, 0, 0, 1, 20};
  TEST_ASSERT(action.start(command, 0));
  action.update(0);
  TEST_ASSERT(action.update(2).buttons == 1);
  TEST_ASSERT(action.cancel().buttons == 0);
  TEST_ASSERT(!action.active());
}

void testTickCounterWrapDoesNotReverseAction() {
  MouseAction action;
  const MouseActionCommand command{100, 0, 0, 0, 20, 0, 0, 0, 0, 0};
  TEST_ASSERT(action.start(command, 0xFFFFFFF0u));
  action.update(0xFFFFFFF0u);
  const MouseActionUpdate endpoint = action.update(4);
  TEST_ASSERT(endpoint.dx == 100);
  TEST_ASSERT(endpoint.dy == 0);
  TEST_ASSERT(!action.active());
}

void testZeroLengthApproachAndButtonlessCorrection() {
  MouseAction action;
  const MouseActionCommand command{0, 0, 10, -5, 0, 4, 0, 10, 0, 10};
  TEST_ASSERT(action.start(command, 7));
  int32_t x = 0;
  int32_t y = 0;
  for (uint32_t tick = 7; tick <= 12; ++tick) {
    const MouseActionUpdate update = action.update(tick);
    x += update.dx;
    y += update.dy;
    TEST_ASSERT(update.buttons == 0);
  }
  TEST_ASSERT(x == 10);
  TEST_ASSERT(y == -5);
  TEST_ASSERT(!action.active());
}

}  // namespace

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(testDecodesMode3Packet);
  RUN_TEST(testRejectsInvalidPackets);
  RUN_TEST(testNormalizesShortDurationsAndIgnoresEmptyCorrectionDuration);
  RUN_TEST(testMinimumDurationIncludesCurvature);
  RUN_TEST(testFrameClockExtendsRawWrap);
  RUN_TEST(testQuadraticCurveUsesMinimumJerkProgress);
  RUN_TEST(testPhaseWarpProducesSensorLikeTemporalTexture);
  RUN_TEST(testActionReachesShotClicksAndCorrects);
  RUN_TEST(testActionEdgesFollowTheExactSchedule);
  RUN_TEST(testMinimumAcceptedDurationStaysWithinSyntheticBudget);
  RUN_TEST(testSkippedTickStillProducesDistinctButtonEdges);
  RUN_TEST(testCancelReleasesHeldButton);
  RUN_TEST(testTickCounterWrapDoesNotReverseAction);
  RUN_TEST(testZeroLengthApproachAndButtonlessCorrection);
  return UNITY_END();
}
