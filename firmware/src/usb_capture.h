// The side facing the real device on J6: clones its descriptors and pipes its endpoints.
#pragma once

#include <USBHost_t36.h>
#include "device_kind.h"
#include "usb_device.h"

// Generic mode only: a mouse plugged in when nothing was cloned, read through the HID parser.
class WiredMouse : public MouseController {
public:
  using MouseController::MouseController;
  USBHIDParser *volatile source = nullptr;

protected:
  hidclaim_t claim_collection(USBHIDParser *parser, Device_t *dev, uint32_t usage) override {
    if (source && source != parser) return CLAIM_NO;
    auto claim = MouseController::claim_collection(parser, dev, usage);
    if (claim != CLAIM_NO) source = parser;
    return claim;
  }

  void disconnect_collection(Device_t *dev) override {
    MouseController::disconnect_collection(dev);
    if (!mydevice) {
      source = nullptr;
      mouseDataClear();
    }
  }
};

extern WiredMouse mouse;

void beginUsbHost();
void taskUsbHost();
// Waits up to 2.5 s for a device, clones it into `profile` and starts forwarding its input.
// kinds[i] receives the kind attached to interface i. False means generic mode: nothing
// usable was on J6. A device that fails after being claimed restarts the board once into
// generic mode, because a claimed device cannot be handed to the HID parser.
bool probeDevice(UsbProfile &profile, const DeviceKind *kinds[MaxUsbInterfaces]);
bool deviceAttached();  // the cloned device is still on J6
// hostControl and hostControlResult, declared in usb_control.h, are implemented here too.
