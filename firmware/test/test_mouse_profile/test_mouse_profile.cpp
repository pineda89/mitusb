#include <unity.h>
#include <string.h>
#include <vector>
#include "mouse_profile.h"
#include "mouse_device.h"

void setUp() {}
void tearDown() {}

static const uint8_t device[] = {
  18, 1, 0x10, 1, 0, 0, 0, 8,
  0x4d, 0x04, 0x3c, 0xc0, 0x34, 0x12, 1, 2, 3, 1
};

static std::vector<uint8_t> configuration(uint8_t endpoint = 0x81) {
  return {
    9, 2, 34, 0, 1, 1, 0, 0x80, 50,
    9, 4, 0, 0, 1, 3, 1, 2, 4,
    9, 0x21, 0x10, 1, 0, 1, 0x22, 52, 0,
    7, 5, endpoint, 3, 8, 0, 10
  };
}

void test_copies_device_and_configuration() {
  auto config = configuration();
  MouseProfile profile;
  TEST_ASSERT_TRUE(readMouseProfile(device, sizeof device, config.data(), config.size(), profile));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(device, profile.device, sizeof device);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(config.data(), profile.configuration, config.size());
  TEST_ASSERT_EQUAL(config.size(), profile.configurationLength);
  TEST_ASSERT_EQUAL(1, profile.interfaceCount);
  TEST_ASSERT_EQUAL(0, profile.interfaces[0].number);
  TEST_ASSERT_EQUAL(18, profile.interfaces[0].hidOffset);
  TEST_ASSERT_EQUAL(9, profile.interfaces[0].hidLength);
  TEST_ASSERT_EQUAL(52, profile.interfaces[0].reportLength);
  TEST_ASSERT_EQUAL(1, profile.interfaces[0].inEndpoint);
  TEST_ASSERT_EQUAL(8, profile.interfaces[0].inPacketSize);
  TEST_ASSERT_EQUAL(4, profile.configuration[17]);
}

void test_keeps_supported_endpoint_number() {
  auto config = configuration(0x83);
  config[31] = 64;
  MouseProfile profile;
  TEST_ASSERT_TRUE(readMouseProfile(device, sizeof device, config.data(), config.size(), profile));
  TEST_ASSERT_EQUAL(3, profile.interfaces[0].inEndpoint);
  TEST_ASSERT_EQUAL(64, profile.interfaces[0].inPacketSize);
  TEST_ASSERT_EQUAL_HEX8(0x83, profile.configuration[29]);
}

void test_preserves_all_composite_interfaces() {
  auto mouse = configuration();
  mouse[11] = 1;
  mouse[17] = 5;
  const std::vector<uint8_t> vendor = {
    9, 4, 0, 0, 1, 3, 0, 0, 6,
    9, 0x21, 0x10, 1, 0, 1, 0x22, 10, 0,
    7, 5, 0x83, 3, 16, 0, 1
  };
  std::vector<uint8_t> composite(mouse.begin(), mouse.begin() + 9);
  composite.insert(composite.end(), vendor.begin(), vendor.end());
  composite.insert(composite.end(), mouse.begin() + 9, mouse.end());
  composite[2] = composite.size();
  composite[3] = composite.size() >> 8;
  composite[4] = 2;

  MouseProfile profile;
  TEST_ASSERT_TRUE(readMouseProfile(device, sizeof device, composite.data(), composite.size(), profile));
  TEST_ASSERT_EQUAL(composite.size(), profile.configurationLength);
  TEST_ASSERT_EQUAL(2, profile.configuration[4]);
  TEST_ASSERT_EQUAL(2, profile.interfaceCount);
  TEST_ASSERT_EQUAL(0, profile.interfaces[0].number);
  TEST_ASSERT_EQUAL(1, profile.interfaces[1].number);
  TEST_ASSERT_EQUAL(5, profile.configuration[profile.interfaces[1].offset + 8]);
  TEST_ASSERT_EQUAL_HEX8(0x81, profile.configuration[54]);
  TEST_ASSERT_EQUAL_HEX8(0x83, profile.configuration[29]);
}

void test_deathadder_three_interface_configuration() {
  std::vector<uint8_t> config = {
    9, 2, 84, 0, 3, 1, 0, 0xe0, 250
  };
  const uint8_t lengths[] = {94, 159, 61};
  for (unsigned i = 0; i < 3; ++i) {
    const std::vector<uint8_t> interface = {
      9, 4, uint8_t(i), 0, 1, 3, 1, uint8_t(i == 0 ? 2 : 1), 0,
      9, 0x21, 0x11, 1, 0, 1, 0x22, lengths[i], 0,
      7, 5, uint8_t(0x81 + i), 3, 64, 0, 1
    };
    config.insert(config.end(), interface.begin(), interface.end());
  }
  MouseProfile profile;
  TEST_ASSERT_TRUE(readMouseProfile(device, sizeof device, config.data(), config.size(), profile));
  TEST_ASSERT_EQUAL(3, profile.interfaceCount);
  TEST_ASSERT_EQUAL(1, profile.interfaces[0].inEndpoint);
  TEST_ASSERT_EQUAL(2, profile.interfaces[1].inEndpoint);
  TEST_ASSERT_EQUAL(3, profile.interfaces[2].inEndpoint);
}

void test_rejects_unsupported_descriptors() {
  MouseProfile profile;
  auto config = configuration();
  for (size_t n = 0; n < config.size(); ++n) {
    TEST_ASSERT_FALSE(readMouseProfile(device, sizeof device, config.data(), n, profile));
  }

  auto output = config;
  output[29] = 0x01;
  TEST_ASSERT_FALSE(readMouseProfile(device, sizeof device, output.data(), output.size(), profile));

  auto bulk = config;
  bulk[30] = 2;
  TEST_ASSERT_FALSE(readMouseProfile(device, sizeof device, bulk.data(), bulk.size(), profile));

  auto oversized = config;
  oversized[31] = 65;
  TEST_ASSERT_FALSE(readMouseProfile(device, sizeof device, oversized.data(), oversized.size(), profile));

  auto reservedAddress = configuration(0x91);
  TEST_ASSERT_FALSE(readMouseProfile(device, sizeof device, reservedAddress.data(), reservedAddress.size(), profile));
  oversized = config;
  oversized[32] = 0x20;
  TEST_ASSERT_FALSE(readMouseProfile(device, sizeof device, oversized.data(), oversized.size(), profile));

  auto wrongCount = config;
  wrongCount[4] = 2;
  TEST_ASSERT_FALSE(readMouseProfile(device, sizeof device, wrongCount.data(), wrongCount.size(), profile));

  uint8_t badDevice[18];
  memcpy(badDevice, device, sizeof badDevice);
  badDevice[7] = 4;
  TEST_ASSERT_FALSE(readMouseProfile(badDevice, sizeof badDevice, config.data(), config.size(), profile));
}

void test_interrupt_out_and_in_can_share_an_endpoint_number() {
  auto config = configuration();
  config.insert(config.end(), {7, 5, 0x01, 3, 16, 0, 10});
  config[2] = config.size();
  config[13] = 2;
  MouseProfile profile;
  TEST_ASSERT_TRUE(readMouseProfile(device, sizeof device, config.data(), config.size(), profile));
  TEST_ASSERT_EQUAL(1, profile.interfaces[0].outEndpoint);
  TEST_ASSERT_EQUAL(16, profile.interfaces[0].outPacketSize);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(config.data(), profile.configuration, config.size());
}

void test_packet_length_uses_only_completed_bytes() {
  TEST_ASSERT_EQUAL(64, completedPacketLength(64, 0));
  TEST_ASSERT_EQUAL(8, completedPacketLength(64, 56u << 16));
  TEST_ASSERT_EQUAL(0, completedPacketLength(64, 64u << 16));
  TEST_ASSERT_EQUAL(0, completedPacketLength(64, 65u << 16));
  TEST_ASSERT_EQUAL(0, completedPacketLength(64, 0x80));
  TEST_ASSERT_EQUAL(0, completedPacketLength(64, 0x40));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_copies_device_and_configuration);
  RUN_TEST(test_keeps_supported_endpoint_number);
  RUN_TEST(test_preserves_all_composite_interfaces);
  RUN_TEST(test_deathadder_three_interface_configuration);
  RUN_TEST(test_rejects_unsupported_descriptors);
  RUN_TEST(test_interrupt_out_and_in_can_share_an_endpoint_number);
  RUN_TEST(test_packet_length_uses_only_completed_bytes);
  return UNITY_END();
}
