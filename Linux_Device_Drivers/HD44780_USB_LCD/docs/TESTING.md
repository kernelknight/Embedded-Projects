# Testing

## Build Test

The repository GitHub Actions workflow builds this module against Ubuntu kernel
headers.

```bash
make -C Linux_Device_Drivers/HD44780_USB_LCD
```

## Runtime Smoke Test

With bridge hardware connected:

```bash
sudo insmod src/hd44780_usb_lcd.ko vid=0x1209 pid=0x4470
dmesg | tail
ls -l /dev/hd44780_usb*
printf "\fhello\nusb lcd" | sudo tee /dev/hd44780_usb0
sudo rmmod hd44780_usb_lcd
```

## Debugging

- `dmesg` should show probe and disconnect messages.
- `lsusb` should show the bridge VID/PID.
- If no `/dev/hd44780_usb0` appears, confirm module parameters match firmware
  IDs.
- If text appears corrupted, inspect bridge nibble mapping and E pulse timing.
