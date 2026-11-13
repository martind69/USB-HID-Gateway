# Firmware implementation status

Date: 2026-11-13

## Implemented in this branch

### PC-facing USB HID enumeration proof of concept

Files:
- `components/usb_gateway/usb_device.c`
- `components/usb_gateway/idf_component.yml`
- `docs/USB_DEVICE_POC_TEST.md`
- `docs/WAVESHARE_USB_ROUTING.md`

Implemented:
- Added Espressif `esp_tinyusb` managed dependency.
- Initialized the ESP32-P4 USB Device stack through TinyUSB.
- Explicitly selected the ESP32-P4 Full-Speed OTG port for the board's Type-C 6 connector.
- Added one HID interface containing keyboard and mouse top-level collections.
- Added the Full-Speed HID configuration descriptor for the Type-C 6 FS OTG path.
- Added HID report descriptor and class request callbacks.
- Added mount/detach state reporting to runtime diagnostics.
- Deliberately sends no synthetic key or mouse input during enumeration testing.

This is a source-level proof of concept only. It has not yet been built or verified on hardware. The supplied Windows reports identify Type-C 4 as the CH343P UART bridge (`1A86:55D3`) and Type-C 6 as USB Serial/JTAG (`303A:1001`). The Waveshare schematic shows Type-C 6 routed through FSUSB42 mux U15, which can select USB Serial/JTAG or the ESP32-P4 USB1.1 Full-Speed OTG pair. The PoC was selecting the wrong controller (HS) for this connector; it is corrected to FS OTG. U15 selection and simultaneous HS Host + FS Device operation still require hardware validation. See `docs/WAVESHARE_USB_ROUTING.md`.

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

- Verify U15 selects USB1.1 FS OTG for Type-C 6.
- Verify simultaneous USB 2.0 HS Host on Type-A and USB 1.1 FS Device on Type-C 6.
- USB Host stack and hub/device enumeration.
- HID Host interface and interrupt-IN report reception.
- Retrieval and storage of raw HID report descriptors.
- Generic HID 1.11 parser and parser-populated mapping metadata.
- USB Device report submission from the proxy router.
- USB Device Output/Feature and vendor control-transfer handling.
- JSON loading and atomic runtime configuration replacement.
- WASD vector calculation, mouse shaping and digital action state machines.
- G1 event decoding and G1 LED feedback.
- Keyboard Array rollover and mixed-report reconstruction tests.
- ADC/DAC/GPIO hardware drivers and electrical validation.
- End-to-end passthrough and G HUB compatibility tests.

## Build and verification

The USB HID proof of concept has not been independently built in the current environment. The project owner must run the steps in `docs/USB_DEVICE_POC_TEST.md` using ESP-IDF 6.1 and the Waveshare ESP32-P4-Module-DEV-KIT.

Do not mark the USB Device milestone complete until the firmware builds, a valid PC-facing OTG Device connection is established, Windows enumerates the keyboard and mouse collections after reconnects, and the programming COM connection remains available.

No end-to-end USB Host-to-Device passthrough or G HUB compatibility is claimed.
