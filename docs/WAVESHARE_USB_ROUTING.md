# Waveshare ESP32-P4-Module-DEV-KIT USB routing

Date: 2026-11-15

## Board and selector

- Board: Waveshare ESP32-P4-Module-DEV-KIT, SKU 30560.
- U14: CH334F USB hub.
- U15: FSUSB42UMX USB 2:1 analog mux.
- H3: USB Host/Device selector; it controls the U15 select input.
- J8: USBHSD1 connector, with USBHSD1_N and USBHSD1_P signals.

The board schematic's U15 truth table and the user's confirmation establish the routing:

| H3 position | U15 signal destination | Result |
|---|---|---|
| Host | U14, CH334F USB hub | USB DM/DP are routed to the hub IC. The USB1–USB3 downstream ports are available on the host-side hub path. |
| Device | J8, USBHSD1_N / USBHSD1_P | USB DM/DP are routed directly to the USBHSD1 connector. |

This corrects earlier repository descriptions that referred to a USB4 Type-A socket or an unverified direct Type-A route. There is no normal USB4 receptacle: USB4 is only exposed as a test point (TP). Do not describe USB4 as a user-connectable port.

## Intended gateway configuration

For the USB HID Gateway's peripheral-facing host connection, set H3 to **Host**:

ESP32-P4 USB Host signal path -> U15 (FSUSB42UMX, Host selection) -> U14 (CH334F hub) -> USB1 / USB2 / USB3.

In this configuration, J8 / USBHSD1 is not selected by U15 and is not part of the active USB Host path. The J8 connector is the alternate Device-mode branch.

The firmware's USB Host controller must be connected to the signal input side of U15 as shown in the board schematic. The board's H3 label defines the mux routing mode; it does not by itself prove that firmware has successfully initialized the USB Host controller.

## Test procedure

1. Set H3 to Host mode.
2. Connect the board to the PC through Type-C 4 / CH343P for programming and UART logs.
3. Connect one known-good USB Full-Speed peripheral or USB stick to USB1.
4. Check the USB Host log for device attach, VID/PID, negotiated speed and configuration interface count.
5. Repeat independently on USB2 and USB3.
6. If a port fails, record the H3 position, port number, VBUS measurement and complete UART log.
7. Do not use J8 as a substitute for USB1–USB3 during this Host-mode test; J8 belongs to the alternate Device-mode branch.

The current host diagnostic uses `fsls_only = true`. It is intended to enumerate Full-/Low-Speed devices; this does not yet provide USB mass-storage passthrough.

## Important distinction: enumeration versus mass-storage passthrough

The current USB Host implementation enumerates devices and logs descriptors. A USB stick appearing in the enumeration log proves only that the host stack detected and enumerated it. It does not mean the stick is mounted or forwarded to the PC.

A functional USB mass-storage bridge still requires:
- USB Host MSC class support and SCSI/Bulk-Only Transport handling.
- A PC-facing USB Device MSC interface.
- Forwarding of SCSI commands and associated data between the Host and Device sides.
- Capacity, block-size, reset, timeout, disconnect and error handling.

## Other USB connections

- Type-C 4 uses the CH343P USB-to-UART bridge and is the programming/logging connection.
- Type-C 6 / USB1P1 is a separate ESP32-P4 FS USB connection wired to GPIO24/GPIO25; its PHY assignment is controlled independently in firmware.
- J8 is the USBHSD1 connector selected in H3 Device mode.
- USB4 is a test point only, not a normal Type-A receptacle.

## Sources

- Waveshare ESP32-P4-Module-DEV-KIT schematic, SKU 30560.
- User-confirmed H3 Host/Device truth-table interpretation (2026-11-15).
- [Waveshare board documentation](https://docs.waveshare.com/ESP32-P4-Module-DEV-KIT)
- [ESP32-P4 USB Host documentation](https://docs.espressif.com/projects/esp-usb/en/latest/esp32p4/usb_host.html)
