# USB Proxy Core Implementation

## Goal

Replace the current semantic-only forwarding path with a descriptor- and transfer-oriented USB proxy. The transport path must preserve raw USB payloads; HID parsing and application mapping remain optional passive observers.

The first implementation target is the existing ESP32-P4 board and the Logitech G810/G502 test topology. This is not yet a claim of arbitrary USB-class compatibility.

## Current source audit

The current implementation is not a suitable transport foundation without a driver-ownership change:

- `usb_host_hid` opens HID interfaces and exposes the report descriptor plus raw interrupt-IN reports.
- Its public API does not expose a generic downstream device handle, arbitrary control-transfer submission, interrupt-OUT forwarding, or generic non-HID endpoint management.
- The application Host client separately opens devices for diagnostics but does not claim interfaces.
- TinyUSB's application API exposes descriptor callbacks and a Vendor Control callback, but not a universal application callback for every SETUP packet.
- The current PC-facing descriptor is a compile-time keyboard/mouse composite descriptor. It is not derived from the downstream device.
- The current proxy translates reports into fixed keyboard/mouse reports and drops unsupported formats. It is not a raw USB proxy.

The Host API does support asynchronous control and non-control transfers. Completion callbacks are dispatched by the owning client's `usb_host_client_handle_events()` task. A control transfer starts with the eight-byte SETUP packet in its transfer buffer. See the official [ESP-IDF USB Host API](https://github.com/espressif/esp-usb/blob/master/host/usb/include/usb/usb_host.h).

TinyUSB exposes dynamic Device/Configuration/String descriptor callbacks and `tud_control_xfer()`. Its public application callback `tud_vendor_control_xfer_cb()` is limited to Vendor-type requests. The project now pins `espressif/esp_tinyusb` to 3.0.1, which bundles Espressif TinyUSB 0.21.0~6, and provides `scripts/apply_tinyusb_control_hook.ps1`. The patch adds `tud_control_request_cb()` before TinyUSB's built-in dispatch and exposes `tud_defer_func()` so Host completion callbacks can safely schedule control-stage completion in the TinyUSB task context. Run `idf.py reconfigure` followed by the PowerShell patch script. The patch is not applied automatically by CMake.

## Required ownership model

The HID class driver must not continue to claim interfaces that the proxy needs to manage. Replace the current `usb_host_hid` ownership with one proxy Host client that:

1. Opens the downstream device.
2. Reads and validates the complete active Configuration descriptor.
3. Claims each supported interface and records every endpoint.
4. Retrieves HID report descriptors through the Host control-transfer API.
5. Owns all endpoint transfers and their buffers.
6. Submits asynchronous EP0 and endpoint transfers from a dedicated Host-client event task.
7. Releases all claimed interfaces and transfer resources on disconnect.

Do not run the Espressif HID class driver and the proxy's own interface manager against the same interfaces.

The existing Host diagnostic path should be folded into this owner or changed so it does not create a second competing lifecycle.

## Confirmed upstream identity requirement — two independent devices

The user has confirmed that the PC must continue to enumerate the Logitech G810 and G502 as two separate USB devices, each with its own original VID/PID and independent USB device address. A single composite proxy device is rejected.

The current ESP32-P4 allocation uses one OTG controller in Host mode for the downstream Logitech topology and one OTG controller in Device mode toward the PC. The P4 silicon has one High-Speed and one Full-Speed OTG controller, but the USB Serial/JTAG block is not a third general-purpose TinyUSB Device controller. Espressif documents one HS and one FS Device peripheral; simultaneous independent TinyUSB Device instances on the P4 are not established by the current project stack. Do not infer that two independent upstream Device instances are available from the silicon controller count alone.

The selected direction for feasibility work is to preserve the current P4 Host + Device roles and investigate adding:
- A second independently programmable USB Device MCU/controller for the second Logitech identity.
- A PC-facing USB hub with separate downstream ports for the two upstream Device controllers.
- A framed inter-processor link (SPI is the initial candidate) so the P4 remains the sole owner of the physical downstream Host bus and forwards control transactions and raw endpoint payloads to the correct upstream Device controller.

The PC will see a hub introduced by the gateway and two separate child devices. This meets the confirmed VID/PID/device-address requirement, but it does not automatically reproduce the original hub parent/port topology. Because the G502 is physically connected through the G810's internal hub/pass-through, G HUB compatibility may additionally depend on the hub identity and hierarchy. The G810 internal hub descriptors and G502 parent/port path must be audited before claiming full compatibility.

Do not implement a merged composite descriptor or interface-only vendor-request routing. The detailed hardware candidate, controller allocation, inter-MCU protocol and topology caveats are tracked in `docs/USB_UPSTREAM_MULTI_DEVICE_ARCHITECTURE.md`.

The initial descriptor parser remains device-configuration scoped. It does not merge G810 and G502 configurations, and its eventual integration must maintain separate per-device models and independent EP0 state.

## Stage 1 — Descriptor-driven upstream enumeration

A host-independent bounded parser/model is now present in `components/usb_gateway/usb_proxy_descriptor.c/.h`. It validates the complete configuration descriptor byte stream, tracks interface alternate settings, HID report descriptor lengths and endpoint properties, and exposes interface/endpoint lookup helpers. Host tests are in `tests/host/test_usb_proxy_descriptor.c`. This parser is not yet connected to the Host device lifecycle or TinyUSB descriptor callbacks.

Store a validated downstream device model:

- Device descriptor.
- Active Configuration descriptor as a bounded raw byte sequence.
- Interface and alternate-setting descriptors.
- HID class descriptors and report descriptors.
- Endpoint descriptors, including address, transfer type, packet size and interval.
- Device speed and active configuration value.

Build a PC-facing Full-Speed descriptor set from that model. Do not blindly copy fields that are local to a USB bus or incompatible with the ESP32-P4 Full-Speed Device controller. Preserve class-specific descriptor bytes and interface numbering where possible. Reject unsupported configurations explicitly rather than advertising endpoints the proxy cannot service.

The downstream model must be complete before the PC is allowed to enumerate the proxy. Keep the upstream D+ pull-up disconnected until the descriptor model is ready; reconnect only after the model and endpoint map are committed atomically. On downstream removal or replacement, disconnect upstream, invalidate all transfer state, then enumerate the new device.

Initial target: one downstream device with HID interfaces and supported interrupt endpoints. Non-HID interfaces may be described only when their transport support is implemented; do not claim them as operational merely because their descriptors can be copied.

## Stage 2 — Raw endpoint forwarding

Create an endpoint map with, at minimum:

- Downstream device handle and endpoint address.
- Upstream endpoint address.
- Interface number and alternate setting.
- Direction and transfer type.
- Maximum packet size and polling interval.
- Transfer buffer ownership and in-flight state.
- Error, cancellation and backpressure counters.

For every supported downstream IN endpoint, maintain the required Host transfers and queue completed payloads for the corresponding upstream Device IN endpoint. For upstream OUT endpoints, submit the payload to the matching downstream Host endpoint. Preserve report bytes, report IDs, ordering within each endpoint and transfer errors. Do not run semantic translation in the transport path.

The current semantic parser may receive a read-only copy of raw HID Input reports after the transport layer accepts them. Parser failure must never drop or modify a transport payload.

The first validation target is HID Interrupt IN and OUT. Bulk and isochronous support are separate capabilities and must not be implied by this milestone.

## Stage 3 — EP0 Control-transfer bridge

A generic upstream SETUP request must be captured before TinyUSB's built-in standard/class dispatch consumes it. The version-pinned extension described above provides the complete `tusb_control_request_t` and SETUP/DATA/ACK stages. The application hook and task-defer API are scaffolding only; the downstream transfer transaction manager is not yet implemented.

The bridge must:

1. Copy the upstream SETUP packet and associated data into an owned transaction object.
2. Translate only bus-local fields that necessarily differ; preserve request type, recipient, request, value, index and data.
3. Submit the downstream control transfer asynchronously using `usb_host_transfer_submit_control()`.
4. Return the downstream data/status to TinyUSB using its control-transfer API.
5. Handle short data stages, zero-length status stages, STALL, timeout, disconnect and cancellation caused by a new SETUP request.
6. Never block TinyUSB's Device task while waiting for the Host completion callback.

The bridge must explicitly handle address/configuration/interface state. The proxy has two independent USB buses; the PC-side address is not the downstream device address.

Initial validation target: standard enumeration requests, HID class GET/SET_REPORT, and Logitech vendor requests observed in the supplied USBPcap captures. Unsupported requests must fail deterministically and be counted; they must not be silently acknowledged.

## Runtime sequencing

1. Start the Host library and the proxy Host client.
2. Enumerate the downstream device and build the descriptor/endpoint model.
3. Validate that every advertised upstream interface and endpoint has a registered transport handler.
4. Install/start the TinyUSB Device stack while upstream remains disconnected.
5. Publish the immutable descriptor snapshot and endpoint map.
6. Connect upstream and allow PC enumeration.
7. On disconnect, cancel in-flight transfers, release claimed interfaces, clear queues and detach upstream.

The existing controller selection remains unchanged:

- Host: `peripheral_map = BIT0`, `fsls_only = true`.
- Device: `TINYUSB_PORT_FULL_SPEED_0`.
- Runtime PHY selection remains `usb_serial_jtag_ll_phy_select(1)`.

Do not change PHY routing, `fsls_only`, hub configuration, reset timing or the parked downstream enumeration investigation as part of this proxy work.

## Test gates

### Gate 1 — Descriptor mirror

- Attach the G810 downstream.
- Verify the proxy remains disconnected until descriptor capture is complete.
- Capture PC-side enumeration with USBPcap.
- Compare Device, Configuration, Interface, HID and Endpoint descriptors against the downstream capture.
- Confirm all advertised endpoints have a transport implementation.

### Gate 2 — Raw HID transport

- Verify keyboard and mouse Input reports are byte-identical downstream versus upstream.
- Verify report IDs, ordering and multiple reports per polling interval.
- Exercise keyboard normal keys, G-keys/M-keys, media controls and mouse movement/buttons.
- Verify disconnect releases resources and does not leave stuck keys/buttons.

### Gate 3 — Control bridge

- Compare upstream and downstream SETUP packets and DATA stages.
- Test standard requests, HID GET_REPORT/SET_REPORT and captured Logitech vendor requests.
- Verify STALL, short response, timeout and disconnect behavior.
- Test G HUB RGB and DPI operations only after the relevant request sequences are shown to traverse both sides.

## Explicit limitations

- The ESP32-P4 PC-facing Device controller is Full-Speed. A downstream High-Speed device cannot be transparently represented as High-Speed to the PC.
- Two independent USB buses do not preserve wire-level timing or electrical behavior.
- Descriptor mirroring alone does not make an interface functional; every advertised endpoint and control request needs a working handler.
- Bulk, isochronous, hubs, alternate settings, remote wakeup, suspend/resume and composite non-HID classes require separate validation.
- No firmware build or hardware test is claimed by this architecture record.

## Implementation order

Implement Stages 1–3 as one coherent proxy core, but keep the test gates separate so failures can be localized. First remove the HID class driver's ownership, then add the descriptor model and endpoint manager, then add the TinyUSB control-dispatch extension and asynchronous EP0 bridge. Do not retain the current semantic translation as the primary forwarding path.

## Current implementation checkpoint (2026-11-16)

Committed:
- Bounded Configuration descriptor model/parser and host-side tests.
- GitHub Actions compiles the new parser test with strict warnings.
- Exact `esp_tinyusb` component version pin at 3.0.1.
- PowerShell patch script for the TinyUSB 0.21.0~6 generic SETUP hook and task-context deferred callback API.
- Public declaration contract in `tinyusb_control_hook.h`.

Not yet implemented:
- Host lifecycle integration and descriptor snapshot storage.
- Host interface ownership replacement for `usb_host_hid`.
- Dynamic upstream descriptor generation and attach gating.
- Endpoint transfer manager and raw IN/OUT forwarding.
- Asynchronous EP0 transaction manager using the new callback/defer APIs.

No ESP-IDF build or board test has been run for this checkpoint. Do not flash this intermediate state as a working proxy.
