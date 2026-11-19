#pragma once

#include <stddef.h>
#include <stdint.h>

enum {
  MouseConfigurationCapacity = 512,
  MouseReportCapacity = 512,
  MouseStringCapacity = 255,
  MaxMouseStrings = 10,
  MaxMouseInterfaces = 3
};

typedef struct MouseString {
  uint16_t language;
  uint8_t index;
  uint8_t length;
  uint8_t data[MouseStringCapacity];
} MouseString;

typedef struct MouseHidInterface {
  uint16_t offset;
  uint16_t hidOffset;
  uint16_t reportLength;
  uint16_t inPacketSize;
  uint16_t outPacketSize;
  uint8_t number;
  uint8_t hidLength;
  uint8_t inEndpoint;
  uint8_t outEndpoint;
  uint8_t report[MouseReportCapacity];
} MouseHidInterface;

typedef struct MouseProfile {
  uint8_t device[18];
  uint8_t configuration[MouseConfigurationCapacity];
  uint16_t configurationLength;
  MouseHidInterface interfaces[MaxMouseInterfaces];
  uint8_t interfaceCount;
  MouseString strings[MaxMouseStrings];
  uint8_t stringCount;
} MouseProfile;

#ifdef __cplusplus
bool readMouseProfile(const uint8_t *device, size_t deviceLength,
                      const uint8_t *configuration, size_t configurationLength,
                      MouseProfile &result);
#endif
