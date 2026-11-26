#include <unity.h>
#include <vector>
#include "mouse_layout.h"

void setUp() {}
void tearDown() {}

static const std::vector<uint8_t> mouse = {
  0x05,1, 0x09,2, 0xa1,1, 0x09,1, 0xa1,0,
  0x05,9, 0x19,1, 0x29,3, 0x15,0, 0x25,1, 0x75,1, 0x95,3, 0x81,2,
  0x75,5, 0x95,1, 0x81,1,
  0x05,1, 0x09,0x30, 0x09,0x31, 0x09,0x38, 0x15,0x81, 0x25,0x7f,
  0x75,8, 0x95,3, 0x81,6, 0xc0, 0xc0
};

static MouseLayout parseMouse(const std::vector<uint8_t> &bytes) {
  MouseLayout layout;
  TEST_ASSERT_TRUE(readMouseLayout(bytes.data(), bytes.size(), layout));
  return layout;
}

void test_three_buttons_with_wheel() {
  auto layout = parseMouse(mouse);
  uint8_t packet[MousePacketSize];
  encodeMouse(layout, 0xfd, -2, 19, -1, 12, packet);
  const uint8_t expected[] = {5, 0xfe, 19, 0xff};
  TEST_ASSERT_EQUAL(sizeof expected, layout.bytes);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, packet, layout.bytes);
}

void test_hides_side_buttons_only() {
  auto bytes = mouse;
  bytes[15] = 5; bytes[23] = 5; bytes[27] = 3;  // five buttons, three padding bits
  auto layout = parseMouse(bytes);
  uint8_t packet[] = {0x1f, 1, 2, 3};
  hideMouseButtons(layout, 8 | 16, packet, sizeof packet);
  const uint8_t expected[] = {0x07, 1, 2, 3};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, packet, sizeof packet);

  bytes.insert(bytes.begin() + 6, {0x85,7});
  layout = parseMouse(bytes);
  uint8_t other[] = {9, 0x1f, 1, 2, 3}, same[] = {7, 0x1f, 1, 2, 3};
  hideMouseButtons(layout, 8 | 16, other, sizeof other);  // another report ID: untouched
  hideMouseButtons(layout, 8 | 16, same, sizeof same);
  TEST_ASSERT_EQUAL_HEX8(0x1f, other[1]);
  TEST_ASSERT_EQUAL_HEX8(0x07, same[1]);
}

void test_merges_injected_input_into_a_physical_report() {
  auto bytes = mouse;
  bytes[15] = 5; bytes[23] = 5; bytes[27] = 3;
  auto layout = parseMouse(bytes);
  uint8_t packet[] = {0x01, 120, 0xfe, 5};  // left held, x=120, y=-2, wheel=5
  int tx, ty;
  TEST_ASSERT_TRUE(mergeMouse(layout, 2, 50, -300, packet, sizeof packet, tx, ty));
  const uint8_t expected[] = {0x03, 127, 0x81, 5};  // physical button kept, axes saturate
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, packet, sizeof packet);
  TEST_ASSERT_EQUAL(7, tx);
  TEST_ASSERT_EQUAL(-125, ty);

  uint8_t small[] = {0, 10, 0xf6, 0};
  TEST_ASSERT_TRUE(mergeMouse(layout, 0, -30, 4, small, sizeof small, tx, ty));
  const uint8_t summed[] = {0, 0xec, 0xfa, 0};  // 10-30=-20, -10+4=-6
  TEST_ASSERT_EQUAL_UINT8_ARRAY(summed, small, sizeof small);
  TEST_ASSERT_EQUAL(-30, tx);
  TEST_ASSERT_EQUAL(4, ty);

  uint8_t truncated[] = {0, 1};
  TEST_ASSERT_FALSE(mergeMouse(layout, 1, 5, 5, truncated, sizeof truncated, tx, ty));
  TEST_ASSERT_EQUAL(0, tx);
  TEST_ASSERT_EQUAL_HEX8(0, truncated[0]);
}

void test_merges_into_sixteen_bit_axes() {
  auto bytes = mouse;
  bytes.insert(bytes.begin() + 6, {0x85,7});
  bytes.erase(bytes.end() - 12, bytes.end());
  bytes.insert(bytes.end(), {0x16,1,0x80, 0x26,0xff,0x7f,
                             0x75,16, 0x95,3, 0x81,6, 0xc0, 0xc0});
  auto layout = parseMouse(bytes);
  uint8_t packet[MousePacketSize], other[MousePacketSize];
  encodeMouse(layout, 0, -300, 1000, 0, 0, packet);
  encodeMouse(layout, 0, -400, 1090, 0, 0, other);
  int tx, ty;
  TEST_ASSERT_TRUE(mergeMouse(layout, 0, -100, 90, packet, layout.bytes, tx, ty));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(other, packet, layout.bytes);
  packet[0] = 9;  // another report ID
  TEST_ASSERT_FALSE(mergeMouse(layout, 1, 1, 1, packet, layout.bytes, tx, ty));
}

void test_report_id_and_signed_sixteen_bit_axes() {
  auto bytes = mouse;
  bytes.insert(bytes.begin() + 6, {0x85,7});
  auto axes = bytes.end() - 12;
  bytes.erase(axes, bytes.end());
  bytes.insert(bytes.end(), {0x16,1,0x80, 0x26,0xff,0x7f,
                             0x75,16, 0x95,3, 0x81,6, 0xc0, 0xc0});
  auto layout = parseMouse(bytes);
  uint8_t packet[MousePacketSize];
  encodeMouse(layout, 2, -256, 512, -1, 0, packet);
  const uint8_t expected[] = {7,2,0,0xff,0,2,0xff,0xff};
  TEST_ASSERT_EQUAL(sizeof expected, layout.bytes);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, packet, layout.bytes);
}

void test_deathadder_v3_pro_vendor_fields() {
  static const std::vector<uint8_t> deathadder = {
    0x05,1, 0x09,2, 0xa1,1, 0x09,1, 0xa1,0,
    0x05,9, 0x19,1, 0x29,5, 0x15,0, 0x25,1, 0x75,1, 0x95,5, 0x81,2,
    0x75,1, 0x95,3, 0x81,3,
    0x06,0,0xff, 0x09,0x40, 0x75,8, 0x95,2, 0x15,0x81, 0x25,0x7f, 0x81,2,
    0x05,1, 0x09,0x38, 0x15,0x81, 0x25,0x7f, 0x75,8, 0x95,1, 0x81,6,
    0x09,0x30, 0x09,0x31, 0x16,0,0x80, 0x26,0xff,0x7f, 0x75,16, 0x95,2, 0x81,6,
    0xc0,
    0x06,0,0xff, 0x09,2, 0x15,0, 0x25,1, 0x75,8, 0x95,0x5a, 0xb1,1,
    0xc0
  };
  auto layout = parseMouse(deathadder);
  uint8_t packet[MousePacketSize];
  encodeMouse(layout, 0x15, -2, 19, -1, 0, packet);
  const uint8_t expected[] = {0x15, 0, 0, 0xff, 0xfe, 0xff, 19, 0};
  TEST_ASSERT_EQUAL(sizeof expected, layout.bytes);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, packet, layout.bytes);
}

void test_unsupported_descriptors_select_fallback() {
  MouseLayout layout;
  for (size_t n = 0; n < mouse.size(); ++n) {
    TEST_ASSERT_FALSE(readMouseLayout(mouse.data(), n, layout));
  }
  auto composite = mouse;
  composite.insert(composite.end(), {0x05,1, 0x09,6, 0xa1,1, 0xc0});
  TEST_ASSERT_FALSE(readMouseLayout(composite.data(), composite.size(), layout));
  auto output = mouse;
  output.insert(output.end()-2, {0x91,2});
  TEST_ASSERT_FALSE(readMouseLayout(output.data(), output.size(), layout));
  auto oversized = mouse;
  oversized[oversized.size()-5] = 255; // Report Count
  TEST_ASSERT_FALSE(readMouseLayout(oversized.data(), oversized.size(), layout));
  TEST_ASSERT_FALSE(readMouseLayout(mouse.data(), MaxMouseDescriptor + 1, layout));
  TEST_ASSERT_EQUAL(0, layout.bytes);
}

void test_decodes_what_it_encodes() {
  auto bytes = mouse;
  bytes[15] = 5; bytes[23] = 5; bytes[27] = 3;  // five buttons
  for (bool withId : {false, true}) {
    if (withId) bytes.insert(bytes.begin() + 6, {0x85,7});
    auto layout = parseMouse(bytes);
    uint8_t packet[MousePacketSize];
    encodeMouse(layout, 5, -3, 100, -1, 0, packet);
    uint8_t buttons = 0;
    int x = 0, y = 0, wheel = 0, pan = 9;
    TEST_ASSERT_TRUE(decodeMouse(layout, packet, layout.bytes, buttons, x, y, wheel, pan));
    TEST_ASSERT_EQUAL(5, buttons);
    TEST_ASSERT_EQUAL(-3, x);
    TEST_ASSERT_EQUAL(100, y);
    TEST_ASSERT_EQUAL(-1, wheel);
    TEST_ASSERT_EQUAL(0, pan);  // no pan field in this layout
    TEST_ASSERT_FALSE(decodeMouse(layout, packet, layout.bytes - 1, buttons, x, y, wheel, pan));
    if (withId) {
      packet[0] = 9;
      TEST_ASSERT_FALSE(decodeMouse(layout, packet, layout.bytes, buttons, x, y, wheel, pan));
    }
  }
  MouseLayout none{};
  uint8_t packet[4] = {};
  uint8_t buttons;
  int x, y, wheel, pan;
  TEST_ASSERT_FALSE(decodeMouse(none, packet, sizeof packet, buttons, x, y, wheel, pan));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_decodes_what_it_encodes);
  RUN_TEST(test_three_buttons_with_wheel);
  RUN_TEST(test_hides_side_buttons_only);
  RUN_TEST(test_merges_injected_input_into_a_physical_report);
  RUN_TEST(test_merges_into_sixteen_bit_axes);
  RUN_TEST(test_report_id_and_signed_sixteen_bit_axes);
  RUN_TEST(test_deathadder_v3_pro_vendor_fields);
  RUN_TEST(test_unsupported_descriptors_select_fallback);
  return UNITY_END();
}
