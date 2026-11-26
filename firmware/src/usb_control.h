#pragma once
#include <stdint.h>
#include "usb_profile.h"

// Forwards to the cloned device the control requests the board cannot answer itself: class
// and vendor requests, and descriptors it did not copy. Two gaps: a request with OUT data is
// acknowledged to the computer before the device answers, and remote wakeup is not offered.
void enableUsbControl(const UsbProfile &profile);
void taskUsbControl();

// Host side. hostControl starts a control transfer to the cloned device; false if one is
// in flight or the device is gone. hostControlResult: -2 in flight, -1 failed, else bytes.
bool hostControl(uint64_t setup, uint8_t *data);
int hostControlResult();
