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
void encodeMouse(const MouseLayout &layout, uint8_t buttons, int x, int y,
                 int wheel, int pan, uint8_t (&packet)[MousePacketSize]);
