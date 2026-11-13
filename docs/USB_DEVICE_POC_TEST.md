# PC-facing USB HID proof of concept

## Scope

The first USB Device milestone exposes one HID interface with two top-level collections: a keyboard and a mouse. It does not generate synthetic key or mouse input. The test is limited to USB enumeration and descriptor retrieval.

The device stack uses Espressif's `esp_tinyusb` component (TinyUSB-based). On ESP32-P4 the PoC explicitly selects the High-Speed OTG port.

## Important board-routing finding

The Waveshare ESP32-P4-Module-DEV-KIT has three distinct physical USB paths:

- Type-C 4: onboard CH343P USB-to-UART bridge (WCH VID:PID `1A86:55D3`).
- Type-C 6: fixed-function ESP32-P4 USB Serial/JTAG (Espressif VID:PID `303A:1001`). It cannot be used as a programmable HID device.
- Four Type-A sockets: the P4 USB OTG path; one socket is direct and three are behind the CH334 hub. H3 selects the Type-A path: HOST enables port 1; DEVICE enables ports 2–4, according to Waveshare's FAQ.

Therefore, do not connect Type-C 6 expecting the TinyUSB HID descriptor. Its appearance as `303A:1001` is expected. The board documentation does not identify a separate, standards-compliant PC-facing OTG Device connector while the Type-A group is being used for peripheral-facing Host operation.

See [Waveshare USB routing assessment](WAVESHARE_USB_ROUTING.md) before performing hardware tests.

## Board setup

Target board: Waveshare ESP32-P4-Module-DEV-KIT.

- Keep Type-C 4 attached for UART logging and flashing.
- Type-C 6 may remain attached for USB Serial/JTAG; it is not the HID test connection.
- H3 selects the Type-A OTG path as described above.
- Do not use a Type-A-to-Type-A cable to connect the Type-A sockets to the PC.
- Do not assume H3 changes the TinyUSB software role or turns Type-C 6 into a HID port.

## Build and flash

From an ESP-IDF 6.1 PowerShell environment:

```powershell
idf.py reconfigure
idf.py build
idf.py -p COMx flash
idf.py -p COMx monitor
```

Use the COM port belonging to the Type-C UART/programming connection.

## Expected software result

The serial log should report:
- TinyUSB device stack installation on the High-Speed OTG port.
- HID device stack ready.
- PC enumeration only after a valid PC-facing OTG Device connection is physically established.
- Detach when that OTG cable is removed.

A log line stating `USB DEVICE=ready` proves only successful stack initialization. A `CONNECTED` state or Windows USB Device Tree Viewer report is required to prove enumeration.

## Acceptance criteria

- [ ] Firmware builds with ESP-IDF 6.1.
- [ ] TinyUSB explicitly selects `TINYUSB_PORT_HIGH_SPEED_0`.
- [ ] A documented, electrically safe PC-facing OTG Device connector is established.
- [ ] Windows enumerates the device consistently after reconnect.
- [ ] Windows identifies keyboard and mouse HID collections.
- [ ] USB descriptor retrieval succeeds in USB Device Tree Viewer or USBView.
- [ ] The separate programming/debugging connection remains usable.
- [ ] No synthetic key or mouse input is generated.

## Not covered by this proof of concept

- USB Host initialization or downstream hub enumeration.
- Forwarding reports from the Logitech G815/G502.
- Vendor-specific Logitech control transfers or G HUB compatibility.
- Composite descriptor identity strategy for the final gateway.
- Concurrent Host and Device operation on the selected board.
