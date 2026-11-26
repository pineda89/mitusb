# Keyboard and gamepad support: device kinds over a shared clone-and-proxy core

Date: 2026-11-26. Status: awaiting approval.

## Intent

mitusb is a USB mouse bridge today. This iteration makes it a bridge for three kinds
of device: mouse, keyboard and gamepad ("mando"). For all three the board must

1. **clone** the real device's USB identity and descriptors towards the computer, and
2. **proxy** what the device does, unmodified, in both directions.

The owner also wants the code organised as one implementation per device kind, so that
adding a fourth kind later is a matter of adding one more.

Test devices on hand: a wired Xbox 360 controller and an 8BitDo Ultimate 2C Wireless
Controller, plus the mouse already used and any USB keyboard.

## Goals

- A keyboard plugged into J6 at boot is cloned and works on the computer, including
  lock LEDs and BIOS/UEFI (boot protocol).
- A wired Xbox 360 controller plugged into J6 at boot is cloned and works on the
  computer as an Xbox 360 controller, including rumble and the ring LED.
- The mouse keeps everything it has today: cloning, human-like actions, text commands,
  side-button mode, subscriber events, and the generic fallback.
- `src/` has one module per kind (mouse, keyboard, gamepad) behind one small interface.

## Non-goals (this iteration)

- Injecting keys or gamepad input over UDP, or reporting their events to a subscriber.
  Keyboard and gamepad are pure proxy. The kind interface leaves the hook where this
  would go.
- More than one device at a time, or a hub with several devices.
- A generic fallback for keyboard or gamepad. With nothing clonable on J6 the board is
  still `mitusb Mouse`, as today.
- Devices with isochronous or bulk endpoints (DualShock/DualSense audio, Xbox One GIP
  over bulk), alternate interface settings, or more than one configuration.
- High-speed link to the computer. It stays full speed.
- Re-attaching after the device is unplugged. Forwarding stops until reboot, as today.
- Waking the computer from sleep with the keyboard (remote wakeup). The core has no
  path for it.
- Correcting high-speed devices for the full-speed link. A high-speed device whose
  descriptors fit the limits is cloned byte for byte, as mice already are today; what
  happens with the 8BitDo is recorded, not fixed.

## Anti-goals

- Regressing the mouse. It is the working product; every mouse behaviour in
  `firmware/README.md` must still hold.
- Three copies of the capture and transfer code, one per kind.
- A kind that parses or rewrites reports when it only needs to pass them through.

## Constraints

- Teensy 4.1, Teensyduino 1.62 (`framework-arduinoteensy@1.162.0`), USBHost_t36 on J6,
  the device controller on micro-USB. PlatformIO; native tests with Unity.
- The device controller gives 7 endpoint numbers per direction. Endpoint 1 is managed
  by hand (`mouse_device.c`); that code stays.
- The core's control-request handler (`endpoint0_setup` in `cores/teensy4/usb.c`) is
  `static`, so `--wrap` cannot reach it.
- USBHost_t36 labels the wired 360 controller (045E:028E) as a Switch pad, so its
  `JoystickController` is not usable for it.

## Approaches

**A. One raw device-level driver (chosen, combined with per-kind modules).** A single
USBHost_t36 driver claims the whole device, the descriptors are copied verbatim, and
every interrupt endpoint is piped whatever its class. Most change, one path, works for
any interrupt-only device.

**B. One USBHost driver per kind.** Keep the mouse path, add `KeyboardController` and a
custom 360 driver. Leaves the mouse alone but triplicates capture, and the 360 driver
has to be written anyway.

**C. Generalise the HID path for keyboard, raw driver only for the gamepad.** Less
mouse risk than A, but ends with A's raw driver plus the HID path beside it.

The owner left the choice open, then asked for one implementation per kind. The design
takes A for capture and transfer, where the three kinds do not differ, and gives each
kind its own module for what does differ: recognising the device and any extras. This
gives the per-kind structure without B's duplication.

## Design

### Shared core

Renamed from the `mouse_*` files, because they stop being mouse-specific.

**`usb_profile.*` (was `mouse_profile.*`), pure C++, native-tested.** Validates a device
descriptor plus one configuration and indexes it. Accepts:

- 1 to 8 interfaces of any class, no alternate settings; an interface may have zero
  endpoints (the 360's security interface);
- interrupt endpoints only, numbers 1 to 7, each number used at most once per
  direction, packets up to 64 bytes;
- configuration up to 512 bytes; unknown descriptors (HID, the 360's vendor `0x21`
  blocks) are kept in place because the configuration is copied byte for byte;
- for HID interfaces, a report descriptor of up to 512 bytes.

The profile lists interfaces (number, class triple, offset, optional HID descriptor
offset and report descriptor) and endpoints (number, direction, packet size, interval,
owning interface).

**`usb_capture.*` (was `mouse_capture.*`).** A `USBDriver` subclass constructed before
any other driver. A device-level claim is one synchronous call that sees only
the configuration descriptor and cannot be undone, so capture has two stages:

1. Inside `claim()`, during the boot window: decline hubs (class 9) and validate the
   configuration against the profile limits. If it does not fit, decline, and
   `USBHIDParser` and `MouseController` serve the generic mouse exactly as today.
2. After claiming: read the device and string descriptors and the report descriptor
   of each HID interface (`GET_DESCRIPTOR 0x22` to the interface), open one pipe per
   endpoint and keep an IN transfer queued on each. If any read fails here the device
   cannot be handed back, so the board restarts once with capture disabled for that
   boot (a flag in a register that survives a soft reset) and comes up generic with a
   working mouse.

Each completed IN transfer goes to the device side under the same endpoint number.
Outside the boot window the driver declines every claim.
`CapturingHIDParser` and `RawHIDInput` are deleted.

The parser must treat a `0x21` descriptor as HID only inside a class-3 interface; the
360 pad has vendor `0x21` blocks.

**`usb_device.*` (was `mouse_device.*`).** Same descriptor substitution and endpoint
code, keyed by endpoint number instead of by interface: every IN endpoint in the
profile gets a transmit queue, every OUT endpoint a receive queue whose packets go back
to the host side. `MaxDescriptorEntries` (24 today) grows to fit 8 interfaces plus
strings.

**Control-request forwarding (new).** In copied mode, class and vendor requests, and
standard requests the board cannot answer from its copies (for example a string it did
not capture), are forwarded to the real device and its answer, or its stall, is
relayed. This covers keyboard LEDs (`SET_REPORT`), `SET_PROTOCOL`/`SET_IDLE`, HID
feature reports, and Windows' vendor requests to the 360 pad.

Mechanism: a build-time patch of `cores/teensy4/usb.c`, in the style of
`scripts/usbhost_enumeration.py`, that calls a hook from `endpoint0_setup` and
`endpoint0_complete` and exposes `endpoint0_transmit`/`endpoint0_receive`. The hook
only records the request; the main loop performs the transfer on the host side and
then completes the data or status stage, so nothing blocks in the interrupt. One
request in flight; a new SETUP drops the old one; no answer within 500 ms is a stall.
Data stage up to 512 bytes.

What the library source imposes on this, all to be settled in the spike:

- USBHost_t36 reports `wLength`, not the bytes received, when a control transfer
  completes (`ehci.cpp`). Relaying short replies correctly needs a second build-time
  patch there, same technique. It is authorised.
- A device stall never reaches `control()`. The capture driver installs its own error
  callback on the control pipe so a stall is relayed at once instead of after the
  timeout.
- For requests with OUT data (`SET_REPORT`) the core acknowledges the status stage
  together with the data stage. The computer therefore sees success before the device
  answers; a stall from the device on such a request is not relayed. Accepted.
- A queued control transfer cannot be cancelled. A request the device never answers
  blocks later ones; accepted, the request is stalled towards the computer.

On a bus reset from the computer the board sends `SET_PROTOCOL(report)` to each HID
interface of the real device, so a keyboard left in boot protocol by the BIOS returns
to report protocol when the OS takes over.

### Device kinds

`device_kind.h` declares the interface; `kind_mouse.cpp`, `kind_keyboard.cpp` and
`kind_gamepad.cpp` implement it. A kind provides:

- `name`: `mouse`, `keyboard`, `gamepad`;
- `match(profile, interface)`: does this interface belong to the kind;
- `attach(profile, interface)`: optional setup after a match;
- `input(endpoint, packet, length)`: optional filter called for each IN packet of a
  matched interface before it is forwarded. It may edit or drop the packet. Keyboard
  and gamepad do not set it.

Matching:

| Kind | Matches an interface when |
|---|---|
| mouse | HID, and `readMouseLayout` recognises its report descriptor |
| keyboard | HID boot keyboard (class 3, subclass 1, protocol 1), or its report descriptor's top-level usage is Generic Desktop / Keyboard |
| gamepad | Xbox 360 control interface (class `FF`, subclass `5D`, protocol `01`), or HID with top-level usage Joystick or Gamepad |

Every interface is proxied whether or not a kind matches it. A device that matches no
kind is still cloned and reported as `other`. An interface gets at most one kind,
tried in the order gamepad, keyboard, mouse. Composite devices (a gaming mouse with a
keyboard interface) attach each kind to its own interface; the reported kind is that
of the lowest-numbered matching interface.

The 8BitDo Ultimate 2C on a cable (2DC8:310A, read on 2026-11-26) is such a composite
and fits the limits: full speed, 8-byte EP0, 106-byte configuration, interface 0 Xbox
360 style (`FF/5D/01`, IN 4 and OUT 5, 32 bytes), interface 1 a boot keyboard whose
report descriptor also carries consumer and mouse collections (IN 2, 16 bytes), and
interface 2 vendor HID (IN 3 and OUT 6, 64 bytes). It reports as `gamepad`; its
interface 1 is `keyboard`, not `mouse`, because of the order above.

**Mouse kind.** Holds what is mouse-specific today in `main.cpp` and
`mouse_capture.cpp`: the layout, button hiding, merging of injected movement, the
side-button mode and subscriber events. Its `input` filter does the hide and merge. In
copied mode the physical button and wheel state now comes from decoding the raw report
with the layout (new `decodeMouse` in `mouse_layout.*`, native-tested) because
`MouseController` no longer sees a cloned device. The generic fallback keeps using
`MouseController`.

**Keyboard and gamepad kinds.** Matcher and name only. Their files are where injection
would be added later.

### UDP protocol

`profile` answers `copied <kind>` (`copied mouse`, `copied keyboard`,
`copied gamepad`, `copied other`) or `generic`. `client/mitusb.py profile` accepts
only the exact replies `copied` and `generic` today, so it must accept the new forms;
`client/README.md` and `firmware/README.md` change with it.
Mouse commands and human actions are ignored unless a mouse kind is attached or the
profile is generic.

## Testing

Native (`make test`): profile parsing with fixtures for a mouse, a boot keyboard and
the wired 360 controller's 153-byte configuration, plus rejections (isochronous
endpoint, alternate setting, endpoint 8, duplicate endpoint); kind matching on those
fixtures; `decodeMouse` round-tripping with `encodeMouse`; existing tests kept.

On the board, added to the hardware checks in `firmware/README.md`:

- mouse: the existing checks, unchanged;
- keyboard: typing, Caps Lock LED follows the computer, usable in BIOS/UEFI;
- 360 pad: recognised as an Xbox 360 controller, all inputs, rumble, ring LED;
- 8BitDo 2C (cable and dongle): record whether it clones; if its descriptors fall
  outside the limits above, the outcome is the generic fallback, not a fix in this
  iteration.

## Order of work

1. Spike control forwarding on the current mouse firmware: confirm the build can patch
   a core file, then forward a `GET_REPORT`, a reply shorter than `wLength`, a request
   the device stalls, and a `SET_REPORT`. It is the riskiest piece; if it cannot be
   done cleanly the design comes back for revision.
2. Generalise the profile and its tests.
3. Raw capture driver and endpoint-keyed device side; mouse back to parity.
4. Kinds, `decodeMouse`, `profile` reply.
5. Keyboard and gamepad on the board; docs.

## Calls made by the designer (not decided by the owner)

- No UDP injection or events for keyboard and gamepad in this iteration.
- One device per boot.
- Limits: 8 interfaces, interrupt endpoints only, 64-byte packets.
- `profile` reply gains the kind as a second word.
- Reported kind for composite devices is that of the lowest-numbered matching interface.
- Unknown interrupt-only devices are cloned as `other` rather than rejected.
- File renames from `mouse_*` to `usb_*` for the shared core.
- A capture that fails after the claim restarts the board once into generic mode.
- A second build patch, to USBHost_t36's `ehci.cpp`, is allowed.
- Stalls on requests with OUT data are not relayed.
- `SET_PROTOCOL(report)` is sent to the device on a bus reset.
- No wake-from-sleep; high-speed devices are cloned as they are.
- Copied-mode mouse events and the mode toggle need a recognised layout (today
  `MouseController` could parse some layouts the encoder does not).

## Left to the builder

Buffer and queue sizing on the host side, the exact shape of the `usb.c` patch, how
the boot window is timed across kinds (2.5 s today), and internal naming.
