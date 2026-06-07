# HD44780 Character LCD Linux Driver

Linux kernel character device driver for HD44780-compatible LCD modules.

The module exposes LCDs as `/dev/hd44780_*` character devices. User space writes
text bytes to the device; the driver translates them into HD44780 command/data
cycles. The implementation supports:

- `demo` backend for driver API testing without hardware.
- `parport` backend using a legacy DB-25/LPT I/O base address.
- `usb` backend for vendor-specific USB-to-LCD bridge firmware.

## Build

```bash
make
```

Clean build output:

```bash
make clean
```

## Load Demo Backend

```bash
sudo insmod src/hd44780_lcd.ko enable_demo=1 enable_parport=0
echo "Linux LCD driver" | sudo tee /dev/hd44780_demo0
sudo rmmod hd44780_lcd
```

## Load DB-25 Parallel-Port Backend

Typical PC LPT1 base address is `0x378`.

```bash
sudo insmod src/hd44780_lcd.ko enable_demo=0 enable_parport=1 parport_base=0x378
echo "DB25 HD44780" | sudo tee /dev/hd44780_lpt0
```

See [docs/WIRING.md](docs/WIRING.md) before connecting hardware.

## Load USB Bridge Backend

HD44780 LCDs are not USB devices. The USB backend expects a microcontroller or
USB-to-GPIO bridge that implements the control-request protocol described in
[docs/USB_BRIDGE_PROTOCOL.md](docs/USB_BRIDGE_PROTOCOL.md).

```bash
sudo insmod src/hd44780_lcd.ko enable_demo=0 usb_vid=0x1209 usb_pid=0x4470
```

## User API

```bash
printf "hello\nworld" > /dev/hd44780_demo0
printf "\fclear and home" > /dev/hd44780_demo0
```

The driver treats:

- `\f` as clear display.
- `\n` as move to second line.
- ordinary printable bytes as LCD data.

## Repository Contents

```text
src/
|-- Makefile
|-- hd44780_lcd.c
`-- hd44780_lcd.h
docs/
|-- WIRING.md
`-- USB_BRIDGE_PROTOCOL.md
```
