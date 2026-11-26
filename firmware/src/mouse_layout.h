#pragma once

#include <stddef.h>
#include <stdint.h>

enum { MousePacketSize = 64, MaxMouseDescriptor = 512 };

struct MouseField {
  uint16_t offset = 0;
  uint8_t width = 0;
};

struct MouseLayout {
  MouseField buttons[8], x, y, wheel, pan;
  uint8_t reportId = 0;
  uint8_t bytes = 0;
};

bool readMouseLayout(const uint8_t *descriptor, size_t length, MouseLayout &layout);
// Clears the buttons in `mask` (bit 0 = button 1) from a raw report of this layout.
void hideMouseButtons(const MouseLayout &layout, uint8_t mask, uint8_t *packet, size_t length);
// Adds injected buttons and up to +-127 of (x, y) to a raw report of this layout, limited by
// the field widths. Returns false and leaves the packet alone if it is not such a report.
bool mergeMouse(const MouseLayout &layout, uint8_t buttons, int x, int y,
                uint8_t *packet, size_t length, int &takenX, int &takenY);
void encodeMouse(const MouseLayout &layout, uint8_t buttons, int x, int y,
                 int wheel, int pan, uint8_t (&packet)[MousePacketSize]);
