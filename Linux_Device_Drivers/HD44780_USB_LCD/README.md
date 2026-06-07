# HD44780 USB LCD Linux Driver

USB-first Linux kernel character device driver for HD44780-compatible LCD
modules connected through a microcontroller USB bridge.

This project is separate from the DB-25/LPT LCD driver. It models the practical
USB setup: a small USB device drives LCD pins D4-D7, RS, and E, while Linux talks
to the bridge through vendor-specific USB control transfers.

## Why A USB Bridge

HD44780 LCD modules are parallel-bus devices, not USB devices. A USB printer
adapter normally exposes printer-class behavior and does not provide direct
GPIO timing for LCD nibbles. This driver expects bridge firmware that implements
the protocol in [docs/BRIDGE_PROTOCOL.md](docs/BRIDGE_PROTOCOL.md).

## Build

```bash
make
```

Clean:

```bash
make clean
```

## Load

Replace VID/PID with the IDs used by the bridge firmware.

```bash
sudo insmod src/hd44780_usb_lcd.ko vid=0x1209 pid=0x4470
echo "USB LCD ready" | sudo tee /dev/hd44780_usb0
sudo rmmod hd44780_usb_lcd
```

## Character Device Behavior

- `write(2)` sends printable bytes as LCD data.
- `\n` moves to the second display line.
- `\f` clears the LCD and returns home.
- `ioctl(2)` supports clear, home, display on, and display off.

## Files

```text
src/
|-- Makefile
|-- hd44780_usb_lcd.c
`-- hd44780_usb_lcd.h
docs/
|-- BRIDGE_PROTOCOL.md
`-- TESTING.md
```
