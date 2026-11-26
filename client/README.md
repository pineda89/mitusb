# mitusb client

The client uses the Python 3.7+ standard library. The client finds the board by its `mitusb.local` name. If your network does not resolve `.local` names, pass the board's IP address with `--host`, for example `--host 192.168.1.50`. Run the commands from the `client/` directory.

```sh
python3 mitusb.py profile
python3 mitusb.py read
python3 mitusb.py move 100 -20
python3 mitusb.py wheel -3
python3 mitusb.py click left
python3 mitusb.py human 400 -120 --click left --then 15 -4 --ms 80
```

`profile` prints the USB profile selected at startup: `generic`, or `copied` followed by the kind of device that was cloned (`mouse`, `keyboard`, `gamepad` or `other`). The pointer commands below need a mouse: with a cloned keyboard or gamepad they have no effect. `read` prints physical button changes and wheel steps (not movement) while the board's side-button mode is on, until you press Ctrl+C. The board supports one subscriber at a time. Starting another reader can replace the current subscriber.

`human` moves the pointer exactly `dx dy` along a slightly curved path that speeds up and slows down like a hand. `--click` clicks at that point. `--then DX DY` keeps moving that much after the click. `--ms` sets how long the move to `dx dy` takes; without it the client picks a human-like duration from the distance. The firmware limits speed to 100 counts per millisecond at the peak, so 400 counts need at least 8 ms. Curvature, the pause before the click, and the hold time are randomized on each run. The client waits for the board to accept the action.

The firmware accepts values from −32767 to 32767 for each axis and the wheel. The client sends movement, wheel, and click commands without waiting for acknowledgement. If the mouse format lacks the requested field, the command has no effect.

```text
wheel=  0 buttons=left
wheel=  1 buttons=left
wheel=  0 buttons=-
```

[Connection, compatibility, and hardware checks](../firmware/README.md)
