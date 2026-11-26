// A device kind recognises the interfaces it understands and may filter their input.
// The bridge proxies every interface whether or not a kind matches it.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include "mouse_layout.h"
#include "usb_profile.h"

struct DeviceKind {
  const char *name;
  bool (*match)(const UsbProfile &profile, unsigned interface);
  void (*attach)(const UsbProfile &profile, unsigned interface);  // may be null
  // May be null. Called in the USB host interrupt for each input packet of the attached
  // interface, before it is forwarded. It may edit the packet; false drops it.
  bool (*input)(uint8_t *packet, uint8_t length);
  // May be null. Whether the packet `input` last let through reached the computer.
  void (*sent)(bool delivered);
};

extern const DeviceKind mouseKind, keyboardKind, gamepadKind;

// Usage page and usage of the first top-level collection of a HID report descriptor.
bool firstUsage(const uint8_t *report, size_t length, uint16_t &page, uint16_t &usage);
// The kind of one interface: gamepad, keyboard and mouse are tried in that order.
const DeviceKind *kindOf(const UsbProfile &profile, unsigned interface);
// What the device is called: the kind of its lowest-numbered matching interface, or "other".
const char *kindName(const UsbProfile &profile);
// Attaches a kind to each interface it matches and writes it to kinds[interface index]
// (null for none). A kind that filters input takes only the first interface it matches.
void attachKinds(const UsbProfile &profile, const DeviceKind *kinds[MaxUsbInterfaces]);

// State of the mouse kind. The filter runs in the host interrupt: touch these with
// interrupts disabled.
struct InjectedMouse {
  int x, y;                      // counts not sent yet
  uint8_t buttons, sentButtons;  // wanted, and last delivered
  bool merged;                   // a physical report carried it since the last frame
};
struct PhysicalMouse {
  uint8_t buttons;  // as the mouse reports them, before hiding
  int wheel;        // steps since the reader last cleared it
  bool changed;     // a report arrived since the reader last cleared it
};
extern MouseLayout mouseLayout;              // bytes == 0 while no mouse is attached
extern volatile bool mouseFrozen;            // drop every physical report
extern volatile uint8_t hiddenMouseButtons;  // button mask removed from forwarded reports
extern InjectedMouse injected;               // synthetic input waiting to reach the computer
extern PhysicalMouse physicalMouse;
