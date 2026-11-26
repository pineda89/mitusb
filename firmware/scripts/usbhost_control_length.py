from pathlib import Path

Import("env")


# USBHost_t36 reports wLength when a control transfer completes. Keep the data stage's
# token so the driver can tell how many bytes the device really sent.
def patch_ehci(target, source, env):
    code = Path(source[0].get_abspath()).read_text(encoding="utf-8")
    edits = (
        ("bool USBHost::followup_Transfer(Transfer_t *transfer)\n{\n",
         "extern \"C\" { volatile uint32_t usbhost_data_token; }\n"
         "bool USBHost::followup_Transfer(Transfer_t *transfer)\n{\n"),
        ("\tif ((token & 0xFC) == 0) {\n"
         "\t\t// transfer is no longer active and does not have any error flags\n",
         "\tif ((token & 0xFC) == 0) {\n"
         "\t\tif (!(token & 0x8000)) usbhost_data_token = token;\n"),
    )
    for original, replacement in edits:
        if code.count(original) != 1:
            raise ValueError("USBHost_t36 ehci.cpp changed; review the control length patch")
        code = code.replace(original, replacement)
    Path(target[0].get_abspath()).write_text(code, encoding="utf-8")


def build_ehci(env, node):
    patched = env.Command(
        "$BUILD_DIR/usbhost/ehci.cpp",
        [node.srcnode(), "$PROJECT_DIR/scripts/usbhost_control_length.py"],
        patch_ehci,
    )
    return env.Object(patched)


env.AddBuildMiddleware(build_ehci, "*/USBHost_t36/ehci.cpp")
