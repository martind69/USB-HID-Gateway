# Waveshare ESP32-P4-Module-DEV-KIT USB routing assessment

Date: 2026-11-13

## Board identification

- Board: Waveshare ESP32-P4-Module-DEV-KIT
- SKU: 30560
- Type-C 4: CH343P USB-to-UART bridge
- Type-C 6: USB connector routed through analog switch U15 (FSUSB42)
- H3: hardware selector connected to U15 SEL through net USB_SEL

The exact PCB revision is not marked on the user's board. The uploaded SKU 30560 schematic is the basis for the electrical statements below.

## H3 and U15: corrected electrical relationship

The previous repository text incorrectly stated that H3 only selects the Type-A routing and is unrelated to U15. This is false.

The schematic directly connects H3 to U15 pin 10 (SEL) through net USB_SEL. U15 is an FSUSB42 analog USB 2:1 switch. The user has clarified the switched signal groups: H3/U15 selects between USB1–USB3 through the onboard CH334F hub and USB4 as a direct Type-A route. The exact jumper-to-route assignment remains to be recorded. Its select truth table is printed in the schematic:

- SEL = LOW selects channel 1.
- SEL = HIGH selects channel 2.

H3 is a three-pin header:
- Pin 1 is connected to GND through R46 (0 ohm).
- Pins 2 and 3 are tied together on USB_SEL.
- R37 (47 kohm) pulls USB_SEL HIGH.
- A jumper between pin 1 and pin 2 (or pin 1 and pin 3) forces USB_SEL LOW.
- With no jumper between pin 1 and the tied selector pins, USB_SEL is pulled HIGH.
- A jumper only between pins 2 and 3 changes nothing because those pins are already connected.

The HOST/DEVICE labels refer to the Type-A routing selection, not to the USB role of Type-C 6. H3/U15 switches the Type-A route between the three-port CH334F hub branch and the direct USB4 branch.

## Observed Windows result

The user supplied a USB Device Tree Viewer result for the H3 HOST position (jumper pin 1-2). Type-C 6 enumerated as:

- Espressif USB Serial/JTAG
- VID:PID `303A:1001`
- Windows COM port: COM3

This is a USB Device enumeration, not evidence that Type-C 6 is operating as a USB Host. It establishes that, in the tested HOST jumper position, the PC-facing connector presented the ESP32-P4 USB Serial/JTAG function.

Do not overwrite this observation with the assumption that the HOST marking means Type-C 6 must expose the application USB Host controller. Type-C 4 remains the independent CH343P UART bridge (VID:PID `1A86:55D3`, previously COM5).

The result for the other H3 position must be recorded separately if/when measured. Do not infer its Windows identity or USB role from the HOST capture.

## Physical connectors

| Board reference | Physical interface | Verified / documented function |
|---|---|---|
| 4 | Type-C UART | CH343P USB-to-UART bridge; WCH VID:PID `1A86:55D3`; previously COM5. |
| 6 | Type-C USB1.1 | USB Serial/JTAG was observed by Windows as `303A:1001`, COM3. This result is not evidence about the H3-switched Type-A routing. |
| 12 | Four Type-A sockets | H3/U15 switches between USB1–USB3 through CH334F hub U14 and USB4 as the direct route. Exact jumper-to-route assignment remains to be recorded. |
| 13 | H3 jumper | Three-pin selector electrically connected to U15 SEL. Pin 1 is GND; pins 2 and 3 are tied to USB_SEL. |

## Controller assignment: intended architecture, not yet board-verified

ESP32-P4 provides separate USB controllers, and the project intends to use independent paths:

- USB 2.0 High-Speed OTG Host -> peripheral-facing connection for G815/G502.
- USB 1.1 Full-Speed OTG Device -> PC-facing HID connection.
- Type-C 4 / CH343P -> programming and UART logs.

The ESP-IDF USB documentation describes independent HS and FS OTG controllers. However, the exact board-level routing through U15 and H3 must be reconciled with the measured connector behavior before treating the intended Type-C 6 FS Device path as established.

The schematic labels U15's signal paths as `USB_N/P` and `USBHSD1_N/P`, with the switch selection shown as channel 1 for LOW and channel 2 for HIGH. These net names and the Windows HOST-position capture must be used together; do not assign HOST/DEVICE roles to the mux channels based only on their names.

## Firmware implications

The TinyUSB PoC is configured for `TINYUSB_PORT_FULL_SPEED_0` for the separate PC-facing FS OTG Device path. H3/U15 selects the Type-A Host routing and does not select the Type-C 6 USB role.

A successful TinyUSB initialization only proves that the stack initialized. It does not prove that Type-C 6 is physically connected to the FS OTG signals. Windows must enumerate the configured TinyUSB VID/PID rather than `303A:1001` before the PC-facing HID path is considered validated.

## Required validation sequence

1. Keep Type-C 4 connected for CH343P UART logs and flashing.
2. Record H3 position unambiguously: jumper absent or jumper between pins 1-2 (pins 2 and 3 are electrically common).
3. For each H3 state, connect Type-C 6 to the PC and save the complete USB Device Tree Viewer result, including VID:PID, interfaces and COM-port assignment.
4. Compare each result with the U15 channel truth table and the complete schematic net connections. Do not infer the USB role from the HOST/DEVICE silkscreen alone.
5. Verify which H3 state routes Type-C 6 to the ESP32-P4 USB1.1 FS OTG controller.
6. Only then test TinyUSB on `TINYUSB_PORT_FULL_SPEED_0`; expected Windows enumeration is the configured HID Gateway identity, not `303A:1001`.
7. Separately verify HS Host enumeration of the G815/G502 through the Type-A group.
8. Verify simultaneous HS Host and FS Device operation before marking the board topology milestone complete.

## Current status

- [x] SKU 30560 schematic reviewed for U15 and H3 connectivity.
- [x] H3 is confirmed connected to U15 SEL; prior documentation claiming otherwise is withdrawn.
- [x] H3 electrical logic established: pin 1-2 forces SEL LOW/channel 1; open selector defaults HIGH/channel 2.
- [x] HOST-position Windows capture recorded: Type-C 6 enumerates as Espressif USB Serial/JTAG `303A:1001`, COM3.
- [ ] Windows result for the other H3 position recorded and compared.
- [ ] Exact mapping of U15 channel 1/channel 2 to the USB1–USB3 hub branch versus direct USB4 branch recorded for both H3 states.
- [ ] TinyUSB HID enumeration on Type-C 6 verified.
- [ ] Simultaneous HS Host + FS Device operation verified.

## Sources

- User-provided Waveshare ESP32-P4-Module-DEV-KIT schematic for SKU 30560.
- Waveshare board documentation: https://docs.waveshare.com/ESP32-P4-Module-DEV-KIT
- Waveshare FAQ: https://docs.waveshare.com/ESP32-P4-Module-DEV-KIT/FAQ
- ESP32-P4 USB Device Stack: https://docs.espressif.com/projects/esp-usb/en/latest/esp32p4/usb_device.html
- ESP32-P4 USB Host: https://docs.espressif.com/projects/esp-usb/en/latest/esp32p4/usb_host.html
