#include "mouse_action.h"

#include <math.h>

namespace {

constexpr uint8_t kMouseActionMode = 3;
constexpr uint8_t kSupportedButtons = 0x1F;
constexpr uint16_t kMinimumMovementTicks = 2;
constexpr uint16_t kMinimumTextureTicks = 32;
constexpr double kMaximumPhaseOffsetTicks = 0.25;
constexpr double kMaximumSyntheticCountsPerTick = 100.0;

uint16_t readU16(const int8_t packet[17], int offset) {
  const uint16_t high = static_cast<uint8_t>(packet[offset]);
  const uint16_t low = static_cast<uint8_t>(packet[offset + 1]);
  return static_cast<uint16_t>((high << 8) | low);
}

int16_t readI16(const int8_t packet[17], int offset) {
  return static_cast<int16_t>(readU16(packet, offset));
}

bool normalizeAndValidate(MouseActionCommand &command) {
  if (command.curvature == static_cast<int8_t>(-128) ||
      (command.buttonMask & ~kSupportedButtons) != 0) {
    return false;
  }

  if (command.shotX != 0 || command.shotY != 0) {
    if (command.approachTicks < kMinimumMovementTicks) {
      command.approachTicks = kMinimumMovementTicks;
    }
    if (command.approachTicks <
        minimumSegmentTicks(command.shotX, command.shotY,
                            command.curvature)) {
      return false;
    }
  } else {
    command.approachTicks = 0;
  }

  if (command.correctionX != 0 || command.correctionY != 0) {
    if (command.correctionTicks < kMinimumMovementTicks) {
      command.correctionTicks = kMinimumMovementTicks;
    }
    if (command.correctionTicks <
        minimumSegmentTicks(command.correctionX, command.correctionY,
                            command.curvature)) {
      return false;
    }
  } else {
    command.correctionTicks = 0;
  }

  if (command.buttonMask != 0 && command.holdTicks == 0) {
    command.holdTicks = 1;
  }
  return true;
}

double minimumJerk(double time) {
  return time * time * time * (10.0 - 15.0 * time + 6.0 * time * time);
}

uint32_t mixBits(uint32_t value) {
  value ^= value >> 16;
  value *= 0x7FEB352Du;
  value ^= value >> 15;
  value *= 0x846CA68Bu;
  return value ^ (value >> 16);
}

uint32_t commandSeed(const MouseActionCommand &command, uint32_t frameTick) {
  uint32_t seed = frameTick;
  seed ^= static_cast<uint16_t>(command.shotX);
  seed ^= static_cast<uint32_t>(static_cast<uint16_t>(command.shotY)) << 16;
  seed ^= static_cast<uint32_t>(command.approachTicks) << 7;
  seed ^= static_cast<uint32_t>(static_cast<uint8_t>(command.curvature)) << 24;
  seed ^= static_cast<uint32_t>(static_cast<uint16_t>(command.correctionX)) << 3;
  seed ^= static_cast<uint32_t>(static_cast<uint16_t>(command.correctionY)) << 19;
  return mixBits(seed);
}

double maximumPhaseOffset(int16_t dx, int16_t dy, int8_t curvature,
                          uint16_t durationTicks) {
  if (durationTicks < kMinimumTextureTicks) {
    return 0.0;
  }
  const double length = hypot(static_cast<double>(dx), static_cast<double>(dy));
  const double bend = static_cast<double>(curvature) / 128.0;
  const double peakCountsPerTick =
      1.875 * length * sqrt(1.0 + bend * bend) / durationTicks;
  if (peakCountsPerTick <= 0.0) {
    return 0.0;
  }
  const double safeOffset =
      (kMaximumSyntheticCountsPerTick / peakCountsPerTick - 1.0) / 2.0;
  if (safeOffset <= 0.0) {
    return 0.0;
  }
  return safeOffset < kMaximumPhaseOffsetTicks ? safeOffset
                                               : kMaximumPhaseOffsetTicks;
}

double phaseOffset(uint32_t seed, uint32_t elapsed, double amplitude) {
  if (elapsed == 0 || amplitude == 0.0) {
    return 0.0;
  }
  const uint32_t sample = mixBits(seed ^ (elapsed * 0x9E3779B9u));
  return (sample & 1u) != 0 ? amplitude : -amplitude;
}

int32_t bezierCoordinate(int16_t end, double control, double progress) {
  const double inverse = 1.0 - progress;
  return lround(2.0 * inverse * progress * control +
                progress * progress * end);
}

}  // namespace

uint16_t minimumSegmentTicks(int16_t dx, int16_t dy, int8_t curvature) {
  const double length = hypot(static_cast<double>(dx), static_cast<double>(dy));
  const double bend = static_cast<double>(curvature) / 128.0;
  return static_cast<uint16_t>(
      ceil(1.875 * length * sqrt(1.0 + bend * bend) / 100.0));
}

bool decodeMouseActionPacket(const int8_t packet[17], MouseActionCommand &command) {
  if (static_cast<uint8_t>(packet[10]) != kMouseActionMode) {
    return false;
  }

  MouseActionCommand decoded{};
  decoded.shotX = readI16(packet, 0);
  decoded.shotY = readI16(packet, 2);
  decoded.approachTicks = readU16(packet, 4);
  decoded.curvature = packet[6];
  decoded.dwellTicks = static_cast<uint8_t>(packet[7]);
  decoded.buttonMask = static_cast<uint8_t>(packet[8]);
  decoded.holdTicks = static_cast<uint8_t>(packet[9]);
  decoded.correctionTicks = readU16(packet, 11);
  decoded.correctionX = readI16(packet, 13);
  decoded.correctionY = readI16(packet, 15);
  if (!normalizeAndValidate(decoded)) {
    return false;
  }

  command = decoded;
  return true;
}

uint32_t FrameClock::update(uint16_t rawFrame) {
  rawFrame &= 0x07FF;
  if (!initialized_) {
    initialized_ = true;
    previousRawFrame_ = rawFrame;
    return frameTick_;
  }
  const uint16_t delta = (rawFrame - previousRawFrame_) & 0x07FF;
  frameTick_ += delta;
  previousRawFrame_ = rawFrame;
  return frameTick_;
}

bool MouseAction::start(const MouseActionCommand &command, uint32_t frameTick) {
  MouseActionCommand normalized = command;
  if (!normalizeAndValidate(normalized)) {
    return false;
  }

  command_ = normalized;
  actionSeed_ = commandSeed(command_, frameTick);
  buttons_ = 0;
  segmentSentX_ = 0;
  segmentSentY_ = 0;
  phaseStartedAt_ = frameTick;
  if (command_.approachTicks != 0) {
    phase_ = Phase::Approach;
  } else {
    beginAfterApproach(frameTick);
  }
  return true;
}

MouseActionUpdate MouseAction::advanceSegment(uint32_t frameTick, int16_t endX,
                                          int16_t endY,
                                          uint16_t durationTicks,
                                          bool isApproach) {
  const uint32_t elapsed = frameTick - phaseStartedAt_;
  int32_t targetX = endX;
  int32_t targetY = endY;
  double appliedPhaseOffset = 0.0;
  if (elapsed < durationTicks) {
    const double amplitude =
        maximumPhaseOffset(endX, endY, command_.curvature, durationTicks);
    const uint32_t segmentSeed =
        isApproach ? actionSeed_ : actionSeed_ ^ 0xA511E9B3u;
    appliedPhaseOffset = phaseOffset(segmentSeed, elapsed, amplitude);
    const double effectiveElapsed =
        static_cast<double>(elapsed) + appliedPhaseOffset;
    const double clampedElapsed = effectiveElapsed < 0.0 ? 0.0 : effectiveElapsed;
    const double time = clampedElapsed / durationTicks;
    const double progress = minimumJerk(time);
    const double curvature = command_.curvature;
    const double controlX = endX * 0.5 - curvature / 256.0 * endY;
    const double controlY = endY * 0.5 + curvature / 256.0 * endX;
    targetX = bezierCoordinate(endX, controlX, progress);
    targetY = bezierCoordinate(endY, controlY, progress);
  }

  MouseActionUpdate update{
      targetX - segmentSentX_, targetY - segmentSentY_, buttons_,
      static_cast<int8_t>(lround(appliedPhaseOffset * 256.0))};
  segmentSentX_ = targetX;
  segmentSentY_ = targetY;
  if (elapsed < durationTicks) {
    return update;
  }

  const uint32_t segmentEndedAt = phaseStartedAt_ + durationTicks;
  if (isApproach) {
    beginAfterApproach(segmentEndedAt);
  } else {
    phase_ = Phase::Idle;
  }
  update.buttons = buttons_;
  return update;
}

void MouseAction::beginAfterApproach(uint32_t frameTick) {
  segmentSentX_ = 0;
  segmentSentY_ = 0;
  phaseStartedAt_ = frameTick;
  if (command_.buttonMask != 0) {
    phase_ = Phase::Dwell;
  } else {
    beginCorrection(frameTick);
  }
}

void MouseAction::beginCorrection(uint32_t frameTick) {
  segmentSentX_ = 0;
  segmentSentY_ = 0;
  phaseStartedAt_ = frameTick;
  phase_ = command_.correctionTicks != 0 ? Phase::Correct : Phase::Idle;
}

MouseActionUpdate MouseAction::update(uint32_t frameTick) {
  MouseActionUpdate update{0, 0, buttons_, 0};
  for (;;) {
    switch (phase_) {
      case Phase::Idle:
        return update;
      case Phase::Approach:
        update = advanceSegment(frameTick, command_.shotX, command_.shotY,
                                command_.approachTicks, true);
        if (phase_ == Phase::Approach || update.dx != 0 || update.dy != 0) {
          if (phase_ != Phase::Dwell ||
              frameTick - phaseStartedAt_ < command_.dwellTicks) {
            return update;
          }
        }
        break;
      case Phase::Dwell:
        if (frameTick - phaseStartedAt_ < command_.dwellTicks) {
          return update;
        }
        phaseStartedAt_ += command_.dwellTicks;
        phase_ = Phase::Hold;
        buttons_ = command_.buttonMask;
        update.buttons = buttons_;
        return update;
      case Phase::Hold:
        if (frameTick - phaseStartedAt_ < command_.holdTicks) {
          return update;
        }
        phaseStartedAt_ += command_.holdTicks;
        buttons_ = 0;
        update.buttons = 0;
        beginCorrection(phaseStartedAt_);
        return update;
      case Phase::Correct:
        return advanceSegment(frameTick, command_.correctionX,
                              command_.correctionY,
                              command_.correctionTicks, false);
    }
  }
}

MouseActionUpdate MouseAction::cancel() {
  phase_ = Phase::Idle;
  buttons_ = 0;
  return {0, 0, 0, 0};
}

bool MouseAction::active() const { return phase_ != Phase::Idle; }
