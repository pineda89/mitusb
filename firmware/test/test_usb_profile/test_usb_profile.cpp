#include <unity.h>
#include <string.h>
#include <vector>
#include "usb_profile.h"
#include "usb_device.h"
#include "../fixtures.h"

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

// The endpoint of an interface (by index) in one direction, or null.
static const UsbEndpoint *endpoint(const UsbProfile &profile, unsigned interface, bool in) {
  for (unsigned i = 0; i < profile.endpointCount; ++i) {
    const UsbEndpoint &e = profile.endpoints[i];
    if (e.interface == interface && bool(e.in) == in) return &e;
  }
  return nullptr;
}

static bool reads(const std::vector<uint8_t> &config, const uint8_t *dev = nullptr) {
  UsbProfile profile;
  bool valid = validUsbConfiguration(config.data(), config.size());
  bool read = readUsbProfile(dev ? dev : PadDevice, 18, config.data(), config.size(), profile);
  if (!dev) TEST_ASSERT_EQUAL(valid, read);  // both entry points agree
  return read;
}

void test_copies_device_and_configuration() {
  auto config = configuration();
  UsbProfile profile;
  TEST_ASSERT_TRUE(readUsbProfile(device, sizeof device, config.data(), config.size(), profile));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(device, profile.device, sizeof device);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(config.data(), profile.configuration, config.size());
  TEST_ASSERT_EQUAL(config.size(), profile.configurationLength);
  TEST_ASSERT_EQUAL(1, profile.interfaceCount);
  TEST_ASSERT_EQUAL(0, profile.interfaces[0].number);
  TEST_ASSERT_EQUAL(18, profile.interfaces[0].hidOffset);
  TEST_ASSERT_EQUAL(9, profile.interfaces[0].hidLength);
  TEST_ASSERT_EQUAL(52, profile.interfaces[0].reportLength);
  TEST_ASSERT_EQUAL(1, endpoint(profile, 0, true)->number);
  TEST_ASSERT_EQUAL(8, endpoint(profile, 0, true)->packetSize);
  TEST_ASSERT_EQUAL(4, profile.configuration[17]);
}

void test_keeps_supported_endpoint_number() {
  auto config = configuration(0x83);
  config[31] = 64;
  UsbProfile profile;
  TEST_ASSERT_TRUE(readUsbProfile(device, sizeof device, config.data(), config.size(), profile));
  TEST_ASSERT_EQUAL(3, endpoint(profile, 0, true)->number);
  TEST_ASSERT_EQUAL(64, endpoint(profile, 0, true)->packetSize);
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

  UsbProfile profile;
  TEST_ASSERT_TRUE(readUsbProfile(device, sizeof device, composite.data(), composite.size(), profile));
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
  UsbProfile profile;
  TEST_ASSERT_TRUE(readUsbProfile(device, sizeof device, config.data(), config.size(), profile));
  TEST_ASSERT_EQUAL(3, profile.interfaceCount);
  TEST_ASSERT_EQUAL(1, endpoint(profile, 0, true)->number);
  TEST_ASSERT_EQUAL(2, endpoint(profile, 1, true)->number);
  TEST_ASSERT_EQUAL(3, endpoint(profile, 2, true)->number);
}

void test_rejects_unsupported_descriptors() {
  UsbProfile profile;
  auto config = configuration();
  for (size_t n = 0; n < config.size(); ++n) {
    TEST_ASSERT_FALSE(readUsbProfile(device, sizeof device, config.data(), n, profile));
  }

  auto output = config;
  output[29] = 0x01;
  TEST_ASSERT_FALSE(readUsbProfile(device, sizeof device, output.data(), output.size(), profile));

  auto bulk = config;
  bulk[30] = 2;
  TEST_ASSERT_FALSE(readUsbProfile(device, sizeof device, bulk.data(), bulk.size(), profile));

  auto oversized = config;
  oversized[31] = 65;
  TEST_ASSERT_FALSE(readUsbProfile(device, sizeof device, oversized.data(), oversized.size(), profile));

  auto reservedAddress = configuration(0x91);
  TEST_ASSERT_FALSE(readUsbProfile(device, sizeof device, reservedAddress.data(), reservedAddress.size(), profile));
  oversized = config;
  oversized[32] = 0x20;
  TEST_ASSERT_FALSE(readUsbProfile(device, sizeof device, oversized.data(), oversized.size(), profile));

  auto wrongCount = config;
  wrongCount[4] = 2;
  TEST_ASSERT_FALSE(readUsbProfile(device, sizeof device, wrongCount.data(), wrongCount.size(), profile));

  uint8_t badDevice[18];
  memcpy(badDevice, device, sizeof badDevice);
  badDevice[7] = 4;
  TEST_ASSERT_FALSE(readUsbProfile(badDevice, sizeof badDevice, config.data(), config.size(), profile));
}

void test_interrupt_out_and_in_can_share_an_endpoint_number() {
  auto config = configuration();
  config.insert(config.end(), {7, 5, 0x01, 3, 16, 0, 10});
  config[2] = config.size();
  config[13] = 2;
  UsbProfile profile;
  TEST_ASSERT_TRUE(readUsbProfile(device, sizeof device, config.data(), config.size(), profile));
  TEST_ASSERT_EQUAL(1, endpoint(profile, 0, false)->number);
  TEST_ASSERT_EQUAL(16, endpoint(profile, 0, false)->packetSize);
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

static void expectEndpoint(const UsbProfile &profile, unsigned i, uint8_t number, bool in,
                           uint8_t interface, uint16_t size, uint8_t interval) {
  const UsbEndpoint &e = profile.endpoints[i];
  TEST_ASSERT_EQUAL(number, e.number);
  TEST_ASSERT_EQUAL(in, bool(e.in));
  TEST_ASSERT_EQUAL(interface, e.interface);
  TEST_ASSERT_EQUAL(size, e.packetSize);
  TEST_ASSERT_EQUAL(interval, e.interval);
}

void test_pad_profile() {
  UsbProfile profile;
  TEST_ASSERT_TRUE(readUsbProfile(PadDevice, sizeof PadDevice, PadConfiguration, sizeof PadConfiguration, profile));
  TEST_ASSERT_EQUAL(3, profile.interfaceCount);
  TEST_ASSERT_EQUAL(5, profile.endpointCount);
  const UsbInterface &pad = profile.interfaces[0], &keyboard = profile.interfaces[1];
  TEST_ASSERT_EQUAL_HEX8(0xff, pad.interfaceClass);
  TEST_ASSERT_EQUAL_HEX8(0x5d, pad.subClass);
  TEST_ASSERT_EQUAL_HEX8(0x01, pad.protocol);
  TEST_ASSERT_EQUAL(0, pad.hidOffset);  // its 0x21 descriptor is not HID
  TEST_ASSERT_EQUAL(0, pad.reportLength);
  TEST_ASSERT_EQUAL(3, keyboard.interfaceClass);
  TEST_ASSERT_EQUAL(1, keyboard.subClass);
  TEST_ASSERT_EQUAL(1, keyboard.protocol);
  TEST_ASSERT_EQUAL(sizeof PadKeyboardReport, keyboard.reportLength);
  TEST_ASSERT_EQUAL(sizeof PadVendorReport, profile.interfaces[2].reportLength);
  expectEndpoint(profile, 0, 4, true, 0, 32, 1);
  expectEndpoint(profile, 1, 5, false, 0, 32, 1);
  expectEndpoint(profile, 2, 2, true, 1, 16, 8);
  expectEndpoint(profile, 3, 3, true, 2, 64, 1);
  expectEndpoint(profile, 4, 6, false, 2, 64, 1);
}

void test_xbox360_profile() {
  UsbProfile profile;
  TEST_ASSERT_TRUE(readUsbProfile(Xbox360Device, sizeof Xbox360Device,
                                  Xbox360Configuration, sizeof Xbox360Configuration, profile));
  TEST_ASSERT_EQUAL(4, profile.interfaceCount);
  TEST_ASSERT_EQUAL(7, profile.endpointCount);
  for (unsigned i = 0; i < 4; ++i) TEST_ASSERT_EQUAL(0, profile.interfaces[i].hidOffset);
  TEST_ASSERT_NULL(endpoint(profile, 3, true));
  TEST_ASSERT_NULL(endpoint(profile, 3, false));
  TEST_ASSERT_TRUE(validUsbConfiguration(Xbox360Configuration, sizeof Xbox360Configuration));
}

// Offsets in PadConfiguration: interface 0 at 9, its endpoints at 35 and 42.
void test_rejects_what_the_bridge_cannot_carry() {
  const std::vector<uint8_t> pad(PadConfiguration, PadConfiguration + sizeof PadConfiguration);
  TEST_ASSERT_TRUE(reads(pad));

  auto config = pad;
  config[35 + 3] = 1;  // isochronous
  TEST_ASSERT_FALSE(reads(config));
  config = pad;
  config[35 + 3] = 2;  // bulk
  TEST_ASSERT_FALSE(reads(config));
  config = pad;
  config[9 + 3] = 1;  // alternate setting
  TEST_ASSERT_FALSE(reads(config));
  config = pad;
  config[35 + 2] = 0x88;  // endpoint 8
  TEST_ASSERT_FALSE(reads(config));
  config = pad;
  config[35 + 2] = 0x82;  // IN 2 twice
  TEST_ASSERT_FALSE(reads(config));
  config = pad;
  config[35 + 4] = 65;
  TEST_ASSERT_FALSE(reads(config));
  config = pad;
  config[4] = 9;
  TEST_ASSERT_FALSE(reads(config));
  config = pad;
  config[99] = 200;  // the last descriptor claims to run past the end
  TEST_ASSERT_FALSE(reads(config));
  config = pad;
  config[18] = 1;  // a descriptor shorter than its own header
  TEST_ASSERT_FALSE(reads(config));
  config = pad;
  config.resize(config.size() - 10);  // shorter than wTotalLength
  TEST_ASSERT_FALSE(reads(config));

  uint8_t twoConfigurations[18];
  memcpy(twoConfigurations, PadDevice, 18);
  twoConfigurations[17] = 2;
  TEST_ASSERT_FALSE(reads(pad, twoConfigurations));
  TEST_ASSERT_FALSE(validUsbConfiguration(nullptr, 0));
}

void test_nine_interfaces_do_not_fit() {
  std::vector<uint8_t> config = {9, 2, 0, 0, 9, 1, 0, 0x80, 50};
  for (uint8_t i = 0; i < 9; ++i) config.insert(config.end(), {9, 4, i, 0, 0, 0xff, 0, 0, 0});
  config.insert(config.end(), {7, 5, 0x81, 3, 8, 0, 1});
  config[9 * 9 + 4] = 1;
  config[2] = config.size();
  TEST_ASSERT_FALSE(reads(config));
  config.erase(config.begin() + 9, config.begin() + 18);  // drop one interface: eight fit
  config[2] = config.size();
  config[4] = 8;
  TEST_ASSERT_TRUE(reads(config));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_pad_profile);
  RUN_TEST(test_xbox360_profile);
  RUN_TEST(test_rejects_what_the_bridge_cannot_carry);
  RUN_TEST(test_nine_interfaces_do_not_fit);
  RUN_TEST(test_copies_device_and_configuration);
  RUN_TEST(test_keeps_supported_endpoint_number);
  RUN_TEST(test_preserves_all_composite_interfaces);
  RUN_TEST(test_deathadder_three_interface_configuration);
  RUN_TEST(test_rejects_unsupported_descriptors);
  RUN_TEST(test_interrupt_out_and_in_can_share_an_endpoint_number);
  RUN_TEST(test_packet_length_uses_only_completed_bytes);
  return UNITY_END();
}
