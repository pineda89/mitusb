from pathlib import Path

Import("env")


def patch_enumeration(target, source, env):
    code = Path(source[0].get_abspath()).read_text(encoding="utf-8")
    # Preserve the length validated at header receipt across transfers and retries.
    edits = (
        ("\t\tenumlen = enumbuf[2] | (enumbuf[3] << 8);\n"
         "\t\t// TODO: is fetching enumlen this way really safe?\n", ""),
        ("if (enumlen > sizeof(enumbuf)) {\n"
         "\t\t\tenumlen = sizeof(enumbuf);\n"
         "\t\t\t// TODO: how to handle device with too much config data",
         "if (enumlen < 9 || enumlen > sizeof(enumbuf)) {\n"
         "\t\t\tUSBHost::enumeration_busy = false;\n"
         "\t\t\treturn;"),
    )
    for original, replacement in edits:
        if code.count(original) != 1:
            raise ValueError("USBHost_t36 enumeration changed; review the configuration length guard")
        code = code.replace(original, replacement)
    Path(target[0].get_abspath()).write_text(code, encoding="utf-8")


def build_enumeration(env, node):
    patched = env.Command(
        "$BUILD_DIR/usbhost/enumeration.cpp",
        [node.srcnode(), "$PROJECT_DIR/scripts/usbhost_enumeration.py"],
        patch_enumeration,
    )
    return env.Object(patched)


env.AddBuildMiddleware(build_enumeration, "*/USBHost_t36/enumeration.cpp")
