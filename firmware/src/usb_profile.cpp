#include "usb_profile.h"
#include <string.h>

namespace {
uint16_t word(const uint8_t *p) {
  return p[0] | uint16_t(p[1]) << 8;
}

bool validControlPacketSize(uint8_t size) {
  return size == 8 || size == 16 || size == 32 || size == 64;
}

struct Found {
  uint16_t offset, hidOffset, reportLength;
  uint8_t hidLength, endpoints;
};

struct Layout {
  Found interfaces[MaxUsbInterfaces];
  UsbEndpoint endpoints[MaxUsbEndpoints];
  uint8_t interfaceCount, endpointCount;
};

bool readLayout(const uint8_t *configuration, size_t length, Layout &result) {
  result = Layout{};
  if (!configuration || length < 9 + 9 + 7 || length > UsbConfigurationCapacity ||
      configuration[0] != 9 || configuration[1] != 2 || word(configuration + 2) != length ||
      !configuration[4] || configuration[4] > MaxUsbInterfaces) {
    return false;
  }

  int current = -1;
  bool used[2][8] = {};
  for (size_t pos = 9; pos < length;) {
    if (length - pos < 2) return false;
    const uint8_t *d = configuration + pos;
    uint8_t size = d[0], type = d[1];
    if (size < 2 || size > length - pos) return false;
    bool hid = current >= 0 && configuration[result.interfaces[current].offset + 5] == 3;
    if (type == 4) {
      if (size != 9 || d[3] != 0 || result.interfaceCount >= configuration[4]) return false;
      for (unsigned i = 0; i < result.interfaceCount; ++i) {
        if (configuration[result.interfaces[i].offset + 2] == d[2]) return false;
      }
      current = result.interfaceCount++;
      result.interfaces[current].offset = pos;
    } else if (type == 0x21 && hid) {  // other classes reuse this type for their own data
      Found &interface = result.interfaces[current];
      if (interface.hidOffset || size < 9 || size != 6 + 3 * d[5]) return false;
      for (unsigned i = 0; i < d[5]; ++i) {
        const uint8_t *entry = d + 6 + 3 * i;
        if (entry[0] == 0x22) {
          if (interface.reportLength || !word(entry + 1) || word(entry + 1) > UsbReportCapacity) {
            return false;
          }
          interface.reportLength = word(entry + 1);
        }
      }
      if (!interface.reportLength) return false;
      interface.hidOffset = pos;
      interface.hidLength = size;
    } else if (type == 5) {
      if (current < 0 || size != 7 || d[3] != 3 || !d[6]) return false;
      uint8_t number = d[2] & 0x7f;
      bool in = d[2] & 0x80;
      uint16_t packetSize = word(d + 4);
      if (!number || number > 7 || !packetSize || packetSize > 64 || used[in][number]) return false;
      used[in][number] = true;
      result.endpoints[result.endpointCount++] = {number, in, uint8_t(current), d[6], packetSize};
      ++result.interfaces[current].endpoints;
    }
    pos += size;
  }

  if (result.interfaceCount != configuration[4] || !result.endpointCount) return false;
  for (unsigned i = 0; i < result.interfaceCount; ++i) {
    const Found &interface = result.interfaces[i];
    if (interface.endpoints != configuration[interface.offset + 4]) return false;
    if (configuration[interface.offset + 5] != 3) continue;
    bool input = false;  // a HID interface reports through an interrupt IN endpoint
    for (unsigned e = 0; e < result.endpointCount; ++e) {
      input |= result.endpoints[e].interface == i && result.endpoints[e].in;
    }
    if (!interface.hidOffset || !input) return false;
  }
  return true;
}
}

bool validUsbConfiguration(const uint8_t *configuration, size_t length) {
  Layout layout;
  return readLayout(configuration, length, layout);
}

bool readUsbProfile(const uint8_t *device, size_t deviceLength,
                    const uint8_t *configuration, size_t configurationLength,
                    UsbProfile &result) {
  result = UsbProfile{};
  Layout layout;
  if (!device || deviceLength != 18 || device[0] != 18 || device[1] != 1 ||
      !validControlPacketSize(device[7]) || device[17] != 1 ||
      !readLayout(configuration, configurationLength, layout)) {
    return false;
  }
  for (unsigned i = 0; i < layout.interfaceCount; ++i) {
    const Found &found = layout.interfaces[i];
    const uint8_t *d = configuration + found.offset;
    UsbInterface &interface = result.interfaces[i];
    interface.offset = found.offset;
    interface.hidOffset = found.hidOffset;
    interface.hidLength = found.hidLength;
    interface.reportLength = found.reportLength;
    interface.number = d[2];
    interface.interfaceClass = d[5];
    interface.subClass = d[6];
    interface.protocol = d[7];
  }
  result.interfaceCount = layout.interfaceCount;
  memcpy(result.endpoints, layout.endpoints, sizeof result.endpoints);
  result.endpointCount = layout.endpointCount;
  memcpy(result.device, device, 18);
  memcpy(result.configuration, configuration, configurationLength);
  result.configurationLength = configurationLength;
  return true;
}
