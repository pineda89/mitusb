// Text protocol spoken over UDP. Pure C++ so it can be unit-tested on the host.
//
//   hello              -> subscribe the sender to mouse events
//   profile            -> report whether USB descriptors were copied
//   move <dx> <dy>     -> inject a relative move
//   wheel <n>          -> inject wheel steps
//   click left|right|middle
#pragma once
#include <stdlib.h>
#include <string.h>

struct Command {
  enum Type { Hello, Move, Wheel, Click, Profile } type;
  int a, b;  // move: dx, dy | wheel: n | click: button mask (1 left, 2 right, 4 middle)
};

static const int MAX_DELTA = 32767;

static bool parseInt(const char* s, int& out) {
  if (!s) return false;
  char* end;
  long v = strtol(s, &end, 10);
  if (end == s || *end || v < -MAX_DELTA || v > MAX_DELTA) return false;
  out = (int)v;
  return true;
}

// Returns false (and leaves nothing half-done) for anything malformed.
static bool parse(const void* data, size_t length, Command& out) {
  char buf[64];
  if (!length || length >= sizeof buf || memchr(data, 0, length)) return false;
  memcpy(buf, data, length);
  buf[length] = 0;

  const char* tok[4] = {};
  int n = 0;
  for (char* t = strtok(buf, " \r\n"); t; t = strtok(nullptr, " \r\n")) {
    if (n == 4) return false;
    tok[n++] = t;
  }
  if (n == 0) return false;

  if (!strcmp(tok[0], "hello") && n == 1) {
    out = {Command::Hello, 0, 0};
    return true;
  }
  if (!strcmp(tok[0], "profile") && n == 1) {
    out = {Command::Profile, 0, 0};
    return true;
  }
  if (!strcmp(tok[0], "move") && n == 3) {
    int dx, dy;
    if (!parseInt(tok[1], dx) || !parseInt(tok[2], dy)) return false;
    out = {Command::Move, dx, dy};
    return true;
  }
  if (!strcmp(tok[0], "wheel") && n == 2) {
    int steps;
    if (!parseInt(tok[1], steps)) return false;
    out = {Command::Wheel, steps, 0};
    return true;
  }
  if (!strcmp(tok[0], "click") && n == 2) {
    int mask = !strcmp(tok[1], "left") ? 1 : !strcmp(tok[1], "right") ? 2 : !strcmp(tok[1], "middle") ? 4 : 0;
    if (!mask) return false;
    out = {Command::Click, mask, 0};
    return true;
  }
  return false;
}

// A USB mouse report carries at most +-127 per axis: big moves go in several reports.
static int nextStep(int remaining) {
  return remaining > 127 ? 127 : remaining < -127 ? -127 : remaining;
}
