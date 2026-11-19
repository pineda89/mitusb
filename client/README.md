# mitusb client

The client uses the Python 3.7+ standard library. Look up the `mitusb` DHCP lease on your router and substitute its IP address in the examples. Run the commands from the `client/` directory.

```sh
python3 mitusb.py --host 192.168.1.50 profile
python3 mitusb.py --host 192.168.1.50 read
python3 mitusb.py --host 192.168.1.50 move 100 -20
python3 mitusb.py --host 192.168.1.50 wheel -3
python3 mitusb.py --host 192.168.1.50 click left
```

`profile` prints the USB profile selected at startup, either `copied` or `generic`. `read` prints physical events until you press Ctrl+C. The board supports one subscriber at a time. Starting another reader can replace the current subscriber.

The firmware accepts values from −32767 to 32767 for each axis and the wheel. The client sends movement, wheel, and click commands without waiting for acknowledgement. If the mouse format lacks the requested field, the command has no effect.

```text
dx=   10 dy=   -3 wheel=  0 buttons=-
dx=    0 dy=    0 wheel=  1 buttons=left
```

[Connection, compatibility, and hardware checks](../firmware/README.md)
