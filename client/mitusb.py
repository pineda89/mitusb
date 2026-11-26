#!/usr/bin/env python3
"""mitusb client: read the mouse's buttons and wheel and inject moves/clicks over UDP.

  mitusb.py read
  mitusb.py move 100 -20
  mitusb.py wheel -3
  mitusb.py click left
  mitusb.py human 400 -120 --click left --then 15 -4 --ms 80
"""
import argparse
import math
import random
import re
import socket
import struct

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
        _, buttons, wheel = data.decode().split()
        held = [name for bit, name in BUTTONS.items() if int(buttons) & bit]
        print(f"wheel={wheel:>3} buttons={','.join(held) or '-'}", flush=True)


def min_ms(dx, dy, curvature):
    """Shortest duration the firmware accepts for a segment (100 counts per ms at peak)."""
    return max(2, math.ceil(1.875 * math.hypot(dx, dy) * math.hypot(1, curvature / 128) / 100))


def human_packet(dx, dy, button, then=(0, 0), ms=None):
    """Curved move to exactly (dx, dy), optional click there, then an optional extra move."""
    curvature = round(random.uniform(-0.06, 0.06) * 512)
    if ms is None:  # Fitts' law, as in the old firmware's test client
        ms = round((80 + 110 * math.log2(math.hypot(dx, dy) / 30 + 1)) * random.uniform(0.5, 0.75))
    if (dx or dy) and ms < min_ms(dx, dy, curvature):
        raise ValueError(f"--ms must be at least {min_ms(dx, dy, 31)} for this distance")
    return struct.pack(
        ">hhHbBBBBHhh", dx, dy, ms, curvature,
        random.randint(8, 30),                        # ms settled before pressing
        button,
        random.randint(25, 70) if button else 0,      # ms held
        3,                                            # action packet marker
        max(random.randint(120, 220), min_ms(*then, curvature)),  # extra move ms
        *then)


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--host", default="mitusb.local", help="board name or IP (default: %(default)s)")
    p.add_argument("--port", type=int, default=4242)
    sub = p.add_subparsers(dest="cmd", required=True)
    sub.add_parser("profile", help="show the USB descriptor profile selected at boot")
    sub.add_parser("read", help="print button and wheel events until Ctrl+C")
    m = sub.add_parser("move", help="relative move")
    m.add_argument("dx", type=int)
    m.add_argument("dy", type=int)
    sub.add_parser("wheel", help="wheel steps").add_argument("n", type=int)
    sub.add_parser("click", help="click a button").add_argument("button", choices=["left", "right", "middle"])
    h = sub.add_parser("human", help="human-like move to dx dy, optional click there, optional extra move")
    h.add_argument("dx", type=int)
    h.add_argument("dy", type=int)
    h.add_argument("--click", choices=["left", "right", "middle"], help="click exactly at dx dy")
    h.add_argument("--then", nargs=2, type=int, default=(0, 0), metavar=("DX", "DY"),
                   help="keep moving this much after the click")
    h.add_argument("--ms", type=int, help="duration of the move to dx dy (default: human-like)")
    a = p.parse_args()

    try:
        addr = (socket.gethostbyname(a.host), a.port)  # resolve once, not per datagram
    except socket.gaierror:
        p.exit(1, f"Cannot find {a.host} on the network. Pass the board's IP with --host.\n")
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    if a.cmd == "profile":
        sock.settimeout(2)
        sock.connect(addr)
        sock.send(b"profile")
        try:
            profile = sock.recv(64).strip()
        except socket.timeout:
            p.exit(1, "No profile reply from the board.\n")
        if not re.fullmatch(rb"generic|copied [a-z]+", profile):
            p.exit(1, "Unexpected profile reply.\n")
        print(profile.decode("ascii"))
        return
    if a.cmd == "human":
        if max(map(abs, (a.dx, a.dy, *a.then))) > 5000 or (a.ms is not None and not 2 <= a.ms <= 65535):
            p.error("distances must be within ±5000 and --ms within 2..65535")
        button = {None: 0, "left": 1, "right": 2, "middle": 4}[a.click]
        try:
            packet = human_packet(a.dx, a.dy, button, a.then, a.ms)
        except ValueError as e:
            p.error(str(e))
        sock.settimeout(2)
        sock.sendto(packet, addr)
        try:
            sock.recvfrom(64)
        except socket.timeout:
            p.exit(1, "The board did not accept the action.\n")
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
