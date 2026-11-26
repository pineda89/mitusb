#include "device_kind.h"

bool firstUsage(const uint8_t *report, size_t length, uint16_t &page, uint16_t &usage) {
  uint32_t currentPage = 0, currentUsage = 0;
  for (size_t pos = 0; report && pos < length;) {
    uint8_t prefix = report[pos++];
    unsigned size = prefix & 3;
    if (size == 3) size = 4;
    if (prefix == 0xfe || size > length - pos) return false;  // long item or cut short
    uint32_t value = 0;
    for (unsigned i = 0; i < size; ++i) value |= uint32_t(report[pos++]) << (8 * i);
    switch (prefix & 0xfc) {
      case 0x04: currentPage = value; break;
      case 0x08: currentUsage = value; break;
      case 0xa0:
        page = currentPage;
        usage = currentUsage;
        return true;
    }
  }
  return false;
}

const DeviceKind *kindOf(const UsbProfile &profile, unsigned interface) {
  const DeviceKind *const order[] = {&gamepadKind, &keyboardKind, &mouseKind};
  for (const DeviceKind *kind : order) {
    if (interface < profile.interfaceCount && kind->match(profile, interface)) return kind;
  }
  return nullptr;
}

const char *kindName(const UsbProfile &profile) {
  const DeviceKind *lowest = nullptr;
  unsigned number = 256;
  for (unsigned i = 0; i < profile.interfaceCount; ++i) {
    const DeviceKind *kind = kindOf(profile, i);
    if (kind && profile.interfaces[i].number < number) {
      lowest = kind;
      number = profile.interfaces[i].number;
    }
  }
  return lowest ? lowest->name : "other";
}

void attachKinds(const UsbProfile &profile, const DeviceKind *kinds[MaxUsbInterfaces]) {
  for (unsigned i = 0; i < MaxUsbInterfaces; ++i) {
    const DeviceKind *kind = kindOf(profile, i);
    // One filter has one state: a second interface of the same kind is only proxied.
    for (unsigned j = 0; kind && kind->input && j < i; ++j) {
      if (kinds[j] == kind) kind = nullptr;
    }
    kinds[i] = kind;
    if (kind && kind->attach) kind->attach(profile, i);
  }
}
