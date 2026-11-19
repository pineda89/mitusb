# mitusb

A USB HID proxy built on a Teensy 4.1. The mouse/keyboard plugs into the board,
the board plugs into the PC, and every HID report passes through it: it can be
read on the way and new movements can be injected.

| Folder | Contents |
|---|---|
| [`firmware/`](firmware/) | Teensy 4.1 firmware |
| [`client/`](client/) | Host-side client to inject and read movements |
| [`hardware/`](hardware/) | KiCad project and fabrication files for the carrier PCB |

## License

[MIT](LICENSE), except the third-party KiCad libraries in
`hardware/library/` (see [hardware/README.md](hardware/README.md)).
