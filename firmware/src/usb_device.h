// The PC-facing side: serves the profile's descriptors and moves packets on its endpoints.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "usb_profile.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { UsbDescriptorCapacity = 512 };
// Both EHCI descriptors store the untransferred byte count in bits 16..30.
static inline uint32_t completedPacketLength(uint32_t requested, uint32_t status) {
  uint32_t remaining = (status >> 16) & 0x7fff;
  return (status & 0xfc) || remaining > requested ? 0 : requested - remaining;
}

// Presents the profile to the computer. With no usable profile it presents the generic
// mouse and returns false.
bool startUsbDevice(const UsbProfile *profile);
// Sends one packet on an IN endpoint of the profile. False if the endpoint is not one, the
// length does not fit it, the computer has not configured the board, or the queue is full
// (after up to 30 ms when `wait`).
bool sendUsbPacket(uint8_t endpoint, const uint8_t *packet, uint8_t length, bool wait);
// Called for each packet the computer sends on an OUT endpoint. Implemented by the host side.
void forwardUsbOutput(uint8_t endpoint, const uint8_t *packet, uint8_t length);
void pollUsbDevice(void);

#ifdef __cplusplus
}
#endif
