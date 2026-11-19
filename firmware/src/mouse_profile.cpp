#include "mouse_profile.h"
#include <string.h>

namespace {
uint16_t word(const uint8_t *p) {
  return p[0] | uint16_t(p[1]) << 8;
}

bool validControlPacketSize(uint8_t size) {
  return size == 8 || size == 16 || size == 32 || size == 64;
}

}

bool readMouseProfile(const uint8_t *device, size_t deviceLength,
                      const uint8_t *configuration, size_t configurationLength,
                      MouseProfile &result) {
  result = MouseProfile{};
  if (!device || deviceLength != 18 || device[0] != 18 || device[1] != 1 ||
      !validControlPacketSize(device[7]) || device[17] != 1 ||
      !configuration || configurationLength < 9 + 9 + 9 + 7 ||
      configurationLength > MouseConfigurationCapacity ||
      configuration[0] != 9 || configuration[1] != 2 ||
      word(configuration + 2) != configurationLength || !configuration[4] ||
      configuration[4] > MaxMouseInterfaces) {
    return false;
  }

  int current = -1;
  for (size_t pos = 9; pos < configurationLength;) {
    if (configurationLength - pos < 2) return false;
    uint8_t length = configuration[pos];
    uint8_t type = configuration[pos + 1];
    if (length < 2 || length > configurationLength - pos) return false;
    if (type == 4) {
      if (length != 9 || configuration[pos + 3] != 0 || configuration[pos + 5] != 3 ||
          configuration[pos + 4] < 1 || configuration[pos + 4] > 2 ||
          result.interfaceCount >= configuration[4]) {
        return false;
      }
      for (unsigned i = 0; i < result.interfaceCount; ++i) {
        if (result.interfaces[i].number == configuration[pos + 2]) return false;
      }
      current = result.interfaceCount++;
      MouseHidInterface &interface = result.interfaces[current];
      interface.number = configuration[pos + 2];
      interface.offset = pos;
    } else if (type == 0x21) {
      if (current < 0) return false;
      MouseHidInterface &interface = result.interfaces[current];
      if (interface.hidOffset || length < 9 || length != 6 + 3 * configuration[pos + 5]) {
        return false;
      }
      for (unsigned i = 0; i < configuration[pos + 5]; ++i) {
        const uint8_t *entry = configuration + pos + 6 + 3 * i;
        if (entry[0] == 0x22) {
          if (interface.reportLength || !word(entry + 1) || word(entry + 1) > MouseReportCapacity) {
            return false;
          }
          interface.reportLength = word(entry + 1);
        }
      }
      if (!interface.reportLength) return false;
      interface.hidOffset = pos;
      interface.hidLength = length;
    } else if (type == 5) {
      if (current < 0 || length != 7 || configuration[pos + 3] != 3 || !configuration[pos + 6]) {
        return false;
      }
      uint8_t endpoint = configuration[pos + 2] & 0x7f;
      uint16_t size = word(configuration + pos + 4);
      if (!endpoint || endpoint > 7 || !size || size > 64) return false;
      MouseHidInterface &interface = result.interfaces[current];
      if (configuration[pos + 2] & 0x80) {
        if (interface.inEndpoint) return false;
        interface.inEndpoint = endpoint;
        interface.inPacketSize = size;
      } else {
        if (interface.outEndpoint) return false;
        interface.outEndpoint = endpoint;
        interface.outPacketSize = size;
      }
    }
    pos += length;
  }

  bool usedIn[8] = {}, usedOut[8] = {};
  if (result.interfaceCount != configuration[4]) return false;
  for (unsigned i = 0; i < result.interfaceCount; ++i) {
    MouseHidInterface &interface = result.interfaces[i];
    if (!interface.hidOffset || !interface.inEndpoint ||
        configuration[interface.offset + 4] != 1 + (interface.outEndpoint != 0)) {
      return false;
    }
    if (usedIn[interface.inEndpoint]) return false;
    usedIn[interface.inEndpoint] = true;
    if (interface.outEndpoint) {
      if (usedOut[interface.outEndpoint]) return false;
      usedOut[interface.outEndpoint] = true;
    }
  }

  memcpy(result.device, device, 18);
  memcpy(result.configuration, configuration, configurationLength);
  result.configurationLength = configurationLength;
  return true;
}
