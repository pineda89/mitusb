#include <stdbool.h>
#include <string.h>
#define USB_DESC_LIST_DEFINE
#include "usb_desc.h"
#include "usb_dev.h"
#include "usb_mouse.h"
#include "core_pins.h"
#include "avr/pgmspace.h"
#include "util/atomic.h"
#include "mouse_device.h"

extern const usb_descriptor_list_t __real_usb_descriptor_list[];
enum { ConfigSize = 9 + 9 + 9 + 7, HidOffset = 18, MaxDescriptorEntries = 24 };
usb_descriptor_list_t __wrap_usb_descriptor_list[MaxDescriptorEntries];
uint8_t __wrap_usb_config_descriptor_12[MouseDescriptorCapacity];
uint8_t __wrap_usb_config_descriptor_480[MouseDescriptorCapacity];
DMAMEM uint8_t __wrap_usb_descriptor_buffer[MouseDescriptorCapacity] __attribute__((aligned(32)));

static uint8_t qualifier[10];
static const MouseProfile *activeProfile;
static uint8_t activeMouseInterface;

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

static bool validProfile(const MouseProfile *profile) {
  if (profile->configurationLength > MouseDescriptorCapacity || profile->stringCount > MaxMouseStrings ||
      !profile->interfaceCount || profile->interfaceCount > MaxMouseInterfaces) {
    return false;
  }
  for (unsigned i = 0; i < profile->interfaceCount; ++i) {
    const MouseHidInterface *interface = &profile->interfaces[i];
    if (interface->hidOffset + interface->hidLength > profile->configurationLength ||
        !interface->reportLength || interface->reportLength > MouseReportCapacity ||
        interface->inEndpoint < 1 || interface->inEndpoint > NUM_ENDPOINTS ||
        !interface->inPacketSize || interface->inPacketSize > 64 ||
        (interface->outEndpoint && (interface->outEndpoint > NUM_ENDPOINTS || !interface->outPacketSize ||
         interface->outPacketSize > 64))) {
      return false;
    }
  }
  return true;
}

static bool buildDescriptors(const MouseProfile *profile) {
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
      if ((!copied && entry.length != configLength) || configLength > MouseDescriptorCapacity) {
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
      const MouseString *string = &profile->strings[i];
      uint16_t value = 0x0300 | string->index;
      if (output + 1 >= MaxDescriptorEntries) return false;
      __wrap_usb_descriptor_list[output++] = (usb_descriptor_list_t){
        value, string->language, string->data, string->length
      };
    }
    for (unsigned i = 0; i < profile->interfaceCount; ++i) {
      const MouseHidInterface *interface = &profile->interfaces[i];
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

bool startMouseDevice(const MouseProfile *profile, uint8_t mouseInterface) {
  if (!profile || !validProfile(profile) || !buildDescriptors(profile)) {
    if (!buildDescriptors(NULL)) return false;
    profile = NULL;
  }
  activeProfile = profile;
  activeMouseInterface = mouseInterface;
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
static transfer_t txTransfers[MaxMouseInterfaces][2] __attribute__((aligned(32)));
DMAMEM static uint8_t txBuffers[MaxMouseInterfaces][2][64] __attribute__((aligned(32)));
static unsigned nextTx[MaxMouseInterfaces];
static unsigned nextRx1;
static transfer_t rxTransfers[MaxMouseInterfaces][2] __attribute__((aligned(32)));
DMAMEM static uint8_t rxBuffers[MaxMouseInterfaces][2][64] __attribute__((aligned(32)));

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

static void configTx(uint8_t endpoint, uint16_t packetSize) {
  if (endpoint == 1) configureEndpoint1(true, packetSize);
  else usb_config_tx(endpoint, packetSize, 0, NULL);
}

static void configRx(uint8_t endpoint, uint16_t packetSize) {
  if (endpoint == 1) configureEndpoint1(false, packetSize);
  else usb_config_rx(endpoint, packetSize, 0, receiveCallback);
}

static void transmitEndpoint(uint8_t endpoint, transfer_t *transfer) {
  if (endpoint == 1) scheduleEndpoint1(true, transfer);
  else usb_transmit(endpoint, transfer);
}

static void receiveEndpoint(uint8_t endpoint, transfer_t *transfer) {
  if (endpoint == 1) scheduleEndpoint1(false, transfer);
  else usb_receive(endpoint, transfer);
}

static void queueReceive(unsigned interfaceIndex, unsigned slot) {
  const MouseHidInterface *interface = &activeProfile->interfaces[interfaceIndex];
  transfer_t *transfer = &rxTransfers[interfaceIndex][slot];
  uint8_t *buffer = rxBuffers[interfaceIndex][slot];
  usb_prepare_transfer(transfer, buffer, interface->outPacketSize, (interfaceIndex << 1) | slot);
  arm_dcache_delete(buffer, interface->outPacketSize);
  receiveEndpoint(interface->outEndpoint, transfer);
}

static void completeReceive(unsigned interfaceIndex, unsigned slot) {
  transfer_t *transfer = &rxTransfers[interfaceIndex][slot];
  const MouseHidInterface *interface = &activeProfile->interfaces[interfaceIndex];
  uint32_t length = completedPacketLength(interface->outPacketSize, transfer->status);
  if (length) {
    forwardHidOutput(interface->number, rxBuffers[interfaceIndex][slot], length);
  }
  queueReceive(interfaceIndex, slot);
}

static void receiveCallback(transfer_t *transfer) {
  unsigned encoded = transfer->callback_param;
  unsigned interfaceIndex = encoded >> 1;
  unsigned slot = encoded & 1;
  if (!activeProfile || interfaceIndex >= activeProfile->interfaceCount) return;
  completeReceive(interfaceIndex, slot);
}

void pollMouseDevice() {
  if (!activeProfile || !usb_configuration) return;
  for (unsigned i = 0; i < activeProfile->interfaceCount; ++i) {
    if (activeProfile->interfaces[i].outEndpoint != 1) continue;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
      for (unsigned count = 0; count < 2; ++count) {
        if (usb_transfer_status(&rxTransfers[i][nextRx1]) & 0x80) break;
        completeReceive(i, nextRx1);
        nextRx1 ^= 1;
      }
    }
    break;
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
    bool transmit = false, receive = false;
    for (unsigned i = 0; i < activeProfile->interfaceCount; ++i) {
      const MouseHidInterface *interface = &activeProfile->interfaces[i];
      transmit |= interface->inEndpoint == endpoint;
      receive |= interface->outEndpoint == endpoint;
    }
    if (transmit || receive) {
      volatile uint32_t *control = (volatile uint32_t *)&USB1_ENDPTCTRL0 + endpoint;
      *control = (receive ? ENDPOINT_RECEIVE_INTERRUPT : ENDPOINT_RECEIVE_UNUSED) |
                 (transmit ? ENDPOINT_TRANSMIT_INTERRUPT : ENDPOINT_TRANSMIT_UNUSED);
    }
  }
  for (unsigned i = 0; i < activeProfile->interfaceCount; ++i) {
    const MouseHidInterface *interface = &activeProfile->interfaces[i];
    configTx(interface->inEndpoint, interface->inPacketSize);
    if (interface->outEndpoint) {
      configRx(interface->outEndpoint, interface->outPacketSize);
      queueReceive(i, 0);
      queueReceive(i, 1);
    }
  }
}

static int interfaceIndex(uint8_t number) {
  if (!activeProfile) return -1;
  for (unsigned i = 0; i < activeProfile->interfaceCount; ++i) {
    if (activeProfile->interfaces[i].number == number) return i;
  }
  return -1;
}

static bool transmitPacket(int index, const uint8_t *packet, uint8_t length, bool wait) {
  if (index < 0 || !activeProfile || !usb_configuration) return false;
  const MouseHidInterface *interface = &activeProfile->interfaces[index];
  if (!length || length > interface->inPacketSize) return false;
  uint32_t start = millis();
  for (;;) {
    uint32_t interruptState = __get_primask();
    __disable_irq();
    unsigned slot = nextTx[index];
    transfer_t *transfer = &txTransfers[index][slot];
    if (!(usb_transfer_status(transfer) & 0x80)) {
      memcpy(txBuffers[index][slot], packet, length);
      arm_dcache_flush_delete(txBuffers[index][slot], sizeof txBuffers[index][slot]);
      usb_prepare_transfer(transfer, txBuffers[index][slot], length, 0);
      nextTx[index] = slot ^ 1;
      transmitEndpoint(interface->inEndpoint, transfer);
      __set_primask(interruptState);
      return true;
    }
    __set_primask(interruptState);
    if (!wait || !usb_configuration || millis() - start >= 30) return false;
    yield();
  }
}

bool forwardHidInput(uint8_t interfaceNumber, const uint8_t *packet, uint8_t length) {
  return transmitPacket(interfaceIndex(interfaceNumber), packet, length, false);
}

bool sendMousePacket(const uint8_t *packet, uint8_t length) {
  return transmitPacket(interfaceIndex(activeMouseInterface), packet, length, true);
}
