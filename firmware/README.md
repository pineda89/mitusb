# mitusb firmware

Connect the mouse to J6 before powering up the Teensy 4.1. The micro-USB port connects the board to the computer. Ethernet is optional for physical mouse input. See the [project guide](../README.md) for prerequisites, build commands, and flashing instructions.

## USB profiles

| Profile | What the computer receives | After the mouse disconnects |
|---|---|---|
| `copied` | The USB identity, configuration, and report descriptors of the captured HID interfaces. The bridge forwards the bytes received in each physical input report. | Forwarding stops for those interfaces. Restart with the mouse connected to capture them again. |
| `generic` | A `mitusb Mouse` device with the Teensyduino VID/PID. The firmware reconstructs recognized movements and button changes. | You can connect a compatible mouse while the board is running. The profile stays generic until the next boot. |

The firmware waits up to 2.5 seconds to detect the mouse interface. Descriptor reads have separate timeouts. If the mouse is missing or capture fails, the board selects the generic profile.

The copied profile supports one USB configuration with up to three HID interfaces and no alternate interface settings. Each interface needs one interrupt IN endpoint and can have one interrupt OUT endpoint. The bridge preserves endpoint numbers 1 through 7. Packets can contain up to 64 bytes. The configuration and each report descriptor can contain up to 512 bytes. The bridge serves strings in the first language the mouse advertises.

USBHost_t36 enumerates peripherals on J6 before profile capture. The Teensy build patches its enumeration source in the build directory. Before requesting a full configuration, the host checks that the advertised length is at least the 9-byte header and fits its 2048-byte buffer. If the length falls outside that range, it stops enumerating that peripheral and releases the host for the next device. The 512-byte profile limit applies later, when the firmware decides whether to copy the configuration.

The link to the computer operates at full speed (12 Mbit/s). Transfer queues can drop reports, including button changes, when the destination is busy. Ethernet event reporting can also combine multiple physical reports into one sample.

The bridge forwards reports over interrupt endpoints. HID control requests such as `GET_REPORT`, `SET_REPORT`, and switching to boot protocol through `SET_PROTOCOL` are outside that path. This can affect DPI or lighting settings and mouse use in BIOS/UEFI. Matching report descriptors confirm the advertised format. Movement, buttons, and OUT transfers require separate functional checks.

## Side-button mode

Press left, right, back, and forward together on the physical mouse to toggle the mode. The mouse freezes for one second so the switch is noticeable, and all buttons are released on the computer. While the mode is on, the computer never sees back and forward, and the subscriber receives button and wheel events, including back and forward. While it is off, no events are sent. The mode is off after every restart. In `copied` mode, hiding the buttons requires a report format that the encoder recognizes.

## UDP protocol

Port 4242 accepts one text command per datagram, up to 63 bytes, with an optional trailing newline. The protocol has no authentication. Any device that can reach the port can control the pointer or subscribe to events. Use Ethernet on a trusted network.

| Command | Behavior |
|---|---|
| `profile` | Returns `copied` or `generic` and keeps the current subscription. |
| `hello` | Replaces the subscriber with the sender's IP address and port. |
| `move <dx> <dy>` | Accepts −32767 to 32767 per axis and splits movement into steps of up to 127 in either direction. |
| `wheel <n>` | Accepts the same range. Requires a wheel field in the profile. |
| `click left\|right\|middle` | Presses and releases an available button while preserving recorded physical button states. |

A 17-byte binary datagram starts a human-like action: a minimum-jerk move along a curved path, an optional click, and a correction move, paced by the 1 ms USB frame. While the physical mouse is moving, its reports carry the action's movement and buttons, so the computer receives one combined report. Otherwise the action sends its own report each frame. The board answers accepted actions with the single byte 121, and a new action replaces the one in progress. `mouse_action.h` documents the fields and `client/mitusb.py human` builds the packet.

The subscriber receives `mouse <buttons> <wheel>` when the physical buttons change or the wheel turns, but only while the [side-button mode](#side-button-mode) is on. Movement alone is not reported. The button mask uses 1 for left, 2 for right, 4 for middle, 8 for back, and 16 for forward. The firmware discards malformed commands. Movement, wheel, and click commands receive no acknowledgement. In `copied` mode, these commands require a report format that the encoder recognizes. Physical forwarding also works with other formats. [Client examples](../client/README.md)

## Hardware checks

1. Start the board with the mouse attached and Ethernet unplugged. Check movement, the wheel, and button presses and releases. Repeat startup without the mouse, then connect it to check the generic profile.
2. With Ethernet connected, run `python3 client/mitusb.py profile` from the repository root. The board announces itself as `mitusb.local` over mDNS and also requests the `mitusb` DHCP hostname. If the name does not resolve, pass the leased IP address with `--host`. Check the profile after both startup scenarios.
3. On Linux, locate the `report_descriptor` file for each interface under `/sys/bus/hid/devices/`. Hash it with `sha256sum` with the mouse connected directly and again through the board. In `copied` mode, each pair of hashes should match.
4. Compare transfer lengths and payloads in direct and bridged USB captures. Include a report shorter than the endpoint maximum and, if supported by the mouse, an interrupt OUT operation. In `copied` mode, confirm that disconnecting the mouse stops forwarding until the next boot.

## Source layout and test coverage

| Files in `src/` | Responsibility |
|---|---|
| `main.cpp` and `protocol.h` | Startup, physical events, subscriptions, and UDP commands |
| `mouse_action.*` | Human-like action timing and trajectory |
| `mouse_capture.*` | Mouse enumeration on J6 and descriptor capture |
| `mouse_profile.*` and `mouse_layout.*` | Configuration validation and encoding for recognized movement formats |
| `mouse_device.*` and `mouse_usb_profile.h` | PC-facing enumeration, the generic profile, and endpoint transfers |

`make test` runs native tests for HID configurations, encoding, packet lengths, and commands. USB integration uses linker options and internal Teensyduino 1.62 queue structures. The firmware manages endpoint 1 directly because the public core API supports endpoints 2 through 7. When updating the core, review the [USBHost_t36 build patch](scripts/usbhost_enumeration.py) and repeat the physical enumeration and transfer checks.
