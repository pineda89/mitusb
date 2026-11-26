// mitusb: USB HID proxy for the Teensy 4.1.
#include <Arduino.h>
#include <QNEthernet.h>
#include <usb_mouse.h>

#include "protocol.h"
#include "mouse_action.h"
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
static uint8_t notifiedButtons = 0;
static IPAddress subscriber;
static uint16_t subscriberPort = 0;

static FrameClock frameClock;
static MouseAction action;

static bool sendReport(uint8_t buttons, int x, int y, int wheel, int pan) {
  buttons |= injected.buttons;
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

// Pressing left+right+back+forward together freezes the mouse for a second and toggles
// the mode: back and forward are hidden from the computer and subscribers get events.
static const uint8_t ModeButtons = 1 | 2 | 8 | 16, SideButtons = 8 | 16;

static void toggleSideButtons() {
  hiddenMouseButtons ^= SideButtons;
  mouseFrozen = true;
  action.cancel();
  noInterrupts();
  injected = {};
  interrupts();
  physicalButtons = notifiedButtons = 0;
  setButtons(0);  // do not leave the four buttons held on the computer during the pause
  delay(1000);
  noInterrupts();
  mouse.mouseDataClear();
  interrupts();
  mouseFrozen = false;
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

  static bool modeButtonsHeld = false;
  bool held = buttons == ModeButtons;
  if (held && !modeButtonsHeld) {
    modeButtonsHeld = true;  // toggle once per press, not on every report while held
    toggleSideButtons();
    return;
  }
  modeButtonsHeld = held;

  uint8_t visible = buttons & ~hiddenMouseButtons;
  if (!copiedProfile) {
    if (visible != physicalButtons) setButtons(physicalButtons = visible);
    if (dx || dy || wheel || hwheel) {  // this report also carries the pending action
      dx += injected.x;
      dy += injected.y;
      injected.x = injected.y = 0;
      injected.sentButtons = injected.buttons;
      injected.merged = true;
    }
    sendMove(dx, dy, wheel, hwheel);
  } else {
    physicalButtons = visible;
  }

  // Only with the mode on, and only button changes and wheel steps: movement would flood the subscriber.
  if (subscriberPort && hiddenMouseButtons && (buttons != notifiedButtons || wheel)) {
    notifiedButtons = buttons;
    char line[32];
    int n = snprintf(line, sizeof line, "mouse %u %d\n", buttons, wheel);
    udp.send(subscriber, subscriberPort, (const uint8_t*)line, n);
  }
}

// Human-like action from the ethmouse firmware, paced by the 1 ms USB frame. As there,
// synthetic and physical movement share one report: while the mouse is moving its reports
// carry the action, otherwise the action sends its own report each frame.
static void advanceAction() {
  static uint32_t lastTick = 0;
  uint32_t tick = frameClock.update(USB1_FRINDEX >> 3);
  if (tick == lastTick) return;
  lastTick = tick;

  MouseActionUpdate u = action.update(tick);
  noInterrupts();
  injected.x += u.dx;
  injected.y += u.dy;
  injected.buttons = u.buttons;
  bool edge = injected.buttons != injected.sentButtons;
  bool carried = injected.merged && !edge;  // button edges never wait for the mouse
  injected.merged = false;
  int sx = 0, sy = 0;
  if (!carried) {
    sx = nextStep(injected.x);
    sy = nextStep(injected.y);
    injected.x -= sx;
    injected.y -= sy;
  }
  interrupts();
  if (carried || (!edge && !sx && !sy)) return;

  bool sent = sendReport(physicalButtons, sx, sy, 0, 0);
  noInterrupts();
  if (sent) {
    injected.sentButtons = u.buttons;
  } else {  // retried next frame
    injected.x += sx;
    injected.y += sy;
  }
  interrupts();
}

static void handleCommand() {
  int length = udp.parsePacket();
  if (length <= 0) return;
  MouseActionCommand human;
  if (length == 17 && decodeMouseActionPacket((const int8_t*)udp.data(), human)) {
    if (copiedProfile && !layout.bytes) return;  // no way to encode reports
    if (!action.start(human, frameClock.update(USB1_FRINDEX >> 3))) return;
    const uint8_t ack = 121;
    udp.send(udp.remoteIP(), udp.remotePort(), &ack, 1);
    return;
  }
  Command c;
  if (!parse(udp.data(), length, c)) return;
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
  MDNS.begin("mitusb");  // reachable as mitusb.local, no IP lookup needed
  udp.begin(PORT);
}

void loop() {
  taskMouseHost();
  pollMouseDevice();
  forwardPhysicalMouse();
  handleCommand();
  advanceAction();
}
