#include "usb_capture.h"
#include "usb_control.h"

extern "C" volatile uint32_t usbhost_data_token;  // scripts/usbhost_control_length.py
#include <util/atomic.h>

namespace {
const uint32_t SkipCapture = 0x6d697455;
// RAM2 is not initialised at startup, so this survives the restart that asks for it.
DMAMEM uint32_t skipCaptureFlag;

// Not DMAMEM: the host controller transfers straight into these and this memory is not cached.
uint8_t controlBuffer[UsbDescriptorCapacity] __attribute__((aligned(32)));
uint8_t configuration[UsbConfigurationCapacity];

struct Endpoint {
  Pipe_t *pipe;
  uint8_t number, interface;
  bool in;
  uint16_t size;
  uint8_t busy;  // OUT: buffers still being sent
  uint8_t buffer[2][64] __attribute__((aligned(32)));
};

class RawDevice : public USBDriver {
public:
  explicit RawDevice(USBHost &) {
    contribute_Pipes(pipes, sizeof pipes / sizeof pipes[0]);
    contribute_Transfers(transfers, sizeof transfers / sizeof transfers[0]);
    driver_ready_for_device(this);
  }

  volatile bool enabled = true;   // claim the next device
  volatile bool claimed = false;  // a device is ours
  volatile bool forwarding = false;
  uint16_t configurationLength = 0;
  const DeviceKind **kinds = nullptr;

  bool read(uint8_t type, uint16_t value, uint16_t index, uint16_t length);
  bool open(const UsbProfile &profile);
  void send(uint8_t number, const uint8_t *packet, uint8_t length);
  bool startControl(uint64_t request, uint8_t *data);
  volatile int forwarded = -1;  // -2 in flight, -1 failed, else bytes transferred

protected:
  bool claim(Device_t *dev, int type, const uint8_t *descriptors, uint32_t len) override;
  void control(const Transfer_t *transfer) override;
  void disconnect() override;

private:
  static void received(const Transfer_t *transfer);
  static void sent(const Transfer_t *transfer);
  static void controlError(const Transfer_t *transfer);
  Endpoint *find(const Pipe_t *pipe);

  Pipe_t pipes[MaxUsbEndpoints] __attribute__((aligned(32)));
  Transfer_t transfers[64] __attribute__((aligned(32)));
  Endpoint endpoints[MaxUsbEndpoints];
  uint8_t endpointCount = 0;
  setup_t setup, forwardedSetup;
  uint8_t *forwardedData = nullptr;
  volatile bool controlComplete = false;
};

USBHost usbHost;
// First in the driver list: it is offered each device before the hub and the HID parsers.
RawDevice raw(usbHost);
USBHub hub(usbHost);
USBHIDParser hid1(usbHost), hid2(usbHost), hid3(usbHost);
}

WiredMouse mouse(usbHost);

namespace {
bool RawDevice::claim(Device_t *dev, int type, const uint8_t *descriptors, uint32_t len) {
  if (!enabled || claimed || type != 0 || dev->bDeviceClass == 9) return false;
  // The descriptors follow the 9-byte configuration header in the enumeration buffer.
  const uint8_t *whole = descriptors - 9;
  if (len + 9 > sizeof configuration || !validUsbConfiguration(whole, len + 9)) return false;
  memcpy(configuration, whole, len + 9);
  configurationLength = len + 9;
  claimed = true;
  return true;
}

void RawDevice::disconnect() {
  claimed = forwarding = false;
  if (forwarded == -2) forwarded = -1;
  endpointCount = 0;
}

void RawDevice::control(const Transfer_t *transfer) {
  if (transfer->buffer == forwardedData && forwarded == -2) {
    uint32_t remaining = (usbhost_data_token >> 16) & 0x7fff;
    forwarded = transfer->length > remaining ? transfer->length - remaining : 0;
  }
  if (transfer->buffer == controlBuffer) controlComplete = true;
}

bool RawDevice::read(uint8_t type, uint16_t value, uint16_t index, uint16_t length) {
  controlComplete = false;
  // Control completions report wLength, so clear any tail left by a short reply.
  memset(controlBuffer, 0, sizeof controlBuffer);
  bool queued = false;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    if (claimed) {
      mk_setup(setup, type, 6, value, index, length);
      queued = queue_Control_Transfer(device, &setup, controlBuffer, this);
    }
  }
  if (!queued) return false;
  uint32_t start = millis();
  while (!controlComplete && claimed && millis() - start < 250) usbHost.Task();
  return controlComplete;
}

bool RawDevice::open(const UsbProfile &profile) {
  bool opened = false;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    if (!claimed) break;
    endpointCount = 0;
    opened = true;
    for (unsigned i = 0; i < profile.endpointCount; ++i) {
      const UsbEndpoint &source = profile.endpoints[i];
      Endpoint &endpoint = endpoints[endpointCount];
      endpoint.pipe = new_Pipe(device, 3, source.number, source.in, source.packetSize, source.interval);
      if (!endpoint.pipe) {
        opened = false;
        break;
      }
      endpoint.pipe->callback_function = source.in ? received : sent;
      endpoint.number = source.number;
      endpoint.interface = source.interface;
      endpoint.in = source.in;
      endpoint.size = source.packetSize;
      endpoint.busy = 0;
      ++endpointCount;
    }
    for (unsigned i = 0; opened && i < endpointCount; ++i) {
      Endpoint &endpoint = endpoints[i];
      if (endpoint.in) opened = queue_Data_Transfer(endpoint.pipe, endpoint.buffer[0], endpoint.size, this);
    }
    forwarding = opened;
  }
  return opened;
}

void RawDevice::controlError(const Transfer_t *) {
  if (raw.forwarded == -2) raw.forwarded = -1;
}

bool RawDevice::startControl(uint64_t request, uint8_t *data) {
  if (!forwarding || forwarded == -2) return false;
  // A stall from the device reaches the pipe's error callback, never control().
  device->control_pipe->error_callback_function = controlError;
  forwardedSetup.word1 = request;
  forwardedSetup.word2 = request >> 32;
  if (!queue_Control_Transfer(device, &forwardedSetup, data, this)) return false;
  forwardedData = data;
  forwarded = -2;
  return true;
}

Endpoint *RawDevice::find(const Pipe_t *pipe) {
  for (unsigned i = 0; i < endpointCount; ++i) {
    if (endpoints[i].pipe == pipe) return &endpoints[i];
  }
  return nullptr;
}

// Runs in the host interrupt for each packet from the device.
void RawDevice::received(const Transfer_t *transfer) {
  Endpoint *endpoint = raw.find(transfer->pipe);
  if (!endpoint) return;
  uint8_t *packet = endpoint->buffer[0];
  uint32_t length = completedPacketLength(transfer->length, transfer->qtd.token);
  if (length && length <= sizeof endpoint->buffer[0] && raw.forwarding) {
    const DeviceKind *kind = raw.kinds ? raw.kinds[endpoint->interface] : nullptr;
    if (!kind || !kind->input || kind->input(packet, length)) {
      bool delivered = sendUsbPacket(endpoint->number, packet, length, false);
      if (kind && kind->sent) kind->sent(delivered);
    }
  }
  raw.queue_Data_Transfer(endpoint->pipe, packet, endpoint->size, &raw);
}

void RawDevice::sent(const Transfer_t *transfer) {
  Endpoint *endpoint = raw.find(transfer->pipe);
  if (!endpoint) return;
  for (unsigned slot = 0; slot < 2; ++slot) {
    if (transfer->buffer == endpoint->buffer[slot]) endpoint->busy &= ~(1 << slot);
  }
}

void RawDevice::send(uint8_t number, const uint8_t *packet, uint8_t length) {
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    for (unsigned i = 0; forwarding && i < endpointCount; ++i) {
      Endpoint &endpoint = endpoints[i];
      if (endpoint.in || endpoint.number != number || length > endpoint.size) continue;
      unsigned slot = endpoint.busy & 1 ? 1 : 0;
      if (endpoint.busy & (1 << slot)) break;  // both in flight: the device is not keeping up
      memcpy(endpoint.buffer[slot], packet, length);
      if (queue_Data_Transfer(endpoint.pipe, endpoint.buffer[slot], length, this)) {
        endpoint.busy |= 1 << slot;
      }
      break;
    }
  }
}

bool captureString(UsbProfile &result, uint8_t index, uint16_t language) {
  if (!raw.read(0x80, 0x0300 | index, language, UsbStringCapacity) || controlBuffer[1] != 3) return false;
  uint8_t length = controlBuffer[0];
  if (length < 2 || (length & 1) || (index == 0 && length < 4) || result.stringCount >= MaxUsbStrings) {
    return false;
  }
  UsbString &string = result.strings[result.stringCount++];
  string.index = index;
  string.language = language;
  string.length = length;
  memcpy(string.data, controlBuffer, length);
  return true;
}

bool captureStrings(UsbProfile &result) {
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
  if (!captureString(result, 0, 0)) return false;
  uint16_t language = result.strings[0].data[2] | uint16_t(result.strings[0].data[3]) << 8;
  // Advertise only the language for which we serve strings.
  result.strings[0].length = result.strings[0].data[0] = 4;
  for (unsigned i = 0; i < count; ++i) {
    if (!indices[i]) continue;
    bool seen = false;
    for (unsigned j = 0; j < i; ++j) seen |= indices[j] == indices[i];
    if (!seen && !captureString(result, indices[i], language)) return false;
  }
  return true;
}

bool capture(UsbProfile &profile, const DeviceKind *kinds[MaxUsbInterfaces]) {
  uint8_t device[18];
  if (!raw.read(0x80, 0x0100, 0, sizeof device)) return false;
  memcpy(device, controlBuffer, sizeof device);
  if (!readUsbProfile(device, sizeof device, configuration, raw.configurationLength, profile) ||
      !captureStrings(profile)) return false;
  for (unsigned i = 0; i < profile.interfaceCount; ++i) {
    UsbInterface &interface = profile.interfaces[i];
    if (!interface.hidOffset) continue;
    if (!raw.read(0x81, 0x2200, interface.number, interface.reportLength)) return false;
    memcpy(interface.report, controlBuffer, interface.reportLength);
  }
  attachKinds(profile, kinds);
  raw.kinds = kinds;
  return raw.open(profile);
}
}

extern "C" void forwardUsbOutput(uint8_t endpoint, const uint8_t *packet, uint8_t length) {
  raw.send(endpoint, packet, length);
}

bool hostControl(uint64_t setup, uint8_t *data) {
  bool started = false;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) started = raw.startControl(setup, data);
  return started;
}

int hostControlResult() {
  return raw.forwarded;
}

void beginUsbHost() {
  if (skipCaptureFlag == SkipCapture) raw.enabled = false;
  skipCaptureFlag = 0;
  usbHost.begin();
}

void taskUsbHost() {
  usbHost.Task();
}

bool deviceAttached() {
  return raw.forwarding;
}

bool probeDevice(UsbProfile &profile, const DeviceKind *kinds[MaxUsbInterfaces]) {
  for (unsigned i = 0; i < MaxUsbInterfaces; ++i) kinds[i] = nullptr;
  uint32_t start = millis();
  while (raw.enabled && !raw.claimed && millis() - start < 2500) usbHost.Task();
  bool wasEnabled = raw.enabled;
  raw.enabled = false;  // whatever is plugged in from now on goes to the HID parser
  if (!wasEnabled || !raw.claimed) return false;
  if (capture(profile, kinds)) return true;
  // The device is claimed and cannot be given back: start again without capturing.
  skipCaptureFlag = SkipCapture;
  arm_dcache_flush(&skipCaptureFlag, sizeof skipCaptureFlag);
  SCB_AIRCR = 0x05FA0004;
  for (;;) {}
}
