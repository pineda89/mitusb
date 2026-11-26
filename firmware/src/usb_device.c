#include <stdbool.h>
#include <string.h>
#define USB_DESC_LIST_DEFINE
#include "usb_desc.h"
#include "usb_dev.h"
#include "usb_mouse.h"
#include "core_pins.h"
#include "avr/pgmspace.h"
#include "util/atomic.h"
#include "usb_device.h"

extern const usb_descriptor_list_t __real_usb_descriptor_list[];
enum { ConfigSize = 9 + 9 + 9 + 7, HidOffset = 18, MaxDescriptorEntries = 48 };
usb_descriptor_list_t __wrap_usb_descriptor_list[MaxDescriptorEntries];
uint8_t __wrap_usb_config_descriptor_12[UsbDescriptorCapacity];
uint8_t __wrap_usb_config_descriptor_480[UsbDescriptorCapacity];
DMAMEM uint8_t __wrap_usb_descriptor_buffer[UsbDescriptorCapacity] __attribute__((aligned(32)));

static uint8_t qualifier[10];
static const UsbProfile *activeProfile;
// Packet size of each endpoint number in the active profile, 0 where there is none.
static uint16_t inSize[NUM_ENDPOINTS + 1], outSize[NUM_ENDPOINTS + 1];

typedef struct {
  uint32_t config, current, next, status;
  uint32_t pointer0, pointer1, pointer2, pointer3, pointer4, reserved;
  uint32_t setup0, setup1;
  transfer_t *first_transfer, *last_transfer;
  void (*callback_function)(transfer_t *);
  uint32_t unused1;
} device_endpoint_t;
extern device_endpoint_t endpoint_queue_head[];

void __real_usb_init(void);
void __wrap_usb_init(void) {}

static bool validProfile(const UsbProfile *profile) {
  if (profile->configurationLength > UsbDescriptorCapacity || profile->stringCount > MaxUsbStrings ||
      !profile->interfaceCount || profile->interfaceCount > MaxUsbInterfaces ||
      !profile->endpointCount || profile->endpointCount > MaxUsbEndpoints) {
    return false;
  }
  for (unsigned i = 0; i < profile->interfaceCount; ++i) {
    const UsbInterface *interface = &profile->interfaces[i];
    if (interface->hidOffset && (interface->hidOffset + interface->hidLength > profile->configurationLength ||
        !interface->reportLength || interface->reportLength > UsbReportCapacity)) {
      return false;
    }
  }
  bool usedIn[NUM_ENDPOINTS + 1] = {0}, usedOut[NUM_ENDPOINTS + 1] = {0};
  for (unsigned i = 0; i < profile->endpointCount; ++i) {
    const UsbEndpoint *endpoint = &profile->endpoints[i];
    bool *used = endpoint->in ? usedIn : usedOut;
    if (endpoint->number < 1 || endpoint->number > NUM_ENDPOINTS || used[endpoint->number] ||
        !endpoint->packetSize || endpoint->packetSize > 64) {
      return false;
    }
    used[endpoint->number] = true;
  }
  return true;
}

static bool buildDescriptors(const UsbProfile *profile) {
  bool copied = profile != NULL;
  if (copied) {
    uint8_t q[10] = {10, 6, profile->device[2], profile->device[3],
                     profile->device[4], profile->device[5], profile->device[6],
                     profile->device[7], 1, 0};
    memcpy(qualifier, q, sizeof qualifier);
  }

  unsigned output = 0;
  for (unsigned i = 0; __real_usb_descriptor_list[i].addr; ++i) {
    usb_descriptor_list_t entry = __real_usb_descriptor_list[i];
    uint8_t *config = NULL;
    if (copied && ((entry.wValue >> 8) == 3 || entry.wValue == 0x2100 || entry.wValue == 0x2200)) continue;
    switch (entry.wValue) {
      case 0x0100:
        if (copied) entry.addr = profile->device;
        break;
      case 0x0600:
        if (copied) entry.addr = qualifier;
        break;
      case 0x0200: config = __wrap_usb_config_descriptor_480; break;
      case 0x0700: config = __wrap_usb_config_descriptor_12; break;
      case 0x2100: entry.addr = __wrap_usb_config_descriptor_480 + HidOffset; break;
    }
    if (config) {
      uint16_t configLength = copied ? profile->configurationLength : ConfigSize;
      if ((!copied && entry.length != configLength) || configLength > UsbDescriptorCapacity) {
        return false;
      }
      memcpy(config, copied ? profile->configuration : entry.addr, configLength);
      entry.addr = config;
      entry.length = configLength;
    }
    if (output + 1 >= MaxDescriptorEntries) return false;
    __wrap_usb_descriptor_list[output++] = entry;
  }
  if (copied) {
    for (unsigned i = 0; i < profile->stringCount; ++i) {
      const UsbString *string = &profile->strings[i];
      uint16_t value = 0x0300 | string->index;
      if (output + 1 >= MaxDescriptorEntries) return false;
      __wrap_usb_descriptor_list[output++] = (usb_descriptor_list_t){
        value, string->language, string->data, string->length
      };
    }
    for (unsigned i = 0; i < profile->interfaceCount; ++i) {
      const UsbInterface *interface = &profile->interfaces[i];
      if (!interface->hidOffset) continue;
      if (output + 2 >= MaxDescriptorEntries) return false;
      __wrap_usb_descriptor_list[output++] = (usb_descriptor_list_t){
        0x2100, interface->number, __wrap_usb_config_descriptor_480 + interface->hidOffset,
        interface->hidLength
      };
      __wrap_usb_descriptor_list[output++] = (usb_descriptor_list_t){
        0x2200, interface->number, interface->report, interface->reportLength
      };
    }
  }
  __wrap_usb_descriptor_list[output] = (usb_descriptor_list_t){0, 0, NULL, 0};
  return true;
}

bool startUsbDevice(const UsbProfile *profile) {
  if (!profile || !validProfile(profile) || !buildDescriptors(profile)) {
    if (!buildDescriptors(NULL)) return false;
    profile = NULL;
  }
  memset(inSize, 0, sizeof inSize);
  memset(outSize, 0, sizeof outSize);
  for (unsigned i = 0; profile && i < profile->endpointCount; ++i) {
    const UsbEndpoint *endpoint = &profile->endpoints[i];
    (endpoint->in ? inSize : outSize)[endpoint->number] = endpoint->packetSize;
  }
  activeProfile = profile;
  uint8_t activeControlPacketSize = profile ? profile->device[7] : EP0_SIZE;
  __real_usb_init();
  endpoint_queue_head[0].config = (endpoint_queue_head[0].config & 0xF800FFFF) |
                                  ((uint32_t)activeControlPacketSize << 16);
  endpoint_queue_head[1].config = (endpoint_queue_head[1].config & 0xF800FFFF) |
                                  ((uint32_t)activeControlPacketSize << 16);
  USB1_PORTSC1 |= USB_PORTSC1_PFSC;
  return profile != NULL;
}

void __real_usb_mouse_configure(void);
// Two transfers per endpoint number and direction; index 0 is unused.
static transfer_t txTransfers[NUM_ENDPOINTS + 1][2] __attribute__((aligned(32)));
DMAMEM static uint8_t txBuffers[NUM_ENDPOINTS + 1][2][64] __attribute__((aligned(32)));
static unsigned nextTx[NUM_ENDPOINTS + 1];
static unsigned nextRx1;
static transfer_t rxTransfers[NUM_ENDPOINTS + 1][2] __attribute__((aligned(32)));
DMAMEM static uint8_t rxBuffers[NUM_ENDPOINTS + 1][2][64] __attribute__((aligned(32)));

static void receiveCallback(transfer_t *transfer);

static void configureEndpoint1(bool transmit, uint16_t packetSize) {
  device_endpoint_t *endpoint = endpoint_queue_head + (transmit ? 3 : 2);
  memset(endpoint, 0, sizeof *endpoint);
  endpoint->config = ((uint32_t)packetSize << 16) | (1 << 29);
  endpoint->next = 1;
}

static void scheduleEndpoint1(bool transmit, transfer_t *transfer) {
  device_endpoint_t *endpoint = endpoint_queue_head + (transmit ? 3 : 2);
  uint32_t mask = transmit ? (1 << 17) : (1 << 1);
  uint32_t interruptState = __get_primask();
  __disable_irq();
  transfer_t *last = endpoint->last_transfer;
  if (last) {
    last->next = (uint32_t)transfer;
    if (USB1_ENDPTPRIME & mask) goto end;
    uint32_t status, cyccnt = ARM_DWT_CYCCNT;
    do {
      USB1_USBCMD |= USB_USBCMD_ATDTW;
      status = USB1_ENDPTSTATUS;
    } while (!(USB1_USBCMD & USB_USBCMD_ATDTW) && ARM_DWT_CYCCNT - cyccnt < 2400);
    if (status & mask) goto end;
    endpoint->next = (uint32_t)transfer;
    endpoint->status = 0;
    USB1_ENDPTPRIME |= mask;
    goto end;
  }
  endpoint->next = (uint32_t)transfer;
  endpoint->status = 0;
  USB1_ENDPTPRIME |= mask;
  endpoint->first_transfer = transfer;
end:
  endpoint->last_transfer = transfer;
  __set_primask(interruptState);
}

static void queueReceive(unsigned endpoint, unsigned slot) {
  transfer_t *transfer = &rxTransfers[endpoint][slot];
  uint8_t *buffer = rxBuffers[endpoint][slot];
  usb_prepare_transfer(transfer, buffer, outSize[endpoint], (endpoint << 1) | slot);
  arm_dcache_delete(buffer, outSize[endpoint]);
  if (endpoint == 1) scheduleEndpoint1(false, transfer);
  else usb_receive(endpoint, transfer);
}

static void completeReceive(unsigned endpoint, unsigned slot) {
  uint32_t length = completedPacketLength(outSize[endpoint], rxTransfers[endpoint][slot].status);
  if (length) forwardUsbOutput(endpoint, rxBuffers[endpoint][slot], length);
  queueReceive(endpoint, slot);
}

static void receiveCallback(transfer_t *transfer) {
  unsigned endpoint = transfer->callback_param >> 1;
  if (!activeProfile || endpoint > NUM_ENDPOINTS || !outSize[endpoint]) return;
  completeReceive(endpoint, transfer->callback_param & 1);
}

// The core only calls back for endpoints 2 to 7, so endpoint 1 is polled.
void pollUsbDevice(void) {
  if (!activeProfile || !usb_configuration || !outSize[1]) return;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    for (unsigned count = 0; count < 2; ++count) {
      if (usb_transfer_status(&rxTransfers[1][nextRx1]) & 0x80) break;
      completeReceive(1, nextRx1);
      nextRx1 ^= 1;
    }
  }
}

void __wrap_usb_mouse_configure(void) {
  __real_usb_mouse_configure();
  memset(txTransfers, 0, sizeof txTransfers);
  memset(nextTx, 0, sizeof nextTx);
  nextRx1 = 0;
  memset(rxTransfers, 0, sizeof rxTransfers);
  if (!activeProfile) return;

  for (uint8_t endpoint = 1; endpoint <= NUM_ENDPOINTS; ++endpoint) {
    if (!inSize[endpoint] && !outSize[endpoint]) continue;
    volatile uint32_t *control = (volatile uint32_t *)&USB1_ENDPTCTRL0 + endpoint;
    *control = (outSize[endpoint] ? ENDPOINT_RECEIVE_INTERRUPT : ENDPOINT_RECEIVE_UNUSED) |
               (inSize[endpoint] ? ENDPOINT_TRANSMIT_INTERRUPT : ENDPOINT_TRANSMIT_UNUSED);
    if (inSize[endpoint]) {
      if (endpoint == 1) configureEndpoint1(true, inSize[endpoint]);
      else usb_config_tx(endpoint, inSize[endpoint], 0, NULL);
    }
    if (outSize[endpoint]) {
      if (endpoint == 1) configureEndpoint1(false, outSize[endpoint]);
      else usb_config_rx(endpoint, outSize[endpoint], 0, receiveCallback);
      queueReceive(endpoint, 0);
      queueReceive(endpoint, 1);
    }
  }
}

bool sendUsbPacket(uint8_t endpoint, const uint8_t *packet, uint8_t length, bool wait) {
  if (!activeProfile || !usb_configuration || endpoint < 1 || endpoint > NUM_ENDPOINTS ||
      !length || length > inSize[endpoint]) return false;
  uint32_t start = millis();
  for (;;) {
    uint32_t interruptState = __get_primask();
    __disable_irq();
    unsigned slot = nextTx[endpoint];
    transfer_t *transfer = &txTransfers[endpoint][slot];
    if (!(usb_transfer_status(transfer) & 0x80)) {
      memcpy(txBuffers[endpoint][slot], packet, length);
      arm_dcache_flush_delete(txBuffers[endpoint][slot], sizeof txBuffers[endpoint][slot]);
      usb_prepare_transfer(transfer, txBuffers[endpoint][slot], length, 0);
      nextTx[endpoint] = slot ^ 1;
      if (endpoint == 1) scheduleEndpoint1(true, transfer);
      else usb_transmit(endpoint, transfer);
      __set_primask(interruptState);
      return true;
    }
    __set_primask(interruptState);
    if (!wait || !usb_configuration || millis() - start >= 30) return false;
    yield();
  }
}
