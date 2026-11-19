// mitusb: USB HID proxy for the Teensy 4.1.
#include <Arduino.h>
#include <QNEthernet.h>
#include <usb_mouse.h>

#include "protocol.h"
#include "mouse_capture.h"
#include "mouse_device.h"
#include "mouse_layout.h"

using namespace qindesign::network;

static const uint16_t PORT = 4242;

EthernetUDP udp;
static MouseLayout layout;
static MouseProfile profile;
static bool copiedProfile;
static_assert(MousePacketSize <= 64, "report must fit endpoint buffers");

static uint8_t physicalButtons = 0;
static IPAddress subscriber;
static uint16_t subscriberPort = 0;

static bool sendReport(uint8_t buttons, int x, int y, int wheel, int pan) {
  if (copiedProfile) {
    if (!layout.bytes) return false;
    uint8_t packet[MousePacketSize];
    encodeMouse(layout, buttons, x, y, wheel, pan, packet);
    return sendMousePacket(packet, layout.bytes);
  }
  usb_mouse_buttons_state = buttons;
  return usb_mouse_move(x, y, wheel, pan) == 0;
}

static void setButtons(uint8_t buttons) {
  sendReport(buttons, 0, 0, 0, 0);
}

static void sendMove(int dx, int dy, int wheel, int hwheel = 0) {
  while (dx || dy || wheel || hwheel) {
    int sx = nextStep(dx), sy = nextStep(dy), sw = nextStep(wheel), sh = nextStep(hwheel);
    if (!sendReport(physicalButtons, sx, sy, sw, sh)) return;
    dx -= sx; dy -= sy; wheel -= sw; hwheel -= sh;
  }
}

static void forwardPhysicalMouse() {
  noInterrupts();
  bool detached = copiedProfile ? !mouse.isCaptured() : !mouse.source;
  if (detached || !mouse.available()) {
    if (detached) mouse.mouseDataClear();
    interrupts();
    if (detached && physicalButtons) {
      physicalButtons = 0;
      setButtons(physicalButtons);
    }
    return;
  }
  int dx = mouse.getMouseX(), dy = mouse.getMouseY();
  int wheel = mouse.getWheel(), hwheel = mouse.getWheelH();
  uint8_t buttons = mouse.getButtons();
  mouse.mouseDataClear();
  interrupts();

  if (!copiedProfile) {
    if (buttons != physicalButtons) setButtons(physicalButtons = buttons);
    sendMove(dx, dy, wheel, hwheel);
  } else {
    physicalButtons = buttons;
  }

  if (subscriberPort) {
    char line[48];
    int n = snprintf(line, sizeof line, "mouse %u %d %d %d\n", buttons, dx, dy, wheel);
    udp.send(subscriber, subscriberPort, (const uint8_t*)line, n);
  }
}

static void handleCommand() {
  int length = udp.parsePacket();
  Command c;
  if (length <= 0 || !parse(udp.data(), length, c)) return;
  switch (c.type) {
    case Command::Hello:
      subscriber = udp.remoteIP();
      subscriberPort = udp.remotePort();
      break;
    case Command::Profile: {
      const char *profile = copiedProfile ? "copied\n" : "generic\n";
      udp.send(udp.remoteIP(), udp.remotePort(), (const uint8_t*)profile, strlen(profile));
      break;
    }
    case Command::Move: sendMove(c.a, c.b, 0); break;
    case Command::Wheel: sendMove(0, 0, c.a); break;
    case Command::Click:
      setButtons(physicalButtons | c.a);
      setButtons(physicalButtons);
      break;
  }
}

void setup() {
  beginMouseHost();
  uint8_t mouseInterface;
  bool captured = probeMouse(profile, layout, mouseInterface);
  copiedProfile = startMouseDevice(captured ? &profile : nullptr, mouseInterface);
  Ethernet.setHostname("mitusb");
  Ethernet.begin();
  udp.begin(PORT);
}

void loop() {
  taskMouseHost();
  pollMouseDevice();
  forwardPhysicalMouse();
  handleCommand();
}
