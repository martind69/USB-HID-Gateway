# Basic HID keyboard/mouse semantic forwarding

Status: source implementation and host-side test sources are committed. ESP-IDF build and hardware verification are pending.

## Data path

```text
USB HID device
    |
    v
Espressif usb_host_hid
    |  report descriptor + interrupt-IN reports
    v
usb_host_hid.c
    |  per-interface parser context
    |  bounded raw-report queue
    v
hid_proxy.c
    |
    v
hid_semantic.c
    |  Generic Desktop Keyboard / Mouse usage decoding
    |  aggregate state across interfaces
    v
TinyUSB HID Device
    |
    +--> Report ID 1: standard 8-byte keyboard report
    |
    +--> Report ID 2: standard 5-byte mouse report
    |
    v
PC
```

The USB Host HID class driver is installed after the USB Host Library. It opens each discovered HID interface, retrieves and stores the raw report descriptor, parses it into a caller-owned `hid_parser_context_t`, and starts interrupt-IN reception. No SET_PROTOCOL(BOOT) request is sent; the upstream device remains in its default report protocol.

## Keyboard translation

Input fields on Usage Page 0x07 are decoded from the parsed bit layout:

- Modifier usages 0xE0–0xE7 are mapped to the standard modifier byte.
- Keyboard Array fields are read as usage values.
- Variable keyboard fields are interpreted from their Usage or Usage range.
- Up to six simultaneous non-modifier keys are emitted in the standard 6-key report.
- More than six simultaneous keys produce ErrorRollOver.
- Keyboard state is merged across active keyboard interfaces.
- Interface removal emits a report with that interface's held keys released.

The PC-side report is 8 bytes: Modifier, Reserved, six keycodes. Report ID is 1.

## Mouse translation

Input fields are decoded by Usage Page and Usage:

- Button usages 1–5 map to the five buttons exposed by the fixed PC descriptor.
- Relative Generic Desktop X and Y map to signed 8-bit deltas.
- Generic Desktop Wheel maps to the vertical wheel byte.
- Consumer AC Pan maps to the horizontal pan byte.
- Multiple mouse interfaces contribute to a merged button state.
- Interface removal releases its held buttons.

The PC-side report is 5 bytes: Buttons, X, Y, Wheel, Pan. Report ID is 2. Relative values outside the signed 8-bit range are saturated; splitting large deltas into multiple reports is not implemented yet.

## Queueing and lifecycle

- Raw reports are copied in the HID Host callback; no semantic decoding occurs there.
- A bounded FreeRTOS queue moves reports to the gateway task.
- Queue capacity is 24 entries; 16 are reserved for data reports and 8 for interface-removal events.
- A report remains pending while the PC-facing TinyUSB endpoint is busy.
- When the PC detaches, queued input is discarded and semantic state is reset. Forwarding resumes after the PC enumerates again.
- Queue overflow drops the newest data report and is logged.

## Explicit limitations

This is semantic forwarding into a fixed standard keyboard/mouse descriptor, not transparent USB forwarding.

Not yet supported:
- Consumer Control / media keys and volume controls.
- More than five mouse buttons.
- Arbitrary HID collections and vendor-defined reports.
- Dynamic mirroring of upstream HID report descriptors.
- Upstream Output and Feature reports.
- HID class control requests and vendor-specific control transfers.
- Logitech G HUB protocol forwarding, RGB, profiles, DPI configuration or device-specific status reports.
- Full NKRO exposure beyond six simultaneous keys.
- Exact preservation of upstream report IDs, timing and packet boundaries.

The existing `hid_proxy_route_input_report()` mapping API remains separate. Field-level Proxy-mode routing is not yet connected to this semantic translation path.

## Verification

Host-side tests cover:
- Independent keyboard and mouse parser contexts.
- Bit-field extraction.
- Keyboard modifier/key translation.
- Mouse button/X/Y translation.
- Interface removal and release reports.

GitHub Actions compiles and runs these tests. The firmware itself still requires an ESP-IDF v6.1 build and board verification with the G815 and G502.
