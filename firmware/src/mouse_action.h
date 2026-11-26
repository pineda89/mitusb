#pragma once

#include <stdint.h>

struct MouseActionCommand {
  int16_t shotX;
  int16_t shotY;
  int16_t correctionX;
  int16_t correctionY;
  uint16_t approachTicks;
  uint16_t correctionTicks;
  int8_t curvature;
  uint8_t dwellTicks;
  uint8_t buttonMask;
  uint8_t holdTicks;
};

struct MouseActionUpdate {
  int32_t dx;
  int32_t dy;
  uint8_t buttons;
  int8_t phaseOffset256;
};

bool decodeMouseActionPacket(const int8_t packet[17], MouseActionCommand &command);
uint16_t minimumSegmentTicks(int16_t dx, int16_t dy, int8_t curvature);

class FrameClock {
 public:
  uint32_t update(uint16_t rawFrame);

 private:
  bool initialized_ = false;
  uint16_t previousRawFrame_ = 0;
  uint32_t frameTick_ = 0;
};

class MouseAction {
 public:
  bool start(const MouseActionCommand &command, uint32_t frameTick);
  MouseActionUpdate update(uint32_t frameTick);
  MouseActionUpdate cancel();
  bool active() const;

 private:
  enum class Phase : uint8_t { Idle, Approach, Dwell, Hold, Correct };

  void beginAfterApproach(uint32_t frameTick);
  void beginCorrection(uint32_t frameTick);
  MouseActionUpdate advanceSegment(uint32_t frameTick, int16_t endX,
                                 int16_t endY, uint16_t durationTicks,
                                 bool isApproach);

  MouseActionCommand command_{};
  Phase phase_ = Phase::Idle;
  uint32_t phaseStartedAt_ = 0;
  uint32_t actionSeed_ = 0;
  int32_t segmentSentX_ = 0;
  int32_t segmentSentY_ = 0;
  uint8_t buttons_ = 0;
};
