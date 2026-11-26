# mitusb firmware

Connect the device to J6 before powering up the Teensy 4.1. The micro-USB port connects the board to the computer. Ethernet is optional. See the [project guide](../README.md) for prerequisites, build commands, and flashing instructions.

## USB profiles

| Profile | What the computer receives | After the device disconnects |
|---|---|---|
| `copied` | The USB identity, configuration, strings, and HID report descriptors of the device on J6. The bridge forwards the bytes of every packet in both directions. | Forwarding stops. Restart with the device connected to capture it again. |
| `generic` | A `mitusb Mouse` device with the Teensyduino VID/PID. The firmware reconstructs recognized movements and button changes. | You can connect a compatible mouse while the board is running. The profile stays generic until the next boot. |

The firmware waits up to 2.5 seconds for a device. If none appears, or its configuration is outside the limits below, the board selects the generic profile. If the device is accepted but a later descriptor read fails, the board restarts once and comes up generic, because a device it has claimed cannot be handed to the generic mouse path.

The copied profile supports one USB configuration with up to eight interfaces of any class and no alternate interface settings. Every endpoint must be an interrupt endpoint, numbered 1 through 7 and used once per direction, with packets of up to 64 bytes. An interface may have no endpoints. The configuration and each report descriptor can contain up to 512 bytes. The bridge serves strings in the first language the device advertises and asks the device for any other. Devices with isochronous or bulk endpoints, such as controllers with audio, do not fit.

USBHost_t36 enumerates peripherals on J6 before profile capture. The Teensy build patches three files in the build directory and stops if any of them no longer matches:

| Patched file | Script | Why |
|---|---|---|
| `USBHost_t36/enumeration.cpp` | [`usbhost_enumeration.py`](scripts/usbhost_enumeration.py) | Rejects a configuration whose advertised length is under 9 bytes or over the 2048-byte buffer, and releases the host for the next device. |
| `cores/teensy4/usb.c` | [`usb_core_control.py`](scripts/usb_core_control.py) | Lets the firmware take control requests before the core answers or stalls them. |
| `USBHost_t36/ehci.cpp` | [`usbhost_control_length.py`](scripts/usbhost_control_length.py) | Reports how many bytes a control transfer really moved. |

The link to the computer operates at full speed (12 Mbit/s). A high-speed device that fits the limits is copied as it is. Transfer queues can drop reports, including button changes, when the destination is busy.

## Device kinds

A kind recognizes the interfaces it understands. Every interface is forwarded whether or not a kind matches it, and an interface gets at most one kind, tried in this order.

| Kind | Matches an interface when | What it adds |
|---|---|---|
| `gamepad` | It is the control interface of an Xbox 360 style controller (class `FF`, subclass `5D`, protocol `01`), or HID with a joystick or gamepad as its first collection. | Nothing. Pure pass-through. |
| `keyboard` | It is a HID boot keyboard, or HID with a keyboard as its first collection. | Nothing. Pure pass-through. |
| `mouse` | It is HID and the report format is one the encoder recognizes. | Injected movement and clicks, the side-button mode, and subscriber events. |

The device is reported as the kind of its lowest-numbered matching interface, or `other` when no interface matches. A composite device keeps each kind on its own interface: an 8BitDo Ultimate 2C on a cable is a `gamepad` whose second interface is a `keyboard`. Only the first mouse interface of a device gets the mouse features.

## Control requests

In the copied profile the board answers standard requests from its copies. It forwards to the device every class and vendor request, and any descriptor it did not copy, and relays the answer or the stall. This carries keyboard lock LEDs, boot protocol for BIOS/UEFI, HID feature reports, and the vendor requests a computer sends to a controller. A request the device does not answer within 500 ms is stalled.

When the computer resets the bus, the board sets every boot interface of the device back to report protocol, so a keyboard used in the BIOS works again once the operating system starts.

Two cases differ from a direct connection. A request that carries data to the device, such as `SET_REPORT`, is acknowledged to the computer before the device answers, so a stall from the device on such a request is not relayed. The board does not offer remote wakeup, so a key press does not wake a sleeping computer.

## Side-button mode

Press left, right, back, and forward together on the physical mouse to toggle the mode. The mouse freezes for one second so the switch is noticeable, and all buttons are released on the computer. While the mode is on, the computer never sees back and forward, and the subscriber receives button and wheel events, including back and forward. While it is off, no events are sent. The mode is off after every restart. The mode needs a mouse. In `copied` mode it also needs a report format that the encoder recognizes: the board reads the buttons from the forwarded reports.

## UDP protocol

Port 4242 accepts one text command per datagram, up to 63 bytes, with an optional trailing newline. The protocol has no authentication. Any device that can reach the port can control the pointer or subscribe to events. Use Ethernet on a trusted network.

| Command | Behavior |
|---|---|
| `profile` | Returns `generic`, or `copied` and the [device kind](#device-kinds): `copied mouse`, `copied keyboard`, `copied gamepad` or `copied other`. Keeps the current subscription. |
| `hello` | Replaces the subscriber with the sender's IP address and port. |
| `move <dx> <dy>` | Accepts −32767 to 32767 per axis and splits movement into steps of up to 127 in either direction. |
| `wheel <n>` | Accepts the same range. Requires a wheel field in the profile. |
| `click left\|right\|middle` | Presses and releases an available button while preserving recorded physical button states. |

A 17-byte binary datagram starts a human-like action: a minimum-jerk move along a curved path, an optional click, and a correction move, paced by the 1 ms USB frame. While the physical mouse is moving, its reports carry the action's movement and buttons, so the computer receives one combined report. Otherwise the action sends its own report each frame. The board answers accepted actions with the single byte 121, and a new action replaces the one in progress. `mouse_action.h` documents the fields and `client/mitusb.py human` builds the packet.

The subscriber receives `mouse <buttons> <wheel>` when the physical buttons change or the wheel turns, but only while the [side-button mode](#side-button-mode) is on. Movement alone is not reported. The button mask uses 1 for left, 2 for right, 4 for middle, 8 for back, and 16 for forward. The firmware discards malformed commands. Movement, wheel, and click commands receive no acknowledgement. In `copied` mode, these commands and the human-like action require a cloned mouse with a report format that the encoder recognizes; with a keyboard or gamepad they do nothing. Physical forwarding works for every device. [Client examples](../client/README.md)

## Hardware checks

1. Start the board with the mouse attached and Ethernet unplugged. Check movement, the wheel, and button presses and releases. Repeat startup without the mouse, then connect it to check the generic profile.
2. With Ethernet connected, run `python3 client/mitusb.py profile` from the repository root. The board announces itself as `mitusb.local` over mDNS and also requests the `mitusb` DHCP hostname. If the name does not resolve, pass the leased IP address with `--host`. Check the profile after both startup scenarios and with each kind of device.
3. On Linux, locate the `report_descriptor` file for each interface under `/sys/bus/hid/devices/`. Hash it with `sha256sum` with the device connected directly and again through the board. In `copied` mode, each pair of hashes should match.
4. Compare transfer lengths and payloads in direct and bridged USB captures. Include a report shorter than the endpoint maximum and, if supported by the device, an interrupt OUT operation. In `copied` mode, confirm that disconnecting the device stops forwarding until the next boot.
5. Keyboard: type, check that the Caps Lock and Num Lock LEDs follow the computer, and use it in BIOS/UEFI and again after the operating system starts.
6. Gamepad: check that the computer recognizes the same controller as when it is connected directly, then every stick, trigger and button, rumble, and the ring LED on an Xbox 360 controller.
7. Control requests: run `python3 client/control_check.py <vid>:<pid>` (needs pyusb) with the device connected directly and again through the board. The two outputs should match. macOS keeps HID class requests to itself without root, so only the last three lines are meaningful there.

## Source layout and test coverage

| Files in `src/` | Responsibility |
|---|---|
| `main.cpp` and `protocol.h` | Startup, mouse events and injection, subscriptions, and UDP commands |
| `usb_profile.*` | Validation and index of the copied device's configuration |
| `usb_capture.*` | The device on J6: claiming it, reading its descriptors, and moving packets on its endpoints |
| `usb_device.*` and `mouse_usb_profile.h` | PC-facing enumeration, the generic profile, and endpoint transfers |
| `usb_control.*` | Forwarding of control requests to the device |
| `device_kind.*`, `kind_mouse.cpp`, `kind_keyboard.cpp`, `kind_gamepad.cpp` | One module per device kind. To add a kind, add a file with its `match` and list it in `kindOf` |
| `mouse_layout.*` | Encoding and decoding of recognized mouse report formats |
| `mouse_action.*` | Human-like action timing and trajectory |

`make test` runs native tests for device configurations, kind matching, mouse encoding and decoding, packet lengths, and commands. The fixtures include the descriptors of a real 8BitDo controller. USB integration uses linker options and internal Teensyduino 1.62 queue structures. The firmware manages endpoint 1 directly because the public core API supports endpoints 2 through 7. When updating the core, review the three build patches above and repeat the hardware checks.
