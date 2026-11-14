# Standalone USB Device HID mouse test

## Purpose

This test isolates the ESP32-P4 Full-Speed USB Device path from the USB Host stack and from HID proxy processing. It exposes one standard HID mouse interface and sends four reports after the PC enumerates the device:

1. Move right by 24 counts.
2. Move left by 24 counts.
3. Press the left mouse button.
4. Release the left mouse button.

The cursor should return to its starting position and one click should be generated. No keyboard interface is exposed in this test mode.

## Build configuration

Enable the Kconfig option:

```
USB HID Gateway
  [*] Standalone USB Device HID mouse test mode
```

Then run:

```powershell
idf.py reconfigure
idf.py build
```

The option is disabled by default. With the option enabled, the application does not start the USB Host stack. This prevents the Host test from interfering with the isolated Device test.

Disable the option again to restore the normal composite keyboard/mouse descriptor and USB Host initialization.

## Physical connection

- Keep Type-C 4 / CH343P connected to the development PC for UART logs and flashing.
- Connect Type-C 6 to the PC as the USB Device under test.
- H3/U15 only changes the Type-A routing and has no electrical effect on Type-C 6 / USB1P1.

Type-C 6 is wired to GPIO24/GPIO25. On ESP32-P4 those pins are FS PHY1. The default PHY assignment connects FS PHY1 to USB Serial/JTAG and FS PHY2 (GPIO27/GPIO26) to USB OTG FS. The PHY assignment must therefore be checked before expecting TinyUSB to appear on Type-C 6.

## PHY/eFuse constraint

The ESP32-P4 eFuse `USB_PHY_SEL` changes the default assignment:

- Value 0: FS PHY1 -> USB Serial/JTAG; FS PHY2 -> USB OTG FS.
- Value 1: FS PHY2 -> USB Serial/JTAG; FS PHY1 -> USB OTG FS.

The desired Type-C 6 mapping (GPIO24/GPIO25 -> USB OTG FS) requires the swapped assignment. Do not burn this eFuse until the current eFuse summary and the consequences for boot/download behavior have been reviewed. The eFuse is irreversible.

The CH343P on Type-C 4 remains a separate UART bridge and is the preferred programming/logging connection for this test.

## Expected logs and Windows result

Expected firmware logs:

- `Standalone HID mouse test mode enabled`
- `HID device stack ready; waiting for PC enumeration`
- `PC enumerated USB HID Gateway`
- `Mouse test report 1 sent` through `Mouse test report 4 sent`

Windows should enumerate a HID-compliant mouse interface. The device must not enumerate as Espressif USB Serial/JTAG (`303A:1001`) on Type-C 6 after the PHY has been correctly assigned to USB OTG FS.

## Acceptance criteria

- [ ] ESP-IDF 6.1 build succeeds with test mode enabled.
- [ ] Type-C 4 / CH343P remains available for logs and flashing.
- [ ] Type-C 6 enumerates as a HID mouse, not USB Serial/JTAG.
- [ ] Windows receives the four test reports.
- [ ] Cursor moves right and returns left.
- [ ] One left click is generated and then released.
- [ ] Reconnect repeats the test sequence.
- [ ] No USB Host initialization occurs in this test mode.

## Limits

This test proves only the PC-facing Full-Speed USB Device path and basic HID report delivery. It does not prove simultaneous USB Host operation, HID forwarding, Logitech vendor-control compatibility, or the final composite descriptor.
