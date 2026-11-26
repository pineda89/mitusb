#pragma once

#include <USBHost_t36.h>
#include "mouse_device.h"
#include "mouse_layout.h"

class WiredMouse : public MouseController {
public:
  using MouseController::MouseController;
  USBHIDParser *volatile source = nullptr;
  bool isCaptured() const;

protected:
  hidclaim_t claim_collection(USBHIDParser *parser, Device_t *dev, uint32_t usage) override {
    if (source && source != parser) return CLAIM_NO;
    auto claim = MouseController::claim_collection(parser, dev, usage);
    if (claim != CLAIM_NO) source = parser;
    return claim;
  }

  bool hid_process_in_data(const Transfer_t *transfer) override;

  void disconnect_collection(Device_t *dev) override {
    MouseController::disconnect_collection(dev);
    if (!mydevice) {
      source = nullptr;
      mouseDataClear();
    }
  }
};

extern WiredMouse mouse;
// Copied-mode forwarding runs in the USB host interrupt, so the main loop steers it with these.
extern volatile bool mouseFrozen;            // drop every physical report
extern volatile uint8_t hiddenMouseButtons;  // button mask removed from forwarded reports
// Synthetic input waiting to reach the computer. Physical reports carry it when the mouse
// is moving, so both arrive as one report. Touch it with interrupts disabled.
struct InjectedMouse {
  int x, y;                      // counts not sent yet
  uint8_t buttons, sentButtons;  // wanted, and last delivered
  bool merged;                   // a physical report carried it since the last frame
};
extern InjectedMouse injected;

void beginMouseHost();
void taskMouseHost();
bool probeMouse(MouseProfile &profile, MouseLayout &layout, uint8_t &mouseInterface);
