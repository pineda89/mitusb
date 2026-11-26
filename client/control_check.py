"""Issue the same HID control requests to a USB device and print what comes back.

Run it against a device plugged in directly and again through the board: with control
forwarding working, the two outputs match. Needs pyusb and libusb.

    python3 control_check.py 046d:c08b [interface]
"""
import sys

import usb.core

REQUESTS = (  # name, bmRequestType, bRequest, wValue, data or length
    ("GET_REPORT input, short reply", 0xA1, 0x01, 0x0100, 64),
    ("GET_IDLE", 0xA1, 0x02, 0x0000, 1),
    ("GET_PROTOCOL", 0xA1, 0x03, 0x0000, 1),
    ("GET_REPORT feature 0xff, likely stall", 0xA1, 0x01, 0x03FF, 64),
    ("SET_IDLE, no data", 0x21, 0x0A, 0x0000, b""),
    ("SET_REPORT output", 0x21, 0x09, 0x0200, b"\x00"),
)


def main():
    vendor, product = (int(x, 16) for x in sys.argv[1].split(":"))
    interface = int(sys.argv[2]) if len(sys.argv) > 2 else 0
    device = usb.core.find(idVendor=vendor, idProduct=product)
    if device is None:
        sys.exit("Device not found.")
    for name, request_type, request, value, payload in REQUESTS:
        try:
            reply = device.ctrl_transfer(request_type, request, value, interface, payload, timeout=1000)
            result = bytes(reply).hex() or "(empty)" if request_type & 0x80 else f"ok, {reply} bytes"
        except usb.core.USBError as error:
            result = f"error: {error.strerror}"
        print(f"{name}: {result}")
    # Standard requests the board cannot answer from its copies, so it has to ask the device.
    # They also work where the OS keeps class requests to itself (macOS without root).
    for name, value, index in (("string 0xee", 0x03EE, 0), ("product string, language 0", 0x0302, 0),
                               ("product string, language 0x0c0a", 0x0302, 0x0C0A)):
        try:
            reply = device.ctrl_transfer(0x80, 6, value, index, 255, timeout=1000)
            print(f"{name}: {bytes(reply).hex()}")
        except usb.core.USBError as error:
            print(f"{name}: error: {error.strerror}")

if __name__ == "__main__":
    main()
