// Gamepad: recognised and proxied untouched. Stick and button injection would go here.
#include "device_kind.h"

static bool match(const UsbProfile &profile, unsigned interface) {
  const UsbInterface &i = profile.interfaces[interface];
  // The control interface of an Xbox 360 style pad, which is not HID.
  if (i.interfaceClass == 0xff && i.subClass == 0x5d && i.protocol == 0x01) return true;
  uint16_t page, usage;
  return i.interfaceClass == 3 && firstUsage(i.report, i.reportLength, page, usage) &&
         page == 1 && (usage == 4 || usage == 5);  // joystick or gamepad
}

const DeviceKind gamepadKind = {"gamepad", match, nullptr, nullptr, nullptr};
