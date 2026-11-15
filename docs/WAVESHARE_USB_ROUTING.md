# Waveshare ESP32-P4-Module-DEV-KIT USB routing

Date: 2026-11-15

## Board and selector

- Board: Waveshare ESP32-P4-Module-DEV-KIT, SKU 30560.
- U14: CH334F USB hub.
- U15: FSUSB42UMX USB 2:1 analog mux.
- H3: USB Host/Device selector; it controls the U15 select input.
- J8: USBHSD1 connector, with USBHSD1_N and USBHSD1_P signals.

The schematic truth table was previously interpreted as a definitive mapping of the complete USB-A port topology. That interpretation is now disputed by physical testing and must not be used as the sole basis for selecting H3.

| H3 position | Observed behavior on the assembled board |
|---|---|
| Device | The Host stack reports activity on downstream Port1, Port2 and Port3. Port4 is inactive. Port1/Port2 currently fail during the initial device-descriptor read; see the test log below. |
| Host | In the earlier test, USB3 successfully enumerated the USB stick, while USB1 and USB2 did not successfully enumerate it. |

The exact electrical route from H3/U15 to the hub and connectors remains to be reconciled against the complete schematic nets and the physical board. Until that reconciliation is complete, do not describe either H3 label as a verified routing truth for USB1–USB3.

This corrects earlier repository descriptions that referred to a USB4 Type-A socket or an unverified direct Type-A route. There is no normal USB4 receptacle: USB4 is only exposed as a test point (TP). Do not describe USB4 as a user-connectable port.

## H3 mode investigation — physical evidence (2026-11-15)

The earlier recommendation to use H3=Host for USB1–USB3 was based on an incomplete interpretation of the schematic and is withdrawn pending net-level verification.

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

The earlier H3=Host test remains a separate observation: USB3 enumerated VID:PID 8564:1000, while USB1 and USB2 did not successfully enumerate.

Do not change firmware routing or claim a final H3-to-port mapping yet. The next step is to trace the actual U15 common/select pins and both switched signal pairs through the schematic to U14/J8, then compare that with continuity measurements on the unpowered board.

## Test procedure

1. Record the H3 position; test both positions separately while the routing discrepancy remains unresolved.
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
- USB4 is a test point only, not a normal Type-A receptacle.

## Sources

- Waveshare ESP32-P4-Module-DEV-KIT schematic, SKU 30560.
- Waveshare ESP32-P4-Module-DEV-KIT schematic, SKU 30560.
- Physical H3 mode observations supplied by the user (2026-11-15); these currently conflict with the earlier schematic interpretation.
- [Waveshare board documentation](https://docs.waveshare.com/ESP32-P4-Module-DEV-KIT)
- [ESP32-P4 USB Host documentation](https://docs.espressif.com/projects/esp-usb/en/latest/esp32p4/usb_host.html)
