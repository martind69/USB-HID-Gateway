# PC-facing USB HID proof of concept

## Scope

The first USB Device milestone exposes one HID interface with two top-level collections: a keyboard and a mouse. It does not generate synthetic key or mouse input. The test validates USB enumeration and descriptor retrieval only.

## Board connections and routing

Target: Waveshare ESP32-P4-Module-DEV-KIT, SKU 30560.

- Type-C 4: CH343P UART bridge. Keep this connected for logs and flashing.
- Type-C 6: USB1.1 Type-C connector routed through FSUSB42 analog switch U15.
- Type-A 12: USB 2.0 High-Speed OTG peripheral connection group.
- H3: three-pin selector connected to U15 SEL. It is not merely a Type-A selector.

The schematic shows H3 pin 1 connected to GND and pins 2/3 tied to USB_SEL, with a 47 kohm pull-up. Therefore:
- Jumper pin 1-2 (or 1-3): SEL LOW, U15 channel 1.
- Jumper absent: SEL HIGH, U15 channel 2.
- Jumper pin 2-3 alone has no effect.

The HOST label on the H3 position does not mean Type-C 6 is enumerating as a USB Host. In the user's recorded HOST-position test (jumper pin 1-2), Type-C 6 appeared in Windows as Espressif USB Serial/JTAG `303A:1001`, COM3. This is a USB Device enumeration. The other H3 position has not yet been established by a corresponding saved Windows result.

Do not assume which U15 channel connects the FS OTG controller solely from the HOST/DEVICE label. Confirm the complete schematic net mapping and the actual Windows result for both H3 positions before changing the hardware setup.

## Intended application paths

- Type-C 6 / USB Full-Speed OTG / TinyUSB Device -> PC.
- Type-A 12 / USB High-Speed OTG / USB Host -> keyboard and mouse.
- Type-C 4 / CH343P UART -> development PC for logs and flashing.

These are the intended project roles. The board-level mux state and simultaneous operation still require hardware validation.

## Build and flash

From an ESP-IDF 6.1 PowerShell environment:

```powershell
idf.py reconfigure
idf.py build
idf.py -p COMx flash
idf.py -p COMx monitor
```

Use the COM port belonging to Type-C 4 / CH343P, not COM3 from Type-C 6 / USB Serial/JTAG.

## Expected result

The serial log should report:
- TinyUSB device stack installation on USB Full-Speed OTG.
- HID device stack ready.
- PC enumeration only when U15 routes Type-C 6 to the ESP32-P4 FS OTG signals.
- Detach when Type-C 6 is removed.

A log line stating `USB DEVICE=ready` proves only successful stack initialization. A `CONNECTED` state and Windows enumeration of the configured HID Gateway identity are required to prove operation. If Windows instead reports `303A:1001`, Type-C 6 is still presenting USB Serial/JTAG.

## Acceptance criteria

- [ ] Firmware builds with ESP-IDF 6.1.
- [ ] TinyUSB explicitly selects `TINYUSB_PORT_FULL_SPEED_0`.
- [ ] H3 state and corresponding U15 channel are recorded.
- [ ] U15 routes Type-C 6 to the USB1.1 OTG path.
- [ ] Windows enumerates the HID device consistently after reconnect.
- [ ] Windows identifies keyboard and mouse HID collections.
- [ ] Type-C 4 / CH343P remains available for logs and flashing.
- [ ] USB HS Host enumerates the G815/G502 through the Type-A path at the same time.
- [ ] No synthetic key or mouse input is generated.

## Not covered

- USB Host hub/device enumeration implementation.
- Forwarding G815/G502 reports.
- Logitech vendor control transfers or G HUB compatibility.
- Final composite descriptor identity strategy.
