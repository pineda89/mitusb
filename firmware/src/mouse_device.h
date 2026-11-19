#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "mouse_profile.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { MouseDescriptorCapacity = 512 };
// Both EHCI descriptors store the untransferred byte count in bits 16..30.
static inline uint32_t completedPacketLength(uint32_t requested, uint32_t status) {
  uint32_t remaining = (status >> 16) & 0x7fff;
  return (status & 0xfc) || remaining > requested ? 0 : requested - remaining;
}

bool startMouseDevice(const MouseProfile *profile, uint8_t mouseInterface);
bool sendMousePacket(const uint8_t *packet, uint8_t length);
bool forwardHidInput(uint8_t interfaceNumber, const uint8_t *packet, uint8_t length);
void forwardHidOutput(uint8_t interfaceNumber, const uint8_t *packet, uint8_t length);
void pollMouseDevice();

#ifdef __cplusplus
}
#endif
