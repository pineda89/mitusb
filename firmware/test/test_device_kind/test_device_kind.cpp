#include <unity.h>
#include <string.h>
#include <vector>
#include "device_kind.h"
#include "../fixtures.h"

static const std::vector<uint8_t> mouseReport = {  // five buttons, x, y, wheel
  0x05,1, 0x09,2, 0xa1,1, 0x09,1, 0xa1,0,
  0x05,9, 0x19,1, 0x29,5, 0x15,0, 0x25,1, 0x75,1, 0x95,5, 0x81,2,
  0x75,3, 0x95,1, 0x81,1,
  0x05,1, 0x09,0x30, 0x09,0x31, 0x09,0x38, 0x15,0x81, 0x25,0x7f,
  0x75,8, 0x95,3, 0x81,6, 0xc0, 0xc0
};
static const uint8_t device[] = {18, 1, 0x10, 1, 0, 0, 0, 8, 0x4d, 0x04, 0x3c, 0xc0, 0x34, 0x12, 1, 2, 3, 1};

static UsbProfile profile;
static const DeviceKind *kinds[MaxUsbInterfaces];

void setUp() {
  mouseLayout = MouseLayout{};
  mouseFrozen = false;
  hiddenMouseButtons = 0;
  injected = {};
  physicalMouse = {};
}
void tearDown() {}

static void setReport(unsigned interface, const uint8_t *report, size_t length) {
  TEST_ASSERT_EQUAL(length, profile.interfaces[interface].reportLength);
  memcpy(profile.interfaces[interface].report, report, length);
}

// One interface of the given class with one interrupt IN endpoint.
static void single(uint8_t cls, uint8_t sub, uint8_t protocol, const std::vector<uint8_t> &report) {
  std::vector<uint8_t> config = {9, 2, 0, 0, 1, 1, 0, 0x80, 50, 9, 4, 0, 0, 1, cls, sub, protocol, 0};
  if (cls == 3) config.insert(config.end(), {9, 0x21, 0x10, 1, 0, 1, 0x22, uint8_t(report.size()), 0});
  config.insert(config.end(), {7, 5, 0x81, 3, 8, 0, 1});
  config[2] = config.size();
  TEST_ASSERT_TRUE(readUsbProfile(device, sizeof device, config.data(), config.size(), profile));
  if (cls == 3) setReport(0, report.data(), report.size());
}

void test_pad_kinds() {
  TEST_ASSERT_TRUE(readUsbProfile(PadDevice, sizeof PadDevice, PadConfiguration, sizeof PadConfiguration, profile));
  setReport(1, PadKeyboardReport, sizeof PadKeyboardReport);
  setReport(2, PadVendorReport, sizeof PadVendorReport);
  TEST_ASSERT_EQUAL_PTR(&gamepadKind, kindOf(profile, 0));
  TEST_ASSERT_EQUAL_PTR(&keyboardKind, kindOf(profile, 1));  // not a mouse, despite its mouse collection
  TEST_ASSERT_NULL(kindOf(profile, 2));
  TEST_ASSERT_EQUAL_STRING("gamepad", kindName(profile));
  attachKinds(profile, kinds);
  TEST_ASSERT_EQUAL_PTR(&gamepadKind, kinds[0]);
  TEST_ASSERT_EQUAL_PTR(&keyboardKind, kinds[1]);
  TEST_ASSERT_NULL(kinds[2]);
  TEST_ASSERT_EQUAL(0, mouseLayout.bytes);
  TEST_ASSERT_NULL(keyboardKind.input);  // pure proxy
  TEST_ASSERT_NULL(gamepadKind.input);
}

void test_xbox360_kinds() {
  TEST_ASSERT_TRUE(readUsbProfile(Xbox360Device, sizeof Xbox360Device,
                                  Xbox360Configuration, sizeof Xbox360Configuration, profile));
  TEST_ASSERT_EQUAL_PTR(&gamepadKind, kindOf(profile, 0));
  for (unsigned i = 1; i < 4; ++i) TEST_ASSERT_NULL(kindOf(profile, i));
  TEST_ASSERT_EQUAL_STRING("gamepad", kindName(profile));
}

void test_mouse_kind() {
  single(3, 1, 2, mouseReport);
  TEST_ASSERT_EQUAL_PTR(&mouseKind, kindOf(profile, 0));
  TEST_ASSERT_EQUAL_STRING("mouse", kindName(profile));
  attachKinds(profile, kinds);
  TEST_ASSERT_EQUAL_PTR(&mouseKind, kinds[0]);
  TEST_ASSERT_EQUAL(4, mouseLayout.bytes);
}

void test_other_kinds() {
  single(3, 0, 0, {0x05,1, 0x09,5, 0xa1,1, 0xc0});  // HID gamepad
  TEST_ASSERT_EQUAL_PTR(&gamepadKind, kindOf(profile, 0));
  single(3, 0, 0, {0x05,1, 0x09,4, 0xa1,1, 0xc0});  // HID joystick
  TEST_ASSERT_EQUAL_PTR(&gamepadKind, kindOf(profile, 0));
  single(3, 0, 0, {0x05,1, 0x09,6, 0xa1,1, 0xc0});  // keyboard without the boot subclass
  TEST_ASSERT_EQUAL_PTR(&keyboardKind, kindOf(profile, 0));
  TEST_ASSERT_EQUAL_STRING("keyboard", kindName(profile));
  single(0xff, 0, 0, {});
  TEST_ASSERT_NULL(kindOf(profile, 0));
  TEST_ASSERT_EQUAL_STRING("other", kindName(profile));
  attachKinds(profile, kinds);
  TEST_ASSERT_NULL(kinds[0]);
}

void test_first_usage() {
  uint16_t page = 0, usage = 0;
  TEST_ASSERT_TRUE(firstUsage(PadVendorReport, sizeof PadVendorReport, page, usage));
  TEST_ASSERT_EQUAL_HEX16(0xff7a, page);
  TEST_ASSERT_EQUAL(1, usage);
  TEST_ASSERT_TRUE(firstUsage(PadKeyboardReport, sizeof PadKeyboardReport, page, usage));
  TEST_ASSERT_EQUAL(1, page);
  TEST_ASSERT_EQUAL(6, usage);
  TEST_ASSERT_FALSE(firstUsage(PadKeyboardReport, 5, page, usage));  // cut before the collection
  const uint8_t truncated[] = {0x05, 1, 0x0a, 6};  // a two-byte item with one byte left
  TEST_ASSERT_FALSE(firstUsage(truncated, sizeof truncated, page, usage));
  TEST_ASSERT_FALSE(firstUsage(nullptr, 0, page, usage));
}

void test_mouse_input_filter() {
  single(3, 1, 2, mouseReport);
  attachKinds(profile, kinds);
  uint8_t packet[MousePacketSize];

  hiddenMouseButtons = 8 | 16;
  encodeMouse(mouseLayout, 1 | 8, 0, 0, -2, 0, packet);
  TEST_ASSERT_TRUE(mouseKind.input(packet, mouseLayout.bytes));
  TEST_ASSERT_EQUAL_HEX8(1, packet[0]);  // back is hidden from the computer
  TEST_ASSERT_EQUAL(1 | 8, physicalMouse.buttons);  // but still seen by the board
  TEST_ASSERT_EQUAL(-2, physicalMouse.wheel);
  TEST_ASSERT_TRUE(physicalMouse.changed);
  mouseKind.sent(true);
  encodeMouse(mouseLayout, 0, 0, 0, -1, 0, packet);
  TEST_ASSERT_TRUE(mouseKind.input(packet, mouseLayout.bytes));
  TEST_ASSERT_EQUAL(-3, physicalMouse.wheel);  // accumulates until read

  injected = {};  // the main loop clears `merged` every frame
  injected.x = 5;
  injected.buttons = 2;
  encodeMouse(mouseLayout, 0, 1, 0, 0, 0, packet);
  TEST_ASSERT_TRUE(mouseKind.input(packet, mouseLayout.bytes));
  TEST_ASSERT_EQUAL_HEX8(2, packet[0]);
  TEST_ASSERT_EQUAL(6, int8_t(packet[1]));
  mouseKind.sent(false);  // not delivered: the movement is still owed
  TEST_ASSERT_EQUAL(5, injected.x);
  TEST_ASSERT_FALSE(injected.merged);
  encodeMouse(mouseLayout, 0, 1, 0, 0, 0, packet);
  TEST_ASSERT_TRUE(mouseKind.input(packet, mouseLayout.bytes));
  mouseKind.sent(true);
  TEST_ASSERT_EQUAL(0, injected.x);
  TEST_ASSERT_EQUAL(2, injected.sentButtons);
  TEST_ASSERT_TRUE(injected.merged);

  mouseFrozen = true;
  TEST_ASSERT_FALSE(mouseKind.input(packet, mouseLayout.bytes));
  mouseFrozen = false;
  hiddenMouseButtons = 0;
  physicalMouse = {};
  uint8_t shortPacket[] = {0x1f};  // too short to be a report of this layout: forwarded untouched
  TEST_ASSERT_TRUE(mouseKind.input(shortPacket, sizeof shortPacket));
  TEST_ASSERT_EQUAL_HEX8(0x1f, shortPacket[0]);
  TEST_ASSERT_FALSE(physicalMouse.changed);
}

void test_only_one_interface_gets_the_mouse_filter() {
  std::vector<uint8_t> config = {9, 2, 0, 0, 2, 1, 0, 0x80, 50};
  for (uint8_t i = 0; i < 2; ++i) {
    config.insert(config.end(), {9, 4, i, 0, 1, 3, 1, 2, 0,
                                 9, 0x21, 0x10, 1, 0, 1, 0x22, uint8_t(mouseReport.size()), 0,
                                 7, 5, uint8_t(0x81 + i), 3, 8, 0, 1});
  }
  config[2] = config.size();
  TEST_ASSERT_TRUE(readUsbProfile(device, sizeof device, config.data(), config.size(), profile));
  setReport(0, mouseReport.data(), mouseReport.size());
  setReport(1, mouseReport.data(), mouseReport.size());
  attachKinds(profile, kinds);
  TEST_ASSERT_EQUAL_PTR(&mouseKind, kinds[0]);
  TEST_ASSERT_NULL(kinds[1]);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_pad_kinds);
  RUN_TEST(test_xbox360_kinds);
  RUN_TEST(test_mouse_kind);
  RUN_TEST(test_other_kinds);
  RUN_TEST(test_first_usage);
  RUN_TEST(test_mouse_input_filter);
  RUN_TEST(test_only_one_interface_gets_the_mouse_filter);
  return UNITY_END();
}
