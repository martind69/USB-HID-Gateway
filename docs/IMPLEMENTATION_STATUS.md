# Firmware implementation status

Date: 2026-11-13

## Implemented in this branch

### Field-level mapping and proxy routing interface

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
- Fail-open forwarding of the original report if metadata is invalid or scratch allocation fails.
- Synchronous semantic event callback for mapped fields.
- Per-report copy of the mapping configuration so a callback cannot change the rules halfway through one report.

A host test source is present at `tests/host/test_mapping.c`.

## Integration still pending

- The generic HID parser does not yet populate `mapping_report_field_t`.
- The USB Host/Device backends do not yet call `hid_proxy_route_input_report()`.
- JSON loading and atomic runtime configuration replacement are not yet connected.
- WASD vector calculation, mouse shaping and digital action state machines are not yet implemented by this interface.
- G1 event decoding and G1 LED feedback remain unbound pending protocol evidence.
- Keyboard Array rollover behavior and mixed-report reconstruction require parser-level tests.
- No end-to-end passthrough or G HUB compatibility is claimed.

## Build and verification

The code in this branch has not been independently built in the current environment. The project owner is compiling the current checkout with ESP-IDF 6.1. A successful local build result should be recorded only after the build completes without errors.

The host test source is not yet wired into CMake/CI. Compile it together with `mapping.c` using a C11 compiler and run it before treating the mapping behavior as verified.
