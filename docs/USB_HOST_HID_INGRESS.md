# USB Host HID ingress

Status: implemented at source level; ESP-IDF build and hardware verification pending.

## Driver integration

The project uses Espressif's `espressif/usb_host_hid` managed component (`~1.2.1`) on top of the existing `espressif/usb` Host Library.

The HID class driver is installed after `usb_host_install()`. It owns a separate Host Library client and background event task. The existing application Host client remains active for device-level diagnostics; it does not claim HID interfaces.

## Per-interface lifecycle

For each HID interface reported by the class driver:

1. Allocate one bounded interface slot and a unique interface ID.
2. Read the device address, interface number, subclass and protocol.
3. Open the HID interface without changing its current HID protocol.
4. Read VID/PID and retrieve the raw HID report descriptor.
5. Copy the descriptor into interface-owned storage.
6. Parse the descriptor into that interface's own `hid_parser_context_t`.
7. Start interrupt-IN report reception.
8. Copy each raw report into the bounded proxy ingress queue.
9. On disconnect, close the interface and release its slot.

No SET_PROTOCOL(BOOT) or SET_IDLE request is issued by this integration. This avoids forcing devices away from their report protocol while vendor-specific reports and G HUB compatibility are still under investigation.

## Bounded resources

- Maximum tracked HID interfaces: 8.
- Maximum retained raw report descriptor: 2048 bytes per interface.
- Parser descriptor capacity: 1024 bytes per interface. Larger descriptors are retained raw but are not parsed yet.
- Raw input report buffer: 64 bytes.
- Proxy ingress queue: 16 reports.

Queue overflow is logged and the newest report is dropped. The current USB Device backend does not yet consume this queue, so this is an ingress milestone, not functioning passthrough.

## Deliberate limitations

- No semantic keyboard/mouse translation is implemented yet.
- No dynamic PC-facing HID descriptor generation is implemented.
- The current TinyUSB composite descriptor remains the static keyboard/mouse test descriptor.
- Output, Feature and vendor-specific control requests are not forwarded.
- The host integration has not yet been built with the user's ESP-IDF v6.1 development checkout or tested on the board.
- The generic Host diagnostic client and HID class driver both observe the same Host Library. The class driver claims only HID interfaces; the diagnostic client opens devices for descriptor logging but does not claim interfaces.

## Verification required

1. Build with ESP-IDF 6.1 and component manager enabled.
2. Confirm the resolved `espressif/usb_host_hid` version is 1.2.x.
3. Connect G815 and G502 through the existing hub.
4. Verify one interface log per HID interface and correct VID/PID/interface numbers.
5. Confirm raw report descriptor lengths and parser success/failure.
6. Confirm raw input report callbacks arrive and queue overflow remains zero under ordinary typing/mouse movement.
7. Disconnect one device while the other remains active; verify the remaining interface continues reporting.
