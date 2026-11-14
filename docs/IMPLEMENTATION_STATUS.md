# Firmware implementation status

Date: 2026-11-14

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

The source is aligned with the public esp_tinyusb 3.x configuration model, but it has not yet been built with the project's exact ESP-IDF 6.1 environment. A successful compile is still required before hardware testing.

### Physical USB PHY mapping

Type-C 6 / USB1P1 is wired to GPIO24/GPIO25, corresponding to FS PHY1. The default ESP32-P4 mapping assigns FS PHY1 to USB Serial/JTAG and FS PHY2 (GPIO27/GPIO26) to USB OTG FS. The `USB_PHY_SEL` eFuse swaps those assignments. The requested Type-C 6 TinyUSB path therefore depends on the current eFuse state and the resulting PHY mapping.

No eFuse has been programmed as part of this work. The exact current value must be read and reviewed before any irreversible change. Type-C 4 / CH343P is a separate UART bridge and remains the intended programming/logging connection.

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

- Verify U15 channel-to-signal mapping for both H3 positions on the exact board.
- Read and review the current `USB_PHY_SEL` eFuse value.
- Verify that Type-C 6 exposes USB OTG FS on GPIO24/GPIO25 after the PHY assignment is confirmed.
- Build the isolated HID mouse test mode with ESP-IDF 6.1.
- Verify Windows enumeration and the four HID mouse reports on the physical board.
- Verify simultaneous USB 2.0 HS Host on Type-A and USB 1.1 FS Device on Type-C 6 after the isolated Device test passes.
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

## Build and verification

The USB Device proof of concept and USB Host enumeration diagnostic remain unbuilt/unverified in the current environment. The project owner must run the build and hardware steps in the relevant test documents using ESP-IDF 6.1 and the Waveshare ESP32-P4-Module-DEV-KIT.

Do not mark the USB Device milestone complete until the firmware builds, the correct PHY is assigned to the Type-C 6 connector, Windows enumerates the HID mouse after reconnects, and Type-C 4 / CH343P remains available for programming and logs.

No end-to-end USB Host-to-Device passthrough or G HUB compatibility is claimed.
