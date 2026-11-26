// Keyboard: recognised and proxied untouched. Key injection would go here.
#include "device_kind.h"

static bool match(const UsbProfile &profile, unsigned interface) {
  const UsbInterface &i = profile.interfaces[interface];
  if (i.interfaceClass != 3) return false;
  if (i.subClass == 1 && i.protocol == 1) return true;  // boot keyboard
  uint16_t page, usage;
  return firstUsage(i.report, i.reportLength, page, usage) && page == 1 && usage == 6;
}

const DeviceKind keyboardKind = {"keyboard", match, nullptr, nullptr, nullptr};
