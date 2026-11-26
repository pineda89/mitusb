#include "mouse_capture.h"
#include <util/atomic.h>

namespace {
class CapturingHIDParser : public USBHIDParser {
public:
  using USBHIDParser::USBHIDParser;
  bool hasReportDescriptor() const { return reportReady; }
  bool belongsTo(const CapturingHIDParser &source) const { return device && device == source.device; }
  volatile bool selected = false;

protected:
  void control(const Transfer_t *transfer) override;
  void disconnect() override {
    selected = false;
    reportReady = false;
    USBHIDParser::disconnect();
  }

private:
  volatile bool reportReady = false;
};

USBHost usbHost;
USBHub hub(usbHost);
CapturingHIDParser hid1(usbHost), hid2(usbHost), hid3(usbHost);
CapturingHIDParser *const parsers[] = {&hid1, &hid2, &hid3};
}

// The mouse must claim its collection before the catch-all raw input driver.
WiredMouse mouse(usbHost);

volatile bool mouseFrozen = false;
volatile uint8_t hiddenMouseButtons = 0;
InjectedMouse injected;

namespace {
const MouseLayout *mouseLayout = nullptr;
const UsbProfile *captured = nullptr;

// The endpoint of an interface (by its number) in one direction, or 0.
uint8_t endpointOf(uint8_t interfaceNumber, bool in) {
  for (unsigned i = 0; captured && i < captured->endpointCount; ++i) {
    const UsbEndpoint &e = captured->endpoints[i];
    if (bool(e.in) == in && captured->interfaces[e.interface].number == interfaceNumber) return e.number;
  }
  return 0;
}
volatile bool controlComplete;
DMAMEM uint8_t controlBuffer[UsbDescriptorCapacity] __attribute__((aligned(32)));

void forwardInput(const Transfer_t *transfer) {
  auto *parser = static_cast<CapturingHIDParser *>(transfer->driver);
  if (!parser->selected || mouseFrozen) return;
  uint32_t length = completedPacketLength(transfer->length, transfer->qtd.token);
  if (length && length <= MousePacketSize) {
    const uint8_t *packet = static_cast<const uint8_t *>(transfer->buffer);
    uint8_t visible[MousePacketSize];
    int takenX = 0, takenY = 0;
    bool merged = false;
    if (parser == mouse.source && mouseLayout) {
      memcpy(visible, packet, length);
      hideMouseButtons(*mouseLayout, hiddenMouseButtons, visible, length);
      merged = mergeMouse(*mouseLayout, injected.buttons, injected.x, injected.y,
                          visible, length, takenX, takenY);
      packet = visible;
    }
    if (sendUsbPacket(endpointOf(parser->interfaceNumber(), true), packet, length, false) && merged) {
      injected.x -= takenX;
      injected.y -= takenY;
      injected.sentButtons = injected.buttons;
      injected.merged = true;
    }
  }
}

class RawHIDInput : public USBHIDInput {
public:
  RawHIDInput() { USBHIDParser::driver_ready_for_hid_collection(this); }

protected:
  hidclaim_t claim_collection(USBHIDParser *, Device_t *, uint32_t) override {
    return CLAIM_REPORT;
  }

  bool hid_process_in_data(const Transfer_t *transfer) override {
    forwardInput(transfer);
    return transfer->driver != mouse.source;
  }
};

RawHIDInput rawInput;

bool readDescriptor(CapturingHIDParser *source, uint16_t value,
                    uint16_t index, uint16_t requestedLength) {
  controlComplete = false;
  // Control completions report wLength, so clear any tail left by a short reply.
  memset(controlBuffer, 0, sizeof controlBuffer);
  arm_dcache_flush_delete(controlBuffer, sizeof controlBuffer);
  noInterrupts();
  bool queued = mouse.source == source &&
                source->sendControlPacket(0x80, 6, value, index, requestedLength, controlBuffer);
  interrupts();
  if (!queued) return false;
  uint32_t start = millis();
  while (!controlComplete && millis() - start < 250) usbHost.Task();
  if (controlComplete) arm_dcache_delete(controlBuffer, sizeof controlBuffer);
  return controlComplete;
}

bool captureString(CapturingHIDParser *source, UsbProfile &result,
                   uint8_t index, uint16_t language) {
  if (!readDescriptor(source, 0x0300 | index, language, UsbStringCapacity) ||
      controlBuffer[1] != 3) return false;
  uint8_t length = controlBuffer[0];
  if (length < 2 || (length & 1) || (index == 0 && length < 4) ||
      result.stringCount >= MaxUsbStrings) {
    return false;
  }
  UsbString &string = result.strings[result.stringCount++];
  string.index = index;
  string.language = language;
  string.length = length;
  memcpy(string.data, controlBuffer, length);
  return true;
}

bool captureStrings(CapturingHIDParser *source, UsbProfile &result) {
  uint8_t indices[4 + MaxUsbInterfaces];
  unsigned count = 0;
  indices[count++] = result.device[14];
  indices[count++] = result.device[15];
  indices[count++] = result.device[16];
  indices[count++] = result.configuration[6];
  for (unsigned i = 0; i < result.interfaceCount; ++i) {
    indices[count++] = result.configuration[result.interfaces[i].offset + 8];
  }
  bool any = false;
  for (unsigned i = 0; i < count; ++i) any |= indices[i] != 0;
  if (!any) return true;
  if (!captureString(source, result, 0, 0)) return false;
  uint16_t language = result.strings[0].data[2] | uint16_t(result.strings[0].data[3]) << 8;
  // Advertise only the language for which we serve strings.
  result.strings[0].length = result.strings[0].data[0] = 4;
  for (unsigned i = 0; i < count; ++i) {
    if (!indices[i]) continue;
    bool seen = false;
    for (unsigned j = 0; j < i; ++j) seen |= indices[j] == indices[i];
    if (!seen && !captureString(source, result, indices[i], language)) return false;
  }
  return true;
}

CapturingHIDParser *readyParser(const CapturingHIDParser &source, const UsbInterface &interface) {
  for (auto *parser : parsers) {
    if (parser->hasReportDescriptor() && parser->belongsTo(source) &&
        parser->interfaceNumber() == interface.number &&
        parser->getHIDReportDescriptorSize() == interface.reportLength) {
      return parser;
    }
  }
  return nullptr;
}

bool copyReports(const CapturingHIDParser &source, UsbProfile &result) {
  CapturingHIDParser *interfaces[MaxUsbInterfaces];
  noInterrupts();
  for (unsigned i = 0; i < result.interfaceCount; ++i) {
    interfaces[i] = readyParser(source, result.interfaces[i]);
    if (!interfaces[i]) {
      interrupts();
      return false;
    }
  }
  for (unsigned i = 0; i < result.interfaceCount; ++i) {
    UsbInterface &interface = result.interfaces[i];
    memcpy(interface.report, interfaces[i]->getHIDReportDescriptor(), interface.reportLength);
    interfaces[i]->selected = true;
  }
  interrupts();
  return true;
}
}

// This capture has one HID parser per interface and three of them.
static bool allHid(const UsbProfile &profile) {
  if (profile.interfaceCount > 3) return false;
  for (unsigned i = 0; i < profile.interfaceCount; ++i) {
    if (!profile.interfaces[i].hidOffset) return false;
  }
  return true;
}

bool WiredMouse::hid_process_in_data(const Transfer_t *transfer) {
  forwardInput(transfer);
  return false;
}

bool WiredMouse::isCaptured() const {
  const auto *parser = static_cast<const CapturingHIDParser *>(source);
  return parser && parser->selected;
}

void CapturingHIDParser::control(const Transfer_t *transfer) {
  if (transfer->setup.word1 == 0x22000681 && transfer->length == getHIDReportDescriptorSize()) {
    reportReady = true;
  }
  if (transfer->buffer == controlBuffer) {
    controlComplete = true;
  }
  USBHIDParser::control(transfer);
}

extern "C" void forwardUsbOutput(uint8_t endpoint, const uint8_t *packet, uint8_t length) {
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    for (auto *parser : parsers) {
      if (parser->selected && endpointOf(parser->interfaceNumber(), false) == endpoint && length <= parser->outSize()) {
        parser->sendPacket(packet, length);
        return;
      }
    }
  }
}

void beginMouseHost() {
  usbHost.begin();
}

void taskMouseHost() {
  usbHost.Task();
}

bool probeMouse(UsbProfile &profile, MouseLayout &layout, uint8_t &mouseEndpoint) {
  layout = MouseLayout{};
  mouseEndpoint = 0;
  uint32_t start = millis();
  while (!mouse.source && millis() - start < 2500) usbHost.Task();
  auto *source = (CapturingHIDParser *)mouse.source;
  uint8_t device[18];
  if (!source || !readDescriptor(source, 0x0100, 0, sizeof device)) return false;
  uint8_t mouseInterface = source->interfaceNumber();
  memcpy(device, controlBuffer, sizeof device);
  if (!readDescriptor(source, 0x0200, 0, 9)) return false;
  uint16_t total = controlBuffer[2] | uint16_t(controlBuffer[3]) << 8;
  if (total < 9 || total > UsbConfigurationCapacity ||
      !readDescriptor(source, 0x0200, 0, total) ||
      !readUsbProfile(device, sizeof device, controlBuffer, total, profile) || !allHid(profile) ||
      !captureStrings(source, profile)) return false;

  start = millis();
  while (!copyReports(*source, profile)) {
    if (mouse.source != source || millis() - start >= 500) return false;
    usbHost.Task();
  }
  captured = &profile;
  mouseEndpoint = endpointOf(mouseInterface, true);
  for (unsigned i = 0; i < profile.interfaceCount; ++i) {
    const UsbInterface &interface = profile.interfaces[i];
    if (interface.number == mouseInterface) {
      if (readMouseLayout(interface.report, interface.reportLength, layout)) mouseLayout = &layout;
      break;
    }
  }
  return true;
}
