#!/usr/bin/env python3
"""mitusb client: read what the mouse does and inject moves/clicks over UDP.

  mitusb.py --host 192.168.1.50 read
  mitusb.py --host 192.168.1.50 move 100 -20
  mitusb.py --host 192.168.1.50 wheel -3
  mitusb.py --host 192.168.1.50 click left
"""
import argparse
import socket

BUTTONS = {1: "left", 2: "right", 4: "middle", 8: "back", 16: "forward"}


def read(sock, addr):
    # "hello" subscribes us; repeat it while idle in case the board rebooted.
    sock.settimeout(2)
    sock.sendto(b"hello", addr)
    while True:
        try:
            data, _ = sock.recvfrom(64)
        except socket.timeout:
            sock.sendto(b"hello", addr)
            continue
        _, buttons, dx, dy, wheel = data.decode().split()
        held = [name for bit, name in BUTTONS.items() if int(buttons) & bit]
        print(f"dx={dx:>5} dy={dy:>5} wheel={wheel:>3} buttons={','.join(held) or '-'}", flush=True)


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--host", required=True, help="Teensy IP (DHCP hostname: mitusb)")
    p.add_argument("--port", type=int, default=4242)
    sub = p.add_subparsers(dest="cmd", required=True)
    sub.add_parser("profile", help="show the USB descriptor profile selected at boot")
    sub.add_parser("read", help="print mouse events until Ctrl+C")
    m = sub.add_parser("move", help="relative move")
    m.add_argument("dx", type=int)
    m.add_argument("dy", type=int)
    sub.add_parser("wheel", help="wheel steps").add_argument("n", type=int)
    sub.add_parser("click", help="click a button").add_argument("button", choices=["left", "right", "middle"])
    a = p.parse_args()

    addr = (a.host, a.port)
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    if a.cmd == "profile":
        sock.settimeout(2)
        sock.connect(addr)
        sock.send(b"profile")
        try:
            profile = sock.recv(64).strip()
        except socket.timeout:
            p.exit(1, "No profile reply from the board.\n")
        if profile not in (b"copied", b"generic"):
            p.exit(1, "Unexpected profile reply.\n")
        print(profile.decode("ascii"))
        return
    if a.cmd == "read":
        try:
            read(sock, addr)
        except KeyboardInterrupt:
            pass
        return
    line = {"move": lambda: f"move {a.dx} {a.dy}",
            "wheel": lambda: f"wheel {a.n}",
            "click": lambda: f"click {a.button}"}[a.cmd]()
    sock.sendto(line.encode(), addr)


if __name__ == "__main__":
    main()
