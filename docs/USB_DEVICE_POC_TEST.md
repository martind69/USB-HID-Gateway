# PC-facing USB HID proof of concept

## Scope

The first USB Device milestone exposes one HID interface with two top-level collections: a keyboard and a mouse. It does not generate synthetic key or mouse input. The test validates USB enumeration and descriptor retrieval only.

## Correct port assignment for the Waveshare board

- Type-C 4: CH343P UART bridge. Keep this connected for logs and flashing.
- Type-C 6: USB1.1 Type-C connector. The schematic shows FSUSB42 mux U15 selecting between USB Serial/JTAG and the ESP32-P4 USB 1.1 Full-Speed OTG pair.
- Type-A 12: USB 2.0 High-Speed OTG path. One socket is direct; three sockets are behind CH334F hub U14. H3 selects the Type-A routing.

The two application data paths are intended to be independent:
- Type-C 6 / USB Full-Speed OTG / TinyUSB Device -> PC.
- Type-A 12 / USB High-Speed OTG / USB Host -> keyboard and mouse.

Type-C 6 showing Espressif VID:PID `303A:1001` means U15 is currently routing that connector to USB Serial/JTAG. It does not mean the connector is inherently incapable of TinyUSB HID. U15 must select the USB1.1 OTG signal pair.

See [Waveshare USB routing assessment](WAVESHARE_USB_ROUTING.md).

## Board setup

Target: Waveshare ESP32-P4-Module-DEV-KIT.

- Keep Type-C 4 connected to the PC for UART logs and flashing.
- Connect Type-C 6 to the PC as the HID Device connection.
- Configure TinyUSB to use `TINYUSB_PORT_FULL_SPEED_0`.
- Ensure U15 selects the P4 USB1.1 OTG pair rather than USB Serial/JTAG. Confirm the correct mux selection for the exact board revision before changing any jumper or GPIO.
- Use H3 only for the Type-A routing described in Waveshare's FAQ. For the peripheral-facing Host path, use the Type-A/CH334 configuration appropriate to the installed board revision.
- Do not use a Type-A-to-Type-A cable.

## Build and flash

From an ESP-IDF 6.1 PowerShell environment:

```powershell
idf.py reconfigure
idf.py build
idf.py -p COMx flash
idf.py -p COMx monitor
```

Use the COM port belonging to Type-C 4 / CH343P.

## Expected result

The serial log should report:
- TinyUSB device stack installation on USB Full-Speed OTG.
- HID device stack ready.
- PC enumeration only after U15 routes Type-C 6 to the OTG FS signals.
- Detach when Type-C 6 is removed.

A log line stating `USB DEVICE=ready` proves only successful stack initialization. A `CONNECTED` state and Windows enumeration of the HID collections are required to prove operation.

## Acceptance criteria

- [ ] Firmware builds with ESP-IDF 6.1.
- [ ] TinyUSB explicitly selects `TINYUSB_PORT_FULL_SPEED_0`.
- [ ] U15 selects the USB1.1 OTG path for Type-C 6.
- [ ] Windows enumerates the HID device consistently after reconnect.
- [ ] Windows identifies keyboard and mouse HID collections.
- [ ] USB descriptor retrieval succeeds in USB Device Tree Viewer or USBView.
- [ ] Type-C 4 / CH343P remains available for logs and flashing.
- [ ] USB HS Host enumerates the G815/G502 through the Type-A path at the same time.
- [ ] No synthetic key or mouse input is generated.

## Not covered

- USB Host hub/device enumeration implementation.
- Forwarding G815/G502 reports.
- Logitech vendor control transfers or G HUB compatibility.
- Final composite descriptor identity strategy.
