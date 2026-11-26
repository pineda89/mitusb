#!/usr/bin/env python3
"""Example: when mouse4 (back) is pressed, run a human-like move, and keep listening.

  example_mouse4.py [host]     # host defaults to mitusb.local

One UDP socket does everything: it subscribes, receives events and sends actions.
"""
import socket
import sys

from mitusb import human_packet

MOUSE4 = 8  # button mask: 1 left, 2 right, 4 middle, 8 back (mouse4), 16 forward (mouse5)

board = (socket.gethostbyname(sys.argv[1] if len(sys.argv) > 1 else "mitusb.local"), 4242)
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.settimeout(2)
sock.sendto(b"hello", board)

previous = 0
while True:
    try:
        data, _ = sock.recvfrom(64)
    except socket.timeout:
        sock.sendto(b"hello", board)  # stay subscribed if the board rebooted
        continue

    if data == b"y":  # the board accepted our action
        print("action accepted", flush=True)
        continue
    if not data.startswith(b"mouse "):
        continue

    buttons = int(data.split()[1])
    if buttons & MOUSE4 and not previous & MOUSE4:  # just pressed, not held
        print("mouse4 pressed -> moving", flush=True)
        # Change this line to do something else: move 200 right, 50 up, then click left.
        sock.sendto(human_packet(200, -50, button=1), board)
    previous = buttons
