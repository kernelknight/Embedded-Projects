# USB Bridge Protocol

The USB bridge is responsible for exact LCD pin timing. The Linux driver sends
high-level 4-bit LCD transfers.

## Vendor Request

The driver sends one control request for each LCD nibble.

| Field | Value |
| --- | --- |
| bmRequestType | `USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE` |
| bRequest | `0x44` |
| wValue bits 0..3 | LCD nibble on D4-D7 |
| wValue bit 8 | RS flag, `0` command and `1` data |
| wIndex | LCD instance, currently `0` |
| data stage | none |

## Firmware Action

For each request, firmware should:

1. Place nibble bits on LCD D4-D7.
2. Drive RS according to `wValue bit 8`.
3. Keep RW low for write-only mode.
4. Pulse E high then low.
5. Return success only after the LCD write cycle is complete.

The Linux driver already waits after commands such as clear and home, but the
bridge should still meet the electrical timing requirements for the LCD module.

## Suggested USB IDs

For personal prototypes, use a VID/PID you are authorized to use. `0x1209` is
commonly used for pid.codes projects after registration.
