// The USB identity of the device on J6, as the computer will see it.
#pragma once

#include <stddef.h>
#include <stdint.h>

enum {
  UsbConfigurationCapacity = 512,
  UsbReportCapacity = 512,
  UsbStringCapacity = 255,
  MaxUsbStrings = 16,
  MaxUsbInterfaces = 8,
  MaxUsbEndpoints = 14  // numbers 1 to 7, once per direction
};

typedef struct UsbString {
  uint16_t language;
  uint8_t index;
  uint8_t length;
  uint8_t data[UsbStringCapacity];
} UsbString;

typedef struct UsbInterface {
  uint16_t offset;  // of the interface descriptor in the configuration
  uint16_t hidOffset, reportLength;  // 0 unless the interface is HID (class 3)
  uint8_t number, hidLength;
  uint8_t interfaceClass, subClass, protocol;
  uint8_t report[UsbReportCapacity];
} UsbInterface;

typedef struct UsbEndpoint {
  uint8_t number;     // 1 to 7
  uint8_t in;         // towards the computer
  uint8_t interface;  // index into UsbProfile.interfaces
  uint8_t interval;
  uint16_t packetSize;
} UsbEndpoint;

typedef struct UsbProfile {
  uint8_t device[18];
  uint8_t configuration[UsbConfigurationCapacity];
  uint16_t configurationLength;
  UsbInterface interfaces[MaxUsbInterfaces];
  uint8_t interfaceCount;
  UsbEndpoint endpoints[MaxUsbEndpoints];
  uint8_t endpointCount;
  UsbString strings[MaxUsbStrings];
  uint8_t stringCount;
} UsbProfile;

#ifdef __cplusplus
// Accepts one configuration of up to 8 interfaces without alternate settings whose
// endpoints are all interrupt, numbered 1 to 7 and at most 64 bytes. Report descriptors
// and strings are left for the caller to fill.
bool readUsbProfile(const uint8_t *device, size_t deviceLength,
                    const uint8_t *configuration, size_t configurationLength,
                    UsbProfile &result);
// The same checks on the configuration alone, without the memory of a whole profile.
bool validUsbConfiguration(const uint8_t *configuration, size_t length);
#endif
