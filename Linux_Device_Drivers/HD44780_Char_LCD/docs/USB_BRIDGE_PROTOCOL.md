# USB Bridge Protocol

HD44780 modules do not implement USB. The USB backend is intended for a
microcontroller bridge such as an AVR, STM32, RP2040, or similar USB device that
drives the LCD pins.

## Device Match

The module matches devices using runtime parameters:

```bash
sudo insmod src/hd44780_lcd.ko usb_vid=0x1209 usb_pid=0x4470
```

## Control Transfer

The driver sends one USB vendor control request per 4-bit LCD transfer.

| Field | Value |
| --- | --- |
| request type | vendor, host-to-device, device recipient |
| request | `0x01` |
| value bits 0..3 | LCD nibble |
| value bit 8 | RS flag, `0` command and `1` data |
| index | `0` |
| data stage | none |

Bridge firmware should latch the nibble onto LCD D4-D7, set RS, pulse E, and
return success.

## Why This Is Not a Generic USB Printer Adapter Driver

Most USB-to-parallel printer adapters expose printer-class behavior, not direct
GPIO pin control. They generally cannot pulse arbitrary DB-25 pins in the exact
timing required by an HD44780 module. This driver therefore models a purposeful
USB LCD bridge rather than assuming every USB adapter is electrically suitable.
