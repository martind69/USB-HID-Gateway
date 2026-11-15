# Firmware implementation status

Date: 2026-11-15

## USB Host enumeration diagnostic

Files:
- `components/usb_gateway/usb_host.c`
- `components/usb_gateway/idf_component.yml`
- `sdkconfig.defaults`
- `docs/USB_HOST_ENUMERATION_TEST.md`

Implemented at source level:
- Added managed dependency `espressif/usb ~1.6.0` for ESP-IDF 6.1.
- Installed the ESP-IDF USB Host Library on the USB 2.0 High-Speed OTG peripheral using `peripheral_map = BIT0`.
- Configured the HS controller for FS/LS-only operation to accommodate Full-Speed peripherals behind High-Speed hubs without Transaction Translator support.
- Added the Host Library daemon task and asynchronous client task.
- Enabled single- and multi-level external hub support in `sdkconfig.defaults`.
- Opened enumerated devices and logged VID, PID, speed and active configuration interface count.
- Tracked device disconnects and updated the diagnostics registry.

This is an enumeration-only diagnostic. It does not claim HID interfaces, retrieve HID report descriptors, submit interrupt-IN transfers or forward reports. The source has not yet been built or tested on the physical board.

### USB Host enumeration test preparation (2026-11-15)

- Added startup instructions for USB Host enumeration; the H3-to-port route is now explicitly marked as under investigation.
- Enabled USB Host client notifications for devices removed without an opened client handle.
- Added removal-event logging and cleanup of pending, unopened device slots.
- Set the standalone HID mouse test option to disabled in `sdkconfig.defaults`, so a fresh/default build starts the USB Host path.
- Expanded the hardware procedure with the active build configuration, build/flash commands and per-port USB-stick test steps.

The source has not yet been compiled in the user's ESP-IDF environment after these changes, and no physical USB-A enumeration test has been performed. Existing build directories retain their own `sdkconfig`; the mouse-test option must be disabled in the active build configuration before testing.

### Board routing clarified (2026-11-15)

The previous interpretation of the U15/H3 schematic truth table as proving the complete USB1–USB3 routing was incorrect or incomplete and is withdrawn pending net-level verification.

Physical observations (2026-11-15):
- H3=Host (corrected user report): USB4 successfully enumerated the test stick; USB1, USB2 and USB3 did not successfully enumerate. The earlier USB3 success entry was incorrect.
- H3=Device: the Host stack reported activity on downstream Port1, Port2 and Port3; Port4 remained inactive.
- In H3=Device mode, Port1 and Port2 failed at the short Device Descriptor control transfer (`CHECK_SHORT_DEV_DESC FAILED`) and were disabled while still connected. This is not successful enumeration.
- USB4 successfully enumerated a USB stick in the corrected H3=Host test. Its physical connector designation must be checked against the exact board revision; the earlier test-point-only assumption is withdrawn.

The exact H3/U15-to-U14/J8 signal routing must be checked against the complete schematic nets and physical continuity before declaring either H3 position the correct USB1–USB3 Host setting. See `docs/WAVESHARE_USB_ROUTING.md` and `docs/USB_HOST_ENUMERATION_TEST.md`.

## PC-facing USB HID Device proof of concept

Files:
- `components/usb_gateway/usb_device.c`
- `components/usb_gateway/idf_component.yml`
- `components/usb_gateway/Kconfig`
- `docs/USB_DEVICE_POC_TEST.md`
- `docs/USB_DEVICE_MOUSE_TEST.md`

Implemented:
- Added Espressif `esp_tinyusb ~3.0.0` managed dependency.
- Initialized the ESP32-P4 USB Device stack through TinyUSB.
- Explicitly selected `TINYUSB_PORT_FULL_SPEED_0`.
- Kept the normal composite keyboard/mouse descriptor as the default mode.
- Added an optional isolated HID mouse test mode with a mouse-only report descriptor and a finite four-report movement/click sequence.
- Disabled USB Host initialization while the isolated mouse test mode is selected.
- Added HID report descriptor and class request callbacks.
- Added mount/detach state reporting to runtime diagnostics.
- Added runtime ESP32-P4 PHY selection with `usb_serial_jtag_ll_phy_select(1)` before TinyUSB installation. This maps USB Serial/JTAG to PHY1 (GPIO26/GPIO27) and USB OTG to PHY0 (GPIO24/GPIO25), matching Type-C 6 / USB1P1.
- Corrected the TinyUSB HID report submission call to the esp_tinyusb/TinyUSB API signature: `tud_hid_report(report_id, report, len)`.
- Declared the `esp_hal_usb` component dependency for the PHY-selection HAL header.

The standalone HID mouse mode and TinyUSB stack have compiled in the user's ESP-IDF 6.1 environment. Physical HID enumeration and report delivery are not yet verified.

### Physical USB PHY mapping

Type-C 6 / USB1P1 is wired to GPIO24/GPIO25. The firmware now uses the ESP-IDF runtime PHY selector before TinyUSB initialization:

```c
usb_serial_jtag_ll_phy_select(1);
```

The ESP-IDF implementation enables software PHY mapping and selects USB Serial/JTAG on PHY1 and USB OTG (USB_WRAP) on PHY0. On this board, that routes USB OTG to GPIO24/GPIO25 and USB Serial/JTAG to GPIO26/GPIO27. This is a runtime register setting; it does not program the irreversible `USB_PHY_SEL` eFuse.

Type-C 4 / CH343P remains the separate UART bridge and is the intended programming/logging connection.

See `docs/USB_DEVICE_MOUSE_TEST.md` for the isolated test procedure and expected Windows behavior.

## Field-level mapping and proxy routing interface

Files:
- `components/usb_gateway/mapping.h`
- `components/usb_gateway/mapping.c`
- `components/usb_gateway/hid_proxy.h`
- `components/usb_gateway/hid_proxy.c`

Implemented:
- Bounded mapping-rule snapshot (24 Usage Page/Usage rules).
- Report-field metadata contract with bit offset, width, neutral value, decoded value and Variable/Array classification.
- Byte-preserving Passthrough path.
- Proxy path that neutralizes only configured fields and preserves all other bits.
- Mapped Variable fields are consumed in both active and neutral/release states.
- Empty Array slots (Usage 0) are not treated as active mappings.
- Complete metadata validation before modifying output.
- Fail-open forwarding of the original report if metadata is invalid or the report exceeds the fixed scratch-buffer capacity.
- Synchronous semantic event callback for mapped fields.
- Per-report copy of the mapping configuration so a callback cannot change the rules halfway through one report.

A host test source is present at `tests/host/test_mapping.c`, but it is not yet wired into CMake/CI.

## Existing diagnostics

The diagnostics component provides a five-second serial summary, bounded host-device registry, proxy counters, I2C health hooks, heap and uptime reporting. Host enumeration and I2C health remain unavailable until their drivers are implemented and connected.

## Integration still pending

- Verify that Type-C 6 exposes USB OTG FS on GPIO24/GPIO25 after the runtime PHY selection.
- Verify that Type-C 4 / CH343P remains available for programming and logs after PHY selection.
- Verify simultaneous USB 2.0 HS Host on USB1–USB3 via U14/CH334F and USB 1.1 FS Device on Type-C 6 after the isolated Device test passes.
- Reconcile H3/U15 signal routing against the full schematic and continuity measurements; then test Host enumeration independently on USB1, USB2 and USB3 in the verified selector position.
- HID Host interface discovery/claiming, report descriptor retrieval and interrupt-IN report reception.
- Retrieval and storage of raw HID report descriptors.
- Generic HID 1.1 parser and parser-populated mapping metadata.
- USB Device report submission from the proxy router.
- USB Device Output/Feature and vendor control-transfer handling.
- JSON loading and atomic runtime configuration replacement.
- WASD vector calculation, mouse shaping and digital action state machines.
- G1 event decoding and G1 LED feedback.
- Keyboard Array rollover and mixed-report reconstruction tests.
- ADC/DAC/GPIO hardware drivers and electrical validation.
- End-to-end passthrough and G HUB compatibility tests.
- USB MSC Host + Device bridge and SCSI/Bulk-Only Transport forwarding for USB-stick passthrough.

## Build and verification

The user has confirmed a successful ESP-IDF 6.1 build with standalone mouse test mode enabled. The runtime PHY selection and corrected HID report API are committed to the project branch. Physical USB Device enumeration and HID report delivery are verified on Type-C 6 / USB1P1. Windows identifies VID/PID 303A:4004 as a HID-compliant mouse and loads hidusb.sys/mouhid.sys. A sequential diagonal drag test moved the cursor and held/released the left button as expected; the initial negative X movement was not observed, likely because the first report was sent before Windows finished bringing the mouse online. The remaining sequence executed correctly.

No end-to-end USB Host-to-Device passthrough, USB MSC bridge or G HUB compatibility is claimed.
