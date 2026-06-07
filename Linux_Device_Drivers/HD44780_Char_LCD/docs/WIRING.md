# DB-25 Parallel-Port Wiring

The parallel-port backend assumes a 4-bit HD44780 connection.

## Suggested Mapping

| LCD Pin | Function | DB-25 Data Bit | Driver Parameter |
| --- | --- | --- | --- |
| D4 | data bit 4 | D0 | fixed |
| D5 | data bit 5 | D1 | fixed |
| D6 | data bit 6 | D2 | fixed |
| D7 | data bit 7 | D3 | fixed |
| RS | register select | D4 | `parport_rs_bit=4` |
| E | enable strobe | D5 | `parport_e_bit=5` |
| RW | read/write | GND | write-only mode |
| VSS | ground | GND | required |
| VDD | +5 V | external +5 V | required |
| VO | contrast | potentiometer wiper | required |

Use a current-limited supply and level compatibility appropriate for the LCD
module. Many modern PCs do not expose a real parallel port; USB printer adapters
usually do not expose GPIO-style pin control and normally cannot drive this
mapping directly.

## Load Example

```bash
sudo insmod src/hd44780_lcd.ko enable_demo=0 enable_parport=1 parport_base=0x378
echo "parallel lcd" | sudo tee /dev/hd44780_lpt0
```

If the port is already owned by another driver, unload or disable that driver
before requesting the I/O region.
