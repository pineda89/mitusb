# mitusb

A USB mouse bridge for Teensy 4.1. Connect the mouse to J6 and the computer to the micro-USB port. At startup, the board tries to copy the mouse's USB identity and supported HID interfaces. If capture fails, it uses the generic mouse profile.

The physical mouse works without Ethernet. The network client can query the profile, read events, and send pointer commands. Use a trusted network because the UDP protocol has no authentication. [Profiles, limits, and hardware checks](firmware/README.md)

## Build, test, and upload

Install PlatformIO Core and make sure `pio` is on your PATH. The first build downloads the toolchain and libraries. GNU Make is optional. The client requires Python 3.7 or later. You can run the native tests and build the firmware without a board.

Run the relevant command from the repository root.

```sh
make test    # Run the native tests
make         # Build the firmware
make upload  # Build and flash the board
make clean   # Remove the Teensy build output
```

The build produces `firmware/.pio/build/teensy41/firmware.hex`. Keep the mouse connected to J6 during restart so the firmware can capture its descriptors. If the loader cannot find the Teensy, press its **Program** button once.

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
