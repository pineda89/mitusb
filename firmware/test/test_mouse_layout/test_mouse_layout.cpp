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

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_three_buttons_with_wheel);
  RUN_TEST(test_report_id_and_signed_sixteen_bit_axes);
  RUN_TEST(test_deathadder_v3_pro_vendor_fields);
  RUN_TEST(test_unsupported_descriptors_select_fallback);
  return UNITY_END();
}
