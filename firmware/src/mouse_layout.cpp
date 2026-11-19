#include "mouse_layout.h"
#include <string.h>

namespace {
struct Usages {
  uint16_t list[8] = {};
  unsigned count = 0;
  uint16_t first = 0, last = 0;

  uint16_t at(unsigned index) const {
    if (index < count) return list[index];
    if (!count && first && first + index <= last) return first + index;
    return 0;
  }
};

MouseField *axis(MouseLayout &layout, unsigned page, unsigned usage) {
  if (page == 1 && usage == 0x30) return &layout.x;
  if (page == 1 && usage == 0x31) return &layout.y;
  if (page == 1 && usage == 0x38) return &layout.wheel;
  if (page == 12 && usage == 0x238) return &layout.pan;
  return nullptr;
}

void put(uint8_t *packet, MouseField field, unsigned value) {
  for (unsigned i = 0; i < field.width; ++i) {
    unsigned bit = field.offset + i;
    packet[bit / 8] |= ((value >> i) & 1u) << (bit % 8);
  }
}
}

bool readMouseLayout(const uint8_t *data, size_t length, MouseLayout &result) {
  if (!data || length > MaxMouseDescriptor) return false;
  MouseLayout layout{};
  Usages usages{};
  unsigned page = 0, width = 0, count = 0, bits = 0, depth = 0;
  int32_t minimum = 0, maximum = 0;
  bool seenMouse = false;

  for (size_t pos = 0; pos < length;) {
    uint8_t prefix = data[pos++];
    unsigned size = prefix & 3;
    if (size == 3) size = 4;
    if (size > length - pos) return false;
    uint32_t value = 0;
    for (unsigned i = 0; i < size; ++i) value |= uint32_t(data[pos++]) << (8 * i);
    int32_t signedValue = size == 1 ? int8_t(value) : size == 2 ? int16_t(value) : int32_t(value);

    switch (prefix & 0xfc) {
      case 0x04: // Usage Page
        if ((usages.count || usages.first) && page != value) return false;
        page = value;
        break;
      case 0x14: minimum = signedValue; break;
      case 0x24: maximum = minimum < 0 ? signedValue : int32_t(value); break;
      case 0x34: case 0x44: case 0x54: case 0x64: break; // Physical scale and unit
      case 0x74: width = value; break;
      case 0x94: count = value; break;
      case 0x84: // A single optional report ID, preceding all input fields
        if (!value || value > 255 || bits || layout.reportId) return false;
        layout.reportId = value;
        bits = 8;
        break;
      case 0x08:
        if (!value || value > 0xffff || usages.first || usages.count == 8) return false;
        usages.list[usages.count++] = value;
        break;
      case 0x18:
        if (!value || value > 0xffff || usages.count || usages.first) return false;
        usages.first = value;
        break;
      case 0x28:
        if (!usages.first || value < usages.first || value > 0xffff) return false;
        usages.last = value;
        break;
      case 0xa0: // Collection
        if (size != 1 || usages.count != 1) return false;
        if (!depth) {
          if (seenMouse || value != 1 || page != 1 || usages.at(0) != 2) return false;
          seenMouse = true;
        } else if (depth != 1 || value != 0 || page != 1 || usages.at(0) != 1) {
          return false;
        }
        ++depth;
        usages = {};
        break;
      case 0xc0:
        if (!depth || size) return false;
        --depth;
        usages = {};
        break;
      case 0x80: // Input
        if (!depth || !width || !count || count > (MousePacketSize * 8 - bits) / width) return false;
        for (unsigned i = 0; i < count; ++i, bits += width) {
          if (value == 1 || value == 3) continue; // Constant padding
          unsigned usage = usages.at(i);
          if (page == 9 && usage >= 1 && usage <= 8) {
            MouseField *field = &layout.buttons[usage - 1];
            if (value != 2 || width != 1 || minimum != 0 || maximum != 1 || field->width) {
              return false;
            }
            *field = {uint16_t(bits), uint8_t(width)};
          } else if (MouseField *field = axis(layout, page, usage)) {
            if (value != 6 || width < 8 || width > 16 || minimum > -127 ||
                maximum < 127 || minimum < -(1 << (width - 1)) ||
                maximum >= (1 << (width - 1)) || field->width) {
              return false;
            }
            *field = {uint16_t(bits), uint8_t(width)};
          }
        }
        usages = {};
        break;
      case 0xb0: // Feature reports do not occupy bits in the input packet
        usages = {};
        break;
      default: return false; // Includes Output, Push/Pop and long items
    }
  }
  if (depth || !layout.x.width || !layout.y.width) return false;
  layout.bytes = (bits + 7) / 8;
  result = layout;
  return true;
}

void encodeMouse(const MouseLayout &layout, uint8_t buttons, int x, int y,
                 int wheel, int pan, uint8_t (&packet)[MousePacketSize]) {
  memset(packet, 0, sizeof packet);
  if (layout.reportId) packet[0] = layout.reportId;
  for (unsigned i = 0; i < 8; ++i) put(packet, layout.buttons[i], buttons >> i);
  put(packet, layout.x, x);
  put(packet, layout.y, y);
  put(packet, layout.wheel, wheel);
  put(packet, layout.pan, pan);
}
