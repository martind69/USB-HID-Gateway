# PC-facing USB HID proof of concept

## Scope

The normal USB Device mode exposes one HID interface with two top-level collections: a keyboard and a mouse. It does not generate synthetic key or mouse input. This mode validates USB enumeration and descriptor retrieval only. For an isolated mouse report test, use [USB_DEVICE_MOUSE_TEST.md](USB_DEVICE_MOUSE_TEST.md).

## Board connections and routing

Target: Waveshare ESP32-P4-Module-DEV-KIT, SKU 30560.

- Type-C 4: CH343P UART bridge. Keep this connected for logs and flashing.
- Type-C 6: USB1.1 Type-C connector wired to USB1P1 / GPIO24-GPIO25 (FS PHY1); Windows has observed USB Serial/JTAG (`303A:1001`, COM3) on this connector.
- Type-A 12: four-port USB 2.0 High-Speed group. H3/U15 selects between USB1–USB3 through the onboard CH334F hub and USB4 as the direct route.
- USB1P1: separate direct connection to the ESP32-P4 core at GPIO24/GPIO25; it is not the H3 Type-A selector path.
- H3: three-pin selector connected to U15 SEL for the Type-A hub/direct-port routing.

The schematic shows H3 pin 1 connected to GND and pins 2/3 tied to USB_SEL, with a 47 kohm pull-up. Therefore:
- Jumper pin 1-2 (or 1-3): SEL LOW, U15 channel 1.
- Jumper absent: SEL HIGH, U15 channel 2.
- Jumper pin 2-3 alone has no effect.

The HOST/DEVICE labels describe the Type-A hub/direct-port routing, not the USB role of Type-C 6. The HOST-position Type-C 6 capture (`303A:1001`, COM3) is a separate USB Serial/JTAG observation and says nothing about which Type-A branch U15 selects. Record the exact Type-A socket behavior for both H3 states.

On ESP32-P4, the default FS PHY mapping is FS PHY1 (GPIO24/GPIO25) -> USB Serial/JTAG and FS PHY2 (GPIO27/GPIO26) -> USB OTG FS. `USB_PHY_SEL=1` swaps the assignments so FS PHY1 is connected to USB OTG FS. This is relevant because Type-C 6 is wired to GPIO24/GPIO25. Do not burn this eFuse until its current state and boot/download implications have been reviewed.

## Intended application paths

- Type-C 6 / USB1P1 (GPIO24/GPIO25, FS PHY1) -> intended PC-facing FS OTG Device path. The current USB PHY assignment must be checked: the default mapping assigns FS PHY1 to USB Serial/JTAG, while FS PHY2 (GPIO27/GPIO26) is assigned to USB OTG FS. The `USB_PHY_SEL` eFuse swaps these assignments.
- Type-A 12 / USB High-Speed OTG / USB Host -> selected Type-A route (USB1–USB3 via CH334F hub or direct USB4).
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
- PC enumeration when the separate FS OTG Device signal path is physically connected to the PC; H3/U15 Type-A selection is independent.
- Detach when Type-C 6 is removed.

A log line stating `USB DEVICE=ready` proves only successful stack initialization. A `CONNECTED` state and Windows enumeration of the configured HID Gateway identity are required to prove operation. If Windows instead reports `303A:1001`, Type-C 6 is still presenting USB Serial/JTAG.

## Acceptance criteria

- [ ] Firmware builds with ESP-IDF 6.1.
- [ ] TinyUSB explicitly selects `TINYUSB_PORT_FULL_SPEED_0`.
- [ ] H3 state and corresponding U15 Type-A route are recorded.
- [ ] Current `USB_PHY_SEL` value is read and reviewed; Type-C 6 / GPIO24/GPIO25 is confirmed assigned to FS OTG Device.
- [ ] Windows enumerates the HID device consistently after reconnect.
- [ ] Windows identifies keyboard and mouse HID collections.
- [ ] Type-C 4 / CH343P remains available for logs and flashing.
- [ ] USB HS Host enumerates the G815/G502 through the Type-A path at the same time.
- [ ] Normal composite mode generates no synthetic key or mouse input. The optional isolated mouse test mode is validated separately.

## Not covered

- USB Host hub/device enumeration implementation.
- Forwarding G815/G502 reports.
- Logitech vendor control transfers or G HUB compatibility.
- Final composite descriptor identity strategy.
