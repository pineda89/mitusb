#pragma once

#include <USBHost_t36.h>
#include "usb_device.h"
#include "device_kind.h"

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
void beginMouseHost();
void taskMouseHost();
// On success `mouseEndpoint` is the IN endpoint that carries the mouse's reports.
bool probeMouse(UsbProfile &profile, MouseLayout &layout, uint8_t &mouseEndpoint);
