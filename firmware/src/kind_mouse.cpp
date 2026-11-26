// Mouse: the one kind that edits reports. It hides buttons, carries injected movement in
// the mouse's own reports and keeps the physical button and wheel state for the main loop.
#include "device_kind.h"

MouseLayout mouseLayout;
volatile bool mouseFrozen = false;
volatile uint8_t hiddenMouseButtons = 0;
InjectedMouse injected;
PhysicalMouse physicalMouse;

static int takenX, takenY;
static bool carried;  // the packet last let through carries injected input

static bool match(const UsbProfile &profile, unsigned interface) {
  const UsbInterface &i = profile.interfaces[interface];
  MouseLayout layout;
  return i.interfaceClass == 3 && readMouseLayout(i.report, i.reportLength, layout);
}

static void attach(const UsbProfile &profile, unsigned interface) {
  const UsbInterface &i = profile.interfaces[interface];
  readMouseLayout(i.report, i.reportLength, mouseLayout);
}

static bool input(uint8_t *packet, uint8_t length) {
  carried = false;
  if (mouseFrozen) return false;
  uint8_t buttons;
  int x, y, wheel, pan;
  if (decodeMouse(mouseLayout, packet, length, buttons, x, y, wheel, pan)) {
    physicalMouse.buttons = buttons;
    physicalMouse.wheel += wheel;
    physicalMouse.changed = true;
  }
  hideMouseButtons(mouseLayout, hiddenMouseButtons, packet, length);
  carried = mergeMouse(mouseLayout, injected.buttons, injected.x, injected.y,
                       packet, length, takenX, takenY);
  return true;
}

static void sent(bool delivered) {
  if (!carried || !delivered) return;
  injected.x -= takenX;
  injected.y -= takenY;
  injected.sentButtons = injected.buttons;
  injected.merged = true;
}

const DeviceKind mouseKind = {"mouse", match, attach, input, sent};
