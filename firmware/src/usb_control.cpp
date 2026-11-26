#include <Arduino.h>
#include "usb_control.h"

extern "C" {
void usb_proxy_transmit(const void *data, uint32_t length);
void usb_proxy_receive(void *data, uint32_t length, int notify);
void usb_proxy_stall(void);
}

namespace {
union Setup {
  uint64_t both;
  struct {
    uint8_t type, request;
    uint16_t value, index, length;
  };
};
enum State : uint8_t { Idle, Receiving, Wanted, Forwarding };

bool enabled;
volatile bool busReset;
uint8_t bootInterfaces[MaxUsbInterfaces], bootInterfaceCount;
volatile State state;
volatile uint32_t generation;  // counts SETUP packets, so a late answer is not sent to a newer request
Setup pending;
// Not DMAMEM: both controllers transfer straight into it and this memory is not cached.
uint8_t data[512] __attribute__((aligned(32)));
const uint32_t TimeoutMs = 500;

void finish(uint32_t forGeneration, const Setup &setup, int result) {
  noInterrupts();
  if (generation == forGeneration) {
    state = Idle;
    bool in = setup.type & 0x80;
    if (result < 0 || (in && setup.length && !result)) usb_proxy_stall();
    else if (in && setup.length) usb_proxy_transmit(data, result < setup.length ? result : setup.length);
    else if (!setup.length) usb_proxy_receive(nullptr, 0, 0);
    // With OUT data the core acknowledged the computer when the data arrived.
  }
  interrupts();
}
}

// Called by the patched core, in the USB interrupt, for every SETUP: first before the core
// looks at it, and again (unhandled) when the core is about to stall it.
extern "C" int usb_proxy_setup(uint64_t setupData, int unhandled) {
  if (!enabled) return 0;
  if (!unhandled) {
    ++generation;
    state = Idle;
  }
  Setup setup;
  setup.both = setupData;
  if ((!unhandled && !(setup.type & 0x60)) || setup.length > sizeof data) return 0;
  pending = setup;
  if (!(setup.type & 0x80) && setup.length) {
    state = Receiving;
    usb_proxy_receive(data, setup.length, 1);
  } else {
    state = Wanted;
  }
  return 1;
}

extern "C" int usb_proxy_complete(void) {
  if (state != Receiving) return 0;
  state = Wanted;
  return 1;
}

// A keyboard the BIOS switched to boot protocol must return to report protocol when the
// operating system resets the bus: the board, not the device, sees that reset.
extern "C" void usb_proxy_reset(void) {
  busReset = true;
}

void enableUsbControl(const UsbProfile &profile) {
  for (unsigned i = 0; i < profile.interfaceCount; ++i) {
    const UsbInterface &interface = profile.interfaces[i];
    if (interface.interfaceClass == 3 && interface.subClass == 1) {
      bootInterfaces[bootInterfaceCount++] = interface.number;
    }
  }
  enabled = true;
}

void taskUsbControl() {
  static uint32_t forGeneration, since;
  static Setup setup;
  static bool waiting;
  static uint8_t toReset;  // boot interfaces still owed SET_PROTOCOL(report)

  if (busReset) {
    busReset = false;
    toReset = bootInterfaceCount;
  }
  if (toReset && !waiting && hostControlResult() != -2) {
    uint64_t setProtocol = 0x21 | 0x0b << 8 | 1 << 16 | uint64_t(bootInterfaces[toReset - 1]) << 32;
    if (hostControl(setProtocol, data)) --toReset;
    else if (hostControlResult() != -2) toReset = 0;  // the device is gone
  }

  noInterrupts();
  if (state == Wanted && (!waiting || generation != forGeneration)) {
    forGeneration = generation;
    setup = pending;
    since = millis();
    waiting = true;
  }
  State now = generation == forGeneration ? (State)state : Idle;
  interrupts();
  if (!waiting) return;
  if (now == Idle || now == Receiving) {  // replaced by a newer request
    waiting = false;
    return;
  }

  bool expired = millis() - since >= TimeoutMs;
  if (now == Wanted) {
    if (hostControl(setup.both, data)) {
      noInterrupts();
      if (generation == forGeneration) state = Forwarding;
      interrupts();
    } else if (expired) {
      waiting = false;
      finish(forGeneration, setup, -1);
    }
    return;
  }
  int result = hostControlResult();
  if (result == -2 && !expired) return;
  waiting = false;
  finish(forGeneration, setup, result == -2 ? -1 : result);
}
