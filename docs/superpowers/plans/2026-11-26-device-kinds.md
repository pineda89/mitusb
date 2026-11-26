# Device Kinds (keyboard and gamepad) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** mitusb clones and proxies a keyboard or a gamepad as it already does a mouse, with one module per device kind over a shared core.

**Architecture:** A raw USBHost_t36 driver claims the whole device on J6, copies its descriptors and pipes every interrupt endpoint to the same endpoint number on the PC side; control requests the board cannot answer are forwarded to the real device. Device kinds (`mouse`, `keyboard`, `gamepad`) recognise interfaces and may filter their input; only the mouse does.

**Tech Stack:** Teensy 4.1, Teensyduino 1.62 (`framework-arduinoteensy@1.162.0`), USBHost_t36, QNEthernet, PlatformIO, Unity native tests, Python 3 client.

**Spec:** `docs/superpowers/specs/2026-11-26-device-kinds-design.md`

## Global Constraints

- Work on branch `device-kinds`; commit at the end of each task.
- Every mouse behaviour described in `firmware/README.md` must still hold after each task that builds for the board.
- Clone limits: 1 to 8 interfaces, no alternate settings, one configuration; interrupt endpoints only, numbers 1 to 7, each used once per direction, packets up to 64 bytes; configuration up to 512 bytes; HID report descriptor up to 512 bytes.
- The PC link stays full speed. No new library dependencies.
- Endpoint 1 stays hand-managed (`configureEndpoint1` / `scheduleEndpoint1`); do not rewrite that code, only re-key it.
- Core and library changes are build-time patches in `firmware/scripts/`, each failing the build if its anchor text is not found exactly once (as `usbhost_enumeration.py` does).
- Keyboard and gamepad are pure proxy: no report parsing or rewriting.
- `make test` (native) passes at the end of every task; `make` (Teensy build) passes at the end of every task from Task 3 on.
- Tasks marked **[board]** end with a check on hardware that needs the owner: stop and ask them to run it.

## Review Focus

1. Device unplugged while a forwarded control request is in flight: the PC's request is stalled within 500 ms and the board keeps running (Task 6, board check).
2. A configuration whose descriptor lengths are truncated or overrun the buffer: rejected, nothing read past the end (Task 2 tests).
3. A descriptor read failing after the claim: one restart into generic mode with a working mouse, never a restart loop (Task 5, board check with the flag forced).
4. The 8BitDo's keyboard interface carries a mouse collection: it is `keyboard`, mouse commands are ignored, nothing is merged into its reports (Task 4 tests).
5. Device sends input while the PC has not configured the board, or a packet longer than the endpoint's size: dropped, no crash (Task 3 code path, Task 5 board check by booting with the PC cable unplugged).

---

### Task 1: Spike control forwarding [board, throwaway]

Throwaway branch `spike-control` off `main`; nothing from it is merged. It answers whether the design's forwarding mechanism works.

**Files:** scratch scripts under `firmware/scripts/`, edits to `mouse_capture.cpp`, `mouse_device.c`, `main.cpp`.

- [ ] **Step 1:** Confirm PlatformIO build middleware can replace a core file: patch `cores/teensy4/usb.c` in the build dir (pattern `*/cores/teensy4/usb.c`, same shape as `usbhost_enumeration.py`) to add a visible marker symbol; check it with `arm-none-eabi-nm` on `firmware.elf`. If middleware does not see core files, find the mechanism that does before going on.
- [ ] **Step 2:** In the patched `usb.c`: call `int usb_proxy_setup(uint64_t setup)` (weak, returns 0) at the top of `endpoint0_setup`; when it returns 1 the core does nothing more for that SETUP. Call `void usb_proxy_data(const uint8_t *data, uint32_t length)` (weak) from `endpoint0_complete`. Add non-static `usb_proxy_transmit(const void*, uint32_t)`, `usb_proxy_receive(void*, uint32_t)`, `usb_proxy_stall(void)` wrapping the static functions.
- [ ] **Step 3:** Host side: send the recorded SETUP to the real mouse with `queue_Control_Transfer` from the main loop and relay the reply. Install an error callback on the control pipe so a stall is seen. Patch `ehci.cpp` so the completion carries the bytes actually received.
- [ ] **Step 4:** With a mouse cloned, from the PC (Linux `usbhid-dump`/`hidapi`, or pyusb) issue: a `GET_REPORT`; a `GET_DESCRIPTOR` for a string index the board did not capture (short reply); a request the mouse stalls; a `SET_REPORT` or `SET_IDLE`. Record for each whether the PC saw the same result as with the mouse plugged in directly.
- [ ] **Step 5:** Write the findings (what worked, exact patch anchors, anything the spec got wrong) in `docs/superpowers/plans/2026-11-26-control-spike-notes.md` on `device-kinds`. If any of the four cases cannot be made to work, stop: the spec goes back to the owner.

---

### Task 2: Generic USB profile

**Files:**
- Create: `firmware/src/usb_profile.h`, `firmware/src/usb_profile.cpp` (from `mouse_profile.*`, deleted)
- Create: `firmware/test/fixtures.h`
- Test: `firmware/test/test_usb_profile/test_usb_profile.cpp` (from `test_mouse_profile`, deleted)
- Modify: `firmware/platformio.ini` (`build_src_filter`)

**Interfaces — Produces:**

```c
enum { UsbConfigurationCapacity = 512, UsbReportCapacity = 512, UsbStringCapacity = 255,
       MaxUsbStrings = 16, MaxUsbInterfaces = 8, MaxUsbEndpoints = 14 };
typedef struct UsbString { uint16_t language; uint8_t index, length; uint8_t data[UsbStringCapacity]; } UsbString;
typedef struct UsbInterface {
  uint16_t offset, hidOffset, reportLength;   // hidOffset/reportLength are 0 unless class 3
  uint8_t number, hidLength, interfaceClass, subClass, protocol;
  uint8_t report[UsbReportCapacity];
} UsbInterface;
typedef struct UsbEndpoint { uint8_t number, in, interface /* index */, interval; uint16_t packetSize; } UsbEndpoint;
typedef struct UsbProfile {
  uint8_t device[18];
  uint8_t configuration[UsbConfigurationCapacity]; uint16_t configurationLength;
  UsbInterface interfaces[MaxUsbInterfaces]; uint8_t interfaceCount;
  UsbEndpoint endpoints[MaxUsbEndpoints]; uint8_t endpointCount;
  UsbString strings[MaxUsbStrings]; uint8_t stringCount;
} UsbProfile;
bool readUsbProfile(const uint8_t *device, size_t deviceLength,
                    const uint8_t *configuration, size_t configurationLength, UsbProfile &result);
// Same checks on the configuration alone; usable inside a USBHost claim().
bool validUsbConfiguration(const uint8_t *configuration, size_t length);
```

`fixtures.h` holds `static const uint8_t` arrays: the existing mouse fixture from the old test; `PadDevice`/`PadConfiguration`, the 8BitDo read on 2026-11-26:

```
device  1201000200000008c82d0a31140101020301
config  09026a0003010080fa0904000002ff5d01001121000101258414000000001305080000070584032000010705050320000109040100010301010009211101000122a600070582031000080904020002030000000921110100012221000705830340000107050603400001
```

and `Xbox360Device`/`Xbox360Configuration`, the wired controller's published 153-byte configuration (045E:028E, four interfaces, IN 1–4, OUT 1–3, 32-byte packets); mark it in a comment as taken from public documentation until Task 7 replaces it with a capture from the owner's pad.

- [ ] **Step 1: Failing tests.** Keep the old tests' cases, adapted. Add:
  - `pad_profile`: `readUsbProfile(Pad…)` true; `interfaceCount == 3`; `endpointCount == 5`; interface 0 `{0xFF, 0x5D, 0x01}`, `hidOffset == 0`, `reportLength == 0`; interface 1 `{3, 1, 1}`, `reportLength == 166`; interface 2 `reportLength == 33`; endpoints `{4 in, 32}`, `{5 out, 32}`, `{2 in, 16, interval 8}`, `{3 in, 64}`, `{6 out, 64}` each with the right interface index.
  - `xbox360_profile`: true; `interfaceCount == 4`; `endpointCount == 7`; interface 3 has no endpoints; no interface has `hidOffset`.
  - Rejections, each by mutating a copy of `PadConfiguration` (fix `wTotalLength` where needed): isochronous endpoint (`bmAttributes = 1`); bulk (`2`); `bAlternateSetting = 1`; endpoint address `0x88`; two IN endpoints numbered 4; packet size 65; `bNumInterfaces = 9`; a descriptor `bLength` running past the end; `bLength = 1`; length argument 10 bytes shorter than `wTotalLength`; device `bNumConfigurations = 2`.
  - `validUsbConfiguration` agrees with `readUsbProfile` on every case above.
- [ ] **Step 2:** `make test` → the new tests fail to compile or fail.
- [ ] **Step 3:** Implement. A `0x21` descriptor is parsed as HID only inside a class-3 interface; elsewhere it is skipped like any unknown descriptor. A class-3 interface without a valid HID descriptor is rejected.
- [ ] **Step 4:** `make test` → all pass.
- [ ] **Step 5:** Commit `Generalise the USB profile beyond HID mice`. (The Teensy build is broken until Task 3; that is expected for this one commit.)

---

### Task 3: Endpoint-keyed device side [board]

**Files:**
- Create: `firmware/src/usb_device.h`, `firmware/src/usb_device.c` (from `mouse_device.*`, deleted)
- Modify: `firmware/src/mouse_capture.cpp`, `firmware/src/mouse_capture.h`, `firmware/src/main.cpp`, `firmware/platformio.ini` if file names appear

**Interfaces — Consumes:** `UsbProfile` (Task 2). **Produces:**

```c
enum { UsbDescriptorCapacity = 512 };
bool startUsbDevice(const UsbProfile *profile);            // NULL or invalid -> generic mouse, returns false
bool sendUsbPacket(uint8_t endpoint, const uint8_t *packet, uint8_t length, bool wait);  // IN endpoint number
void forwardUsbOutput(uint8_t endpoint, const uint8_t *packet, uint8_t length);          // implemented by the host side
void pollUsbDevice(void);
```

`completedPacketLength` moves here unchanged.

- [ ] **Step 1:** Re-key transmit/receive queues, buffers and `callback_param` by endpoint number (1–7) instead of interface index. `sendUsbPacket` returns false when the endpoint is not an IN endpoint of the profile, `length` is 0 or above its packet size, or the PC has not configured the board. Raise `MaxDescriptorEntries` to 48.
- [ ] **Step 2:** Adapt the existing HID capture to compile and behave as before against `UsbProfile`: it rejects profiles with a non-HID interface or more than 3 interfaces (its three parsers), fills `report` per interface, forwards by the interface's IN endpoint number, and `main.cpp` passes the mouse interface's IN endpoint where it passed the interface number.
- [ ] **Step 3:** `make test && make` → both pass.
- [ ] **Step 4 [board]:** Owner flashes and runs hardware checks 1 and 2 of `firmware/README.md` with the mouse, plus one human action from the client. Result must match `main`.
- [ ] **Step 5:** Commit `Key the PC-facing endpoints by number`.

---

### Task 4: Kinds and mouse decoding (native)

**Files:**
- Create: `firmware/src/device_kind.h`, `firmware/src/device_kind.cpp`, `firmware/src/kind_mouse.cpp`, `firmware/src/kind_keyboard.cpp`, `firmware/src/kind_gamepad.cpp`
- Modify: `firmware/src/mouse_layout.h`, `firmware/src/mouse_layout.cpp`, `firmware/platformio.ini` (`build_src_filter` gains the five files and `usb_profile.cpp`)
- Test: `firmware/test/test_device_kind/test_device_kind.cpp`, `firmware/test/test_mouse_layout/test_mouse_layout.cpp`

All five new files are pure C++ (no Arduino headers) so they build natively.

**Interfaces — Consumes:** `UsbProfile`, `MouseLayout`, `hideMouseButtons`, `mergeMouse`. **Produces:**

```cpp
// mouse_layout.h
bool decodeMouse(const MouseLayout &layout, const uint8_t *packet, size_t length,
                 uint8_t &buttons, int &x, int &y, int &wheel, int &pan);  // false: not a report of this layout

// device_kind.h
struct DeviceKind {
  const char *name;
  bool (*match)(const UsbProfile &profile, unsigned interface);
  void (*attach)(const UsbProfile &profile, unsigned interface);          // may be null
  bool (*input)(uint8_t *packet, uint8_t length);  // may be null; false drops the packet. Runs in the host interrupt.
  void (*sent)(bool delivered);                    // may be null; outcome of forwarding the packet input() last passed
};
extern const DeviceKind mouseKind, keyboardKind, gamepadKind;
bool firstUsage(const uint8_t *report, size_t length, uint16_t &page, uint16_t &usage);  // first top-level collection
const DeviceKind *kindOf(const UsbProfile &profile, unsigned interface);  // gamepad, keyboard, mouse; else null
const char *kindName(const UsbProfile &profile);  // kind of the lowest-numbered matching interface, else "other"

// kind_mouse.cpp state, declared in device_kind.h
struct InjectedMouse { int x, y; uint8_t buttons, sentButtons; bool merged; };   // moved from mouse_capture.h
struct PhysicalMouse { uint8_t buttons; int wheel; bool changed; };              // decoded from forwarded reports
extern MouseLayout mouseLayout;               // bytes == 0 when no mouse kind is attached
extern volatile bool mouseFrozen;
extern volatile uint8_t hiddenMouseButtons;
extern InjectedMouse injected;
extern PhysicalMouse physicalMouse;           // wheel accumulates until the reader clears it
```

Match rules: gamepad = class `FF`/`5D`/`01`, or class 3 with first usage `1/4` or `1/5`; keyboard = class 3 `1/1`, or first usage `1/6`; mouse = class 3 and `readMouseLayout` accepts the report. `mouseKind.attach` stores the layout. `mouseKind.input`: returns false while `mouseFrozen`; otherwise decodes into `physicalMouse` (before hiding), hides `hiddenMouseButtons`, merges `injected`, and remembers what it took; `sent(true)` subtracts it and sets `sentButtons`/`merged`, `sent(false)` forgets it. This is the logic of `forwardInput` in `mouse_capture.cpp` today.

- [ ] **Step 1: Failing tests.**
  - `decode_roundtrip`: for the layouts already used in `test_mouse_layout`, `encodeMouse(buttons=5, x=-3, y=100, wheel=-1, pan=0)` then `decodeMouse` returns the same values; wrong report ID → false; `length` shorter than `layout.bytes` → false.
  - `pad_kinds`: with the profile from `Pad…` and the two report descriptors below filled into interfaces 1 and 2: `kindOf(0) == &gamepadKind`, `kindOf(1) == &keyboardKind`, `kindOf(2) == nullptr`, `kindName == "gamepad"`; after attaching kinds, `mouseLayout.bytes == 0`.
    - interface 1 report: `05010906a101850175019508050719e029e715002501810295017508810395057501050819012905910295017503910395067508150026ff000507190029ff8100c0050c0901a10185021500263c0419002a3c04751095018100c005010902a10185030901a1000509190129031500250175019503810275059501810105010930093116008026ff7f75109502810609381581257f750895018106050c0a380295018106c0c0`
    - interface 2 report: `067aff0901a10185020902a1000903150025ff7508953f8102858109059102c0c0`
  - `xbox360_kinds`: `kindOf(0) == &gamepadKind`, others null, `kindName == "gamepad"`.
  - `mouse_kinds`: mouse fixture → `&mouseKind`, `kindName == "mouse"`, `mouseLayout.bytes != 0` after attach.
  - `hid_gamepad`: a one-interface class 3 `0/0` profile with report `05010905a101c0` → `&gamepadKind`.
  - `other`: a one-interface class `FF/00/00` profile → `kindName == "other"`.
  - `mouse_input`: after attach on the mouse fixture, with `hiddenMouseButtons = 8|16`, an encoded report with buttons `1|8` passes through `input` with button 8 cleared and `physicalMouse.buttons == 9`; with `injected.x = 5`, `input` then `sent(true)` leaves `injected.x == 0`, and `input` then `sent(false)` leaves it 5; with `mouseFrozen`, `input` returns false.
- [ ] **Step 2:** `make test` → new tests fail.
- [ ] **Step 3:** Implement.
- [ ] **Step 4:** `make test && make` → pass (the new files are not yet called on the board).
- [ ] **Step 5:** Commit `Add device kinds and raw mouse report decoding`.

---

### Task 5: Raw capture driver [board]

**Files:**
- Create: `firmware/src/usb_capture.h`, `firmware/src/usb_capture.cpp` (replace `mouse_capture.*`, deleted)
- Modify: `firmware/src/main.cpp`

**Interfaces — Consumes:** Tasks 2–4. **Produces:**

```cpp
void beginUsbHost();
void taskUsbHost();
bool probeDevice(UsbProfile &profile);   // true: cloned; kinds attached. Waits up to 2500 ms for a device.
bool deviceAttached();                   // the cloned device is still on J6
extern WiredMouse mouse;                 // generic mode only: MouseController over USBHIDParser, as today
```

- [ ] **Step 1:** `RawDevice : USBDriver`, constructed before the hub, HID parsers and `mouse`. `claim()` accepts only when `type == 0`, capture is enabled, the device class is not 9, and `validUsbConfiguration` passes on the descriptors it is given (rebuild the 9-byte header check as needed: `claim` receives the configuration after its header). It then creates one pipe per endpoint and queues an IN transfer on each IN pipe. Contribute enough `Pipe_t`/`Transfer_t` for 14 endpoints.
- [ ] **Step 2:** `probeDevice`: wait for the claim; read device, full configuration, strings (as `captureStrings` does today, plus each interface's string) and each HID interface's report descriptor (`0x81, 6, 0x2200, interface number`); `readUsbProfile`; attach `kindOf` for every interface. No claim within 2500 ms → capture disabled, return false (generic). A read failing after the claim → write a flag in a register that survives a soft reset and restart; at boot the flag is read and cleared first, and when set, capture stays disabled for that boot. Do not use `SRC_GPR5`: the core's CrashReport owns it.
- [ ] **Step 3:** On each IN completion: length via `completedPacketLength`; run the interface's kind `input` if any; `sendUsbPacket(endpoint, …, wait = false)`; call `sent`; re-queue. `forwardUsbOutput` queues the packet on the matching OUT pipe. `disconnect()` marks the device gone; nothing is forwarded afterwards.
- [ ] **Step 4:** `main.cpp`: use `probeDevice`/`startUsbDevice`. In copied mode with `mouseLayout.bytes`, read buttons and wheel from `physicalMouse` (interrupts off, then clear `wheel`/`changed`) where it read `mouse.getButtons()`/`getWheel()`; injection sends on the mouse interface's IN endpoint. In copied mode without a mouse kind, `forwardPhysicalMouse`, `advanceAction` and the `Move`/`Wheel`/`Click`/human commands do nothing. Generic mode is unchanged.
- [ ] **Step 5:** `make test && make` → pass.
- [ ] **Step 6 [board]:** Owner runs, and results are written down in the commit message:
  - mouse: hardware checks 1–4, human action, side-button mode and `read` events;
  - boot with the PC cable unplugged, then plug it: mouse works;
  - 8BitDo on a cable: `profile` (still `copied`), sticks and buttons in a gamepad tester;
  - a keyboard: typing works (LEDs are expected not to work yet);
  - force the restart flag once (temporary build): one restart, generic mode, mouse works, no loop.
- [ ] **Step 7:** Commit `Capture any interrupt-only device with a raw host driver`.

---

### Task 6: Control forwarding [board]

**Files:**
- Create: `firmware/scripts/usb_core_control.py`, `firmware/scripts/usbhost_control_length.py`, `firmware/src/usb_control.h`, `firmware/src/usb_control.cpp`
- Modify: `firmware/platformio.ini` (`extra_scripts`), `firmware/src/usb_capture.cpp`, `firmware/src/main.cpp`

Build this from the spike notes (`2026-11-26-control-spike-notes.md`); where they differ from the interface below, the notes win.

**Interfaces — Produces:**

```cpp
// usb_capture.h
// Starts a control transfer to the cloned device. done(length) runs from the main loop:
// bytes received (or sent), or -1 for a stall or a 500 ms timeout. False if one is in flight or no device.
bool hostControl(uint64_t setup, uint8_t *data, void (*done)(int length));

// usb_control.h
void taskUsbControl();   // main loop: runs pending forwards, completes EP0 stages, handles bus reset
```

- [ ] **Step 1:** The two patch scripts, with the anchors recorded by the spike.
- [ ] **Step 2:** `usb_control.cpp`: the SETUP hook takes, in copied mode only, every request whose type is class or vendor, and every standard `GET_DESCRIPTOR` missing from the descriptor list. IN: forward, then `usb_proxy_transmit` the bytes received, or stall. OUT with data: receive up to 512 bytes, then forward (the PC has already been acknowledged). No-data: forward, then acknowledge or stall. A new SETUP replaces the pending one. Device gone → stall.
- [ ] **Step 3:** On a PC bus reset, send `SET_PROTOCOL(report)` (`0x21, 0x0B, wValue 1, wIndex interface`) to each class-3 interface of the device.
- [ ] **Step 4:** `make test && make` → pass.
- [ ] **Step 5 [board]:** Owner checks: keyboard Caps/Num Lock LEDs follow the PC; keyboard usable in BIOS/UEFI and still correct after the OS boots; the four spike requests against the mouse; unplug the device during a `GET_REPORT` loop → PC gets errors, board keeps answering `profile`.
- [ ] **Step 6:** Commit `Forward control requests to the cloned device`.

---

### Task 7: Protocol reply, client, docs, final checks [board]

**Files:**
- Modify: `firmware/src/main.cpp`, `client/mitusb.py:82-92`, `client/README.md`, `firmware/README.md`, `README.md`, `firmware/test/fixtures.h`

- [ ] **Step 1:** `profile` replies `copied <kindName>\n` or `generic\n`. The client accepts `generic` and `copied` followed by one word of lowercase letters, and prints it; anything else is still "Unexpected profile reply.".
- [ ] **Step 2:** Update the three READMEs: what a device kind is, the clone limits, the new `profile` replies, control forwarding and its two accepted gaps (stalls on OUT-data requests, no wake from sleep), the new source layout table, and the hardware checks for keyboard and gamepad from the spec's Testing section. The top-level README stops calling the project only a mouse bridge.
- [ ] **Step 3 [board]:** Owner runs the full hardware checks: mouse; keyboard; wired Xbox 360 pad (recognised as an Xbox 360 controller, all inputs, rumble, ring LED); 8BitDo on cable and on its dongle. Record outcomes in `firmware/README.md` under the checks, including any device that falls back to generic.
- [ ] **Step 4:** Replace the documented `Xbox360Configuration` fixture with the bytes read from the owner's pad (pyusb on the laptop, as was done for the 8BitDo) and drop the "from public documentation" comment. `make test` → pass.
- [ ] **Step 5:** `make test && make` → pass. Commit `Report the device kind and document keyboard and gamepad support`.
