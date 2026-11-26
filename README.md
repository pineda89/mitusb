# mitusb

A USB bridge for Teensy 4.1 that sits between a mouse, keyboard or gamepad and the computer. Connect the device to J6 and the computer to the micro-USB port. At startup, the board copies the device's USB identity and descriptors and then passes everything through in both directions. If there is nothing it can copy, it becomes a generic mouse.

The device works without Ethernet. With a mouse, the network client can also read events and send pointer commands; keyboards and gamepads are passed through untouched. Use a trusted network because the UDP protocol has no authentication. [Profiles, limits, and hardware checks](firmware/README.md)

## Build, test, and upload

Install PlatformIO Core and make sure `pio` is on your PATH. The first build downloads the toolchain and libraries. GNU Make is optional. The client requires Python 3.7 or later. You can run the native tests and build the firmware without a board.

Run the relevant command from the repository root.

```sh
make test    # Run the native tests
make         # Build the firmware
make upload  # Build and flash the board
make clean   # Remove the Teensy build output
```

The build produces `firmware/.pio/build/teensy41/firmware.hex`. Keep the device connected to J6 during restart so the firmware can capture its descriptors. If the loader cannot find the Teensy, press its **Program** button once.

The equivalent PlatformIO commands run from the firmware directory.

```sh
cd firmware
pio test -e native
pio run -e teensy41
pio run -e teensy41 -t upload
pio run -e teensy41 -t clean
```

## Directories

| Directory | Contents |
|---|---|
| [firmware/](firmware/) | Firmware, UDP protocol, and hardware checks |
| [client/](client/) | Command-line client and usage examples |
| [hardware/](hardware/) | KiCad project and board fabrication files |

## License

The project uses the [MIT License](LICENSE). The third-party KiCad libraries have [separate terms](hardware/README.md).
