# PC-facing USB HID proof of concept

## Scope

The first USB Device milestone exposes one HID interface with two top-level
collections: a keyboard and a mouse. It does not generate synthetic key or
mouse input. The test is limited to USB enumeration and descriptor retrieval.

The device stack uses Espressif's `esp_tinyusb` component (TinyUSB-based).
On this Waveshare board, the Type-C UART/programming connector uses the onboard CH343P USB-to-UART bridge. Its COM port is separate from the PC-facing OTG HID connection.

## Board setup

Target board: Waveshare ESP32-P4-Module-DEV-KIT.

- Keep the Type-C UART/programming connection attached for flashing and logs.
- Connect the board's USB OTG connection to the PC using the board's documented
  OTG path.
- Set jumper H3 to DEVICE before connecting the OTG cable.
- Do not connect a USB peripheral to the OTG connector while it is configured
  as a PC-facing device.
- The Type-A OTG connector is switched between HOST and DEVICE by the board
  jumper arrangement. Verify the silkscreen position against the board
  revision before applying power.

## Build and flash

From an ESP-IDF 6.1 PowerShell environment:

```powershell
idf.py reconfigure
idf.py build
idf.py -p COMx flash
idf.py -p COMx monitor
```

Use the COM port belonging to the Type-C UART/programming connection. Do not
assume that the PC-facing OTG HID connection will create a COM port.

If the HID class is not enabled in the existing local sdkconfig, open `idf.py menuconfig` and set Component config → TinyUSB Stack → Human Interface Device Class → TinyUSB HID interfaces count to `1`. The repository default is `CONFIG_TINYUSB_HID_COUNT=1`.

## Expected result

After the PC-facing OTG connection is attached, Windows should enumerate
`USB HID Gateway` as a HID device. The HID report descriptor contains
keyboard and mouse top-level collections. No key presses or mouse movement
should occur because this proof of concept deliberately sends no input reports.

The serial log should report:

- TinyUSB device stack installation.
- HID device stack ready.
- PC enumeration when the host configures the device.
- Detach when the OTG cable is removed.

## Acceptance criteria

- [ ] Firmware builds with ESP-IDF 6.1.
- [ ] Device stack starts without errors.
- [ ] Windows enumerates the device consistently after reconnect.
- [ ] Windows identifies keyboard and mouse HID collections.
- [ ] USB descriptor retrieval succeeds in USB Device Tree Viewer or USBView.
- [ ] The separate programming COM connection remains usable.
- [ ] No synthetic key or mouse input is generated.

## Not covered by this proof of concept

- USB Host initialization or downstream hub enumeration.
- Forwarding reports from the Logitech G815/G502.
- Vendor-specific Logitech control transfers or G HUB compatibility.
- Composite descriptor identity strategy for the final gateway.
- Concurrent Host and Device operation on the selected board.
