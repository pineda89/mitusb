#pragma once

// Teensyduino supplies a no-op Arduino Serial for C++ with USB_DISABLED.
// Its C device driver needs the normal USB controller declarations.
#ifndef __cplusplus
#undef USB_DISABLED
#else
extern "C" void serialEvent(void) __attribute__((weak));
#endif

// Fallback IDs used when no supported mouse is available at boot.
#define VENDOR_ID 0x16C0
#define PRODUCT_ID 0x0482
#define MANUFACTURER_NAME {'T','e','e','n','s','y','d','u','i','n','o'}
#define MANUFACTURER_NAME_LEN 11
#define PRODUCT_NAME {'m','i','t','u','s','b',' ','M','o','u','s','e'}
#define PRODUCT_NAME_LEN 12
#define EP0_SIZE 64
#define NUM_INTERFACE 1
#define MOUSE_INTERFACE 0
// The generic profile uses endpoint 2. Copied profiles can also use endpoint 1.
#define NUM_ENDPOINTS 7
#define MOUSE_ENDPOINT 2
#define MOUSE_SIZE 8
#define MOUSE_INTERVAL 1
#define ENDPOINT2_CONFIG (ENDPOINT_RECEIVE_UNUSED | ENDPOINT_TRANSMIT_INTERRUPT)
