# Waveshare ESP32-P4-Module-DEV-KIT USB routing assessment

Date: 2026-11-13

## Corrected conclusion

The earlier conclusion that Type-C connector 6 is only USB Serial/JTAG was incorrect. The Waveshare schematic identifies this connector as **USB1.1 Type-C** and shows an FSUSB42 analog USB switch (U15) between the connector and two signal paths. One path is the fixed USB Serial/JTAG pair (`USB_DBG_D_P/N`); the other is the ESP32-P4 USB 1.1 OTG path (`USBHSD1_P/N`). The selector net is named `USB_SEL`.

Therefore, the Windows report `303A:1001` on connector 6 means that the switch was routing the connector to USB Serial/JTAG at the time of that capture. It does not prove that connector 6 cannot carry TinyUSB HID. The USB mux selection and the selected TinyUSB controller must be checked together.

## Physical connectors

| Board reference | Physical interface | Function |
|---|---|---|
| 4 | Type-C UART | CH343P USB-to-UART bridge; WCH VID:PID `1A86:55D3`; use for flashing and UART logs. |
| 6 | Type-C USB1.1 | FSUSB42 mux (U15) selects between USB Serial/JTAG and the P4 USB 1.1 Full-Speed OTG path. This is the intended candidate PC-facing HID connector when the mux selects OTG. |
| 12 | Four Type-A sockets | P4 USB 2.0 High-Speed OTG path. One socket is directly connected; three are behind CH334F hub U14. |
| 13 | H3 jumper | Selects the Type-A routing. Waveshare FAQ: HOST enables Type-A port 1; DEVICE enables Type-A ports 2–4 through the CH334 hub. This jumper is not a software USB role setting. |

Sources:
- Waveshare hardware description: https://docs.waveshare.com/ESP32-P4-Module-DEV-KIT
- Waveshare FAQ: https://docs.waveshare.com/ESP32-P4-Module-DEV-KIT/FAQ
- Waveshare schematic, especially U14 (CH334F), U15 (FSUSB42), H1/H2 and H3: https://files.waveshare.com/wiki/ESP32-P4-Module-DEV-KIT/ESP32-P4-Module-DEV-KIT.pdf
- ESP32-P4 USB Device Stack: https://docs.espressif.com/projects/esp-usb/en/latest/esp32p4/usb_device.html
- ESP32-P4 USB Host: https://docs.espressif.com/projects/esp-usb/en/latest/esp32p4/usb_host.html

## ESP32-P4 controller assignment

ESP32-P4 provides two independent OTG controllers:
- USB 2.0 High-Speed OTG: dedicated USB_DM/USB_DP pins; routed by this board to the Type-A group. Use this controller for peripheral-facing USB Host.
- USB 1.1 Full-Speed OTG: GPIO26/GPIO27; routed through U15 to Type-C connector 6. Use this controller for PC-facing TinyUSB HID Device.
- USB Serial/JTAG: separate fixed-function debug controller; it can also be routed to Type-C 6 by U15. It is not a HID-capable application controller.
- Type-C connector 4 remains the CH343P UART connection and is independent of the above.

This gives the intended physical architecture without a USB role conflict:
- Type-C 6 / USB FS OTG / TinyUSB Device -> PC (keyboard + mouse HID)
- Type-A 12 / USB HS OTG / USB Host -> G815/G502, with H3 selecting the required Type-A/hub path
- Type-C 4 / CH343P UART -> development PC for logs and flashing

The ESP-IDF USB Host documentation describes the HS and FS OTG controllers as independently operable. The project still needs an on-board simultaneous Host + Device integration test; this is not yet claimed as verified.

## Firmware correction

The TinyUSB PoC must select `TINYUSB_PORT_FULL_SPEED_0`, not `TINYUSB_PORT_HIGH_SPEED_0`, because connector 6's OTG signal path is USB 1.1 FS. The Type-A connector group is the separate HS OTG path and is reserved for USB Host.

Do not configure the HID PoC on the HS controller while expecting it to enumerate through Type-C 6. A successful `tinyusb_driver_install()` only confirms stack initialization; PC enumeration must still be verified.

## USB switch U15 and H3: do not conflate them

U15 is an FSUSB42 analog USB switch. Its `SEL` input is the schematic net `USB_SEL`; it chooses whether Type-C connector 6 is connected to USB Serial/JTAG or to the USB1.1 OTG signal pair. The exact source/strap/logic level for `USB_SEL` must be confirmed from the fitted board revision and schematic connectivity before assigning a GPIO or recommending a jumper change.

H3 is documented by Waveshare as the Type-A routing selector for the direct Type-A socket versus the CH334 hub ports. Do not assume H3 controls U15 unless the exact schematic/revision confirms that electrical connection.

The user supplied USB Device Tree Viewer captures for H3 HOST and DEVICE both showed:
- Type-C 4: CH343P (`1A86:55D3`)
- Type-C 6: Espressif USB Serial/JTAG (`303A:1001`)

These captures establish that the Type-C 6 switch was still presenting USB Serial/JTAG in both captured states. They do not establish that the FS OTG mux path was selected. The next hardware test must verify U15's selected path, then enumerate the FS TinyUSB device.

## Required validation sequence

1. Keep Type-C 4 connected for CH343P UART logs and flashing.
2. Connect Type-C 6 to the PC.
3. Build and flash firmware with TinyUSB set to `TINYUSB_PORT_FULL_SPEED_0`.
4. Confirm from schematic/revision or a safe continuity/logic-level measurement that U15 selects the P4 USB1.1 OTG pair, not `USB_DBG_D_P/N`. Do not short or move unidentified pads while powered.
5. Check Windows Device Manager / USB Device Tree Viewer. Expected: TinyUSB HID Gateway (VID/PID from the configured device descriptor), not `303A:1001`.
6. Separately connect the G815/G502 to Type-A 12 with H3 set to the routing documented for the CH334 hub path. Verify that the HS USB Host can enumerate them while the FS HID Device remains connected to the PC.
7. Confirm both buses work concurrently before marking the USB topology milestone complete.

## Current status

- The board contains the required two OTG controllers and routes them to separate physical paths (HS to Type-A group, FS via U15 to Type-C 6).
- Type-C 6 was observed as USB Serial/JTAG because U15 selected the debug path; the capture does not prove the OTG path is unavailable.
- The TinyUSB PoC previously selected the wrong controller for Type-C 6. This is corrected to Full-Speed OTG in firmware.
- U15 mux selection is not yet validated on the user's exact board revision.
- Simultaneous HS Host + FS Device operation is architecturally supported by separate controllers but remains an integration test.
- No Windows HID enumeration or end-to-end forwarding is claimed yet.
