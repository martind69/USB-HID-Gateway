# Waveshare ESP32-P4-Module-DEV-KIT USB routing

Date: 2026-11-15

## Board and selector

- Board: Waveshare ESP32-P4-Module-DEV-KIT, SKU 30560.
- U14: CH334F USB hub.
- U15: FSUSB42UMX USB 2:1 analog mux.
- H3: USB Host/Device selector; it controls the U15 select input.
- J8: USBHSD1 connector, with USBHSD1_N and USBHSD1_P signals.

Physical tests are the current source of truth for observed port behavior. The user has since confirmed that the H3 selector/jumper setting is resolved; the earlier observations below are retained as test history, not as an open selector investigation. The latest corrected observations were:

| H3 position | Physical connection | Observed behavior |
|---|---|---|
| Host | USB1–USB3 (CH334F downstream ports) | No successful enumeration |
| Host | J8 / USBHSD1 (user called this "USB4") | USB stick successfully enumerates |
| Device | USB1–USB3 / hub ports 1–3 | Hub-port activity observed; enumeration failure reported on ports 1 and 2 |
| Device | Port4 | Inactive |

The user's "USB4" label refers to connector J8 / USBHSD1, not a fourth USB-A port. This supersedes the earlier mistaken report that USB3 enumerated in H3=Host mode. The H3=Host positive control is J8 / USBHSD1.

The user's informal name 'USB4' refers to J8 / USBHSD1. It is not a fourth USB-A downstream port. In H3=Host mode, the USB stick successfully enumerates on J8 / USBHSD1.

## H3 mode investigation — physical evidence (2026-11-15)

The earlier recommendation to use H3=Host for USB1–USB3 was based on an incomplete interpretation of the schematic and is withdrawn. The user has confirmed the selector/jumper setting separately; current work must focus on the failed enumeration transfer in that confirmed setting.

With H3 set to **Device**, the Host stack reports downstream Port1, Port2 and Port3 activity; Port4 remains inactive. The following errors were observed on Port1 and Port2:

```text
E (175400) USBH: Dev 0 EP 0 Error
E (175400) ENUM: Bad transfer status 1: CHECK_SHORT_DEV_DESC
E (175400) ENUM: CHECK_SHORT_DEV_DESC FAILED
W (175400) EXT_PORT: Port1 disabled while connected, reset attempts=1
I (175400) usb_host: Device removed, address=0
E (180990) USBH: Dev 0 EP 0 Error
E (180990) ENUM: Bad transfer status 1: CHECK_SHORT_DEV_DESC
E (180990) ENUM: CHECK_SHORT_DEV_DESC FAILED
W (180990) EXT_PORT: Port2 disabled while connected, reset attempts=1
```

These messages prove that the Host stack sees activity on those hub ports and attempts enumeration. They do **not** prove successful device enumeration: the control transfer for the short Device Descriptor fails, after which the port is disabled while still connected.

The corrected H3=Host observation is that USB4 enumerated VID:PID 8564:1000, while USB1, USB2 and USB3 did not successfully enumerate.

Do not revisit selector position as part of the current failure investigation. The next step is to diagnose the EP0 control-transfer failure (`Bad transfer status 1: CHECK_SHORT_DEV_DESC`) with detailed USB Host/HUB logs in the already confirmed setting.

## Test procedure

1. Keep H3 in the position already confirmed by the user; do not toggle it during enumeration diagnosis.
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
- J8 is the USBHSD1 connector connected to the alternate U15 branch according to the schematic interpretation; its relationship to the observed H3=Device hub-port activity is not yet reconciled.
- J8 / USBHSD1 (called 'USB4' by the user) successfully enumerated a USB stick in H3=Host mode.

## Sources

- Waveshare ESP32-P4-Module-DEV-KIT schematic, SKU 30560.
- Waveshare ESP32-P4-Module-DEV-KIT schematic, SKU 30560.
- Physical H3 mode observations supplied by the user (2026-11-15); the selector/jumper setting has since been confirmed by the user.
- [Waveshare board documentation](https://docs.waveshare.com/ESP32-P4-Module-DEV-KIT)
- [ESP32-P4 USB Host documentation](https://docs.espressif.com/projects/esp-usb/en/latest/esp32p4/usb_host.html)
