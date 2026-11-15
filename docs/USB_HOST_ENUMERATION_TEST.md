# USB Host enumeration test firmware

## Scope

This milestone installs the ESP-IDF USB Host Library on the ESP32-P4 High-Speed OTG controller (`peripheral_map = BIT0`) and reports enumerated USB devices. It is an enumeration/descriptor diagnostic, not yet a HID class driver or a gateway data path.

The firmware:
- Installs the USB Host Library.
- Runs the required library daemon and client event tasks.
- Enables external hub support, including multi-level hubs.
- Opens each enumerated device and logs VID, PID, bus speed and active configuration interface count.
- Tracks disconnects and updates runtime diagnostics.
- Does not claim HID interfaces or submit interrupt transfers.
- Does not forward reports to the PC-facing TinyUSB device.
- Does not implement USB Mass Storage Class passthrough.

## Board routing required for this test

Set H3 to **Host** mode. According to the Waveshare schematic truth table, H3 controls U15 (FSUSB42UMX):

- H3 Host: U15 routes USB DM/DP to U14, the CH334F hub. This is the required route for USB1, USB2 and USB3.
- H3 Device: U15 routes USB DM/DP to J8 (USBHSD1_N / USBHSD1_P). This is not the required route for the USB-A hub-port test.

USB4 is only a test point (TP), not a normal USB-A receptacle. Do not follow earlier instructions referring to a direct USB4 Type-A socket.

## Speed mode and hub limitation

The host controller is explicitly selected with `peripheral_map = BIT0` (USB 2.0 High-Speed OTG on ESP32-P4) and configured with `fsls_only = true`. The target Logitech G815/G502 devices were observed as Full-Speed devices. Espressif documents that the external hub driver currently has no Transaction Translator layer, so Full-/Low-Speed devices behind a High-Speed hub are not supported in normal High-Speed mode. The HS controller must therefore be operated as an FS/LS-only host for this test.

This sacrifices High-Speed operation on the host bus during this diagnostic milestone. Do not interpret this as the final throughput configuration.

## Build

Use the ESP-IDF version selected by the repository setup (ESP-IDF 6.1):

```powershell
idf.py set-target esp32p4
idf.py reconfigure
idf.py build
```

The managed USB Host dependency is pinned to `espressif/usb ~1.6.0`; this release provides the `fsls_only` host configuration used here.

Flash using the CH343P UART connector (Type-C 4), not the Type-C 6 USB Serial/JTAG connection:

```powershell
idf.py -p COMx flash monitor
```

## Hardware test

1. Connect Type-C 4 to the PC for flashing and serial output.
2. Set H3 to **Host** mode.
3. Connect one known-good USB Full-Speed device or USB stick to USB1.
4. Confirm the log reports a new device address, VID/PID, speed and configuration interface count.
5. Repeat independently with the same device on USB2 and USB3.
6. Connect the G815 and G502 through the intended USB1–USB3 hub path and record both VID/PID values and interface counts.
7. Record the exact H3 state and the physical port used with every log.

Expected Logitech identities from the existing captures:
- G815 DE: `046D:C33F`
- G502 HERO: `046D:C08B`

J8 / USBHSD1 is the alternate U15 Device-mode branch and is not used in this Host-mode test. USB4 is a test point, not a socket.

## Power

The USB-A host ports must provide valid 5 V VBUS to the attached device/hub. If the board does not enable host VBUS automatically, the board's VBUS power-switch circuit must be enabled according to the Waveshare schematic. Do not externally inject 5 V into a port that is already powered.

## Acceptance criteria

- [ ] Firmware builds with the selected ESP-IDF version.
- [ ] Host stack starts without an installation error.
- [ ] USB1 enumerates a directly connected Full-Speed device through U14/CH334F.
- [ ] USB2 enumerates the same device.
- [ ] USB3 enumerates the same device.
- [ ] Disconnect is detected and the device is closed cleanly.
- [ ] G815 and G502 enumerate through the actual USB1–USB3 hub topology.
- [ ] PC-facing TinyUSB Device remains operational at the same time.

## Not implemented in this milestone

- HID interface discovery and claiming.
- HID report descriptor retrieval.
- Interrupt-IN report transfers.
- Generic HID parsing from live devices.
- Host-to-Device report forwarding.
- USB vendor/control-transfer proxying or G HUB compatibility.
- USB MSC bridge, SCSI command forwarding or block-level passthrough.

## References

- [Waveshare USB routing assessment](WAVESHARE_USB_ROUTING.md)
- [ESP-IDF ESP32-P4 USB Host documentation](https://docs.espressif.com/projects/esp-usb/en/latest/esp32p4/usb_host.html)
- [Espressif USB Host Library example](https://github.com/espressif/esp-idf/tree/master/examples/peripherals/usb/host/usb_host_lib)
- [ESP Component Registry: espressif/usb 1.6.0](https://components.espressif.com/components/espressif/usb/versions/1.6.0)
