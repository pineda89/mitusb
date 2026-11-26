from pathlib import Path

Import("env")

# Lets usb_control.cpp take control requests the core would answer itself or stall.
HOOKS = """
__attribute__((weak)) int usb_proxy_setup(uint64_t setup, int unhandled) { return 0; }
__attribute__((weak)) int usb_proxy_complete(void) { return 0; }
__attribute__((weak)) void usb_proxy_reset(void) {}
"""
WRAPPERS = """
void usb_proxy_transmit(const void *data, uint32_t length) { endpoint0_transmit(data, length, 0); }
void usb_proxy_receive(void *data, uint32_t length, int notify) { endpoint0_receive(data, length, notify); }
void usb_proxy_stall(void) { USB1_ENDPTCTRL0 = 0x00010001; }
"""


def patch_core(target, source, env):
    code = Path(source[0].get_abspath()).read_text(encoding="utf-8")
    edits = (
        ("static void endpoint0_complete(void);\n", "static void endpoint0_complete(void);\n" + HOOKS),
        ("static void endpoint0_setup(uint64_t setupdata)\n{\n",
         "static void endpoint0_setup(uint64_t setupdata)\n{\n"
         "\tif (usb_proxy_setup(setupdata, 0)) return;\n"),
        ('\tprintf("endpoint 0 stall\\n");\n',
         '\tif (usb_proxy_setup(setupdata, 1)) return;\n\tprintf("endpoint 0 stall\\n");\n'),
        ("static void endpoint0_complete(void)\n{\n",
         "static void endpoint0_complete(void)\n{\n\tif (usb_proxy_complete()) return;\n"),
        ("\tif (status & USB_USBSTS_URI) { // page 3164\n",
         "\tif (status & USB_USBSTS_URI) { // page 3164\n\t\tusb_proxy_reset();\n"),
    )
    for original, replacement in edits:
        if code.count(original) != 1:
            raise ValueError("Teensy core usb.c changed; review the control request hooks")
        code = code.replace(original, replacement)
    Path(target[0].get_abspath()).write_text(code + WRAPPERS, encoding="utf-8")


def build_core(env, node):
    patched = env.Command(
        "$BUILD_DIR/usbcore/usb.c",
        [node.srcnode(), "$PROJECT_DIR/scripts/usb_core_control.py"],
        patch_core,
    )
    return env.Object(patched)


env.AddBuildMiddleware(build_core, "*/cores/teensy4/usb.c")
