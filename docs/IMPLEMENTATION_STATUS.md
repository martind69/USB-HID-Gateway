# Firmware implementation status

Date: 2026-11-15

## Current work focus

The intermittent initial EP0 enumeration issue is parked at the user's request while external electrical measurements are performed. Do not make further firmware timing, PHY, hub, reset or enumeration-state changes until the user returns with measurement results. Development focus has moved to HID keyboard/mouse report passthrough.

## USB Host enumeration diagnostic

Files:
- `components/usb_gateway/usb_host.c`
- `components/usb_gateway/idf_component.yml`
- `sdkconfig.defaults`
- `docs/USB_HOST_ENUMERATION_TEST.md`

Implemented at source level:
- Added managed dependency `espressif/usb ~1.6.1` for ESP-IDF 6.1.
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

The user has confirmed that the H3 selector/jumper setting is resolved. Do not continue treating selector position as the active issue or ask the user to repeat selector tests.

Physical observations (2026-11-15):
- H3=Host (corrected user report): J8 / USBHSD1 (informally called 'USB4' by the user) successfully enumerated the test stick; USB1, USB2 and USB3 did not successfully enumerate. The earlier USB3 success entry was incorrect.
- H3=Device: the Host stack reported activity on downstream Port1, Port2 and Port3; Port4 remained inactive.
- In H3=Device mode, Port1 and Port2 failed at the short Device Descriptor control transfer (`CHECK_SHORT_DEV_DESC FAILED`) and were disabled while still connected. This is not successful enumeration.
- J8 / USBHSD1, informally called 'USB4' by the user, successfully enumerated a USB stick in the corrected H3=Host test. It is not a fourth USB-A downstream port.

The active issue is the failed EP0 control transfer during `CHECK_SHORT_DEV_DESC`, not selector routing. See the current failure analysis in `docs/USB_HOST_ENUMERATION_TEST.md`.

### EP0 enumeration failure diagnosis (2026-11-15)

- Confirmed interpretation: transfer status value 1 is `USB_TRANSFER_STATUS_ERROR`, not a short-packet or short-descriptor length result. Espressif defines this status as excessive transfer errors such as no response or CRC errors.
- `CHECK_SHORT_DEV_DESC` is the state that checks the first 8 bytes of the Device Descriptor after the initial control transfer. The failure occurs before a valid descriptor is parsed and before the application receives `USB_HOST_CLIENT_EVENT_NEW_DEV`.
- Added runtime DEBUG logging for `USBH`, `ENUM`, `HUB` and `EXT_PORT` before USB Host installation.
- Updated the managed USB Host dependency minimum to `espressif/usb ~1.6.1`, which contains additional ESP32-P4 channel-error and external-hub cleanup fixes. This update is a safer diagnostic baseline, not a claimed fix for the observed transfer error.
- The updated firmware has not yet been built or tested on the board. The next required evidence is a full DEBUG UART capture from boot through failure, with the selector left in its already confirmed position.
- Do not bypass the failed initial descriptor transfer or change `fsls_only`, PHY mapping, hub settings or reset timings until the detailed trace identifies the failing layer.

### CH334F is the primary fault domain (2026-11-15)

The user confirmed that the only relevant difference between the working direct USB-stick connection at J8 / USBHSD1 and the failing USB-A downstream connections is the CH334F hub path. The selector/jumper setting is resolved and is not to be revisited.

The CH334F is a USB 2.0 four-port hub controller with HS/FS upstream support and MTT capability. The ESP32-P4 Host uses `fsls_only = true`, intentionally forcing the hub's upstream connection to Full Speed so Full-/Low-Speed devices can be reached without relying on the ESP-IDF Host stack's unsupported TT layer for a High-Speed hub.

The current logs show downstream hub-port activity and reset attempts, followed by failure of the child's first EP0 control transfer in `CHECK_SHORT_DEV_DESC`. The evidence does not yet identify whether the cause is CH334F port reset/timing, downstream D+/D- signal integrity, CH334F clock/reset/power/strap configuration, or Host/HUB driver interaction.

Next: compare the known-good direct J8 test with one downstream CH334F port at a time; capture full hub debug logs and, if available, D+/D- during reset and the first GET_DESCRIPTOR(Device) request. Check CH334F V5/VDD33, RESET#, 12 MHz clock and PSELF strap against the board schematic. No production Host reset timing or PHY changes are approved. A separately documented 10 ms enumeration-stage pacing experiment is prepared as a controlled diagnostic only.

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

## Generic HID parser – per-interface contexts


Files:
- `components/usb_gateway/hid_parser.h`
- `components/usb_gateway/hid_parser.c`
- `tests/host/test_hid_parser.c`
- `docs/HID_PARSER_IMPLEMENTATION.md`

Implemented at source level:
- Fixed-capacity descriptor storage and parser metadata; no heap allocation.
- HID short-item decoding and bounded long-item skipping.
- Input, Output and Feature report layouts with independent bit cursors per Report ID.
- Basic Global Push/Pop and Collection nesting validation.
- Field metadata for usage page/usage/range, bit offset, width, count and Main-item flags.
- Host-side smoke-test source for a boot-style keyboard descriptor.

Not yet verified:
- The new parser and test have not yet been compiled or executed in the user's ESP-IDF/host environment.
- The parser now uses caller-owned per-interface contexts.
- Full HID 1.11 usage semantics, Array slot resolution, bit accessors, parser fuzzing and G815/G502 captured-descriptor tests remain pending.
- The Espressif USB Host HID class driver is installed alongside the USB Host library. It opens HID interfaces, copies report descriptors, parses per-interface contexts and queues raw interrupt-IN reports. A semantic translator maps common keyboard modifiers/key arrays and mouse buttons/relative axes to the existing TinyUSB keyboard/mouse report IDs. The queue is consumed in task context and retains the pending report while the PC endpoint is busy. Host-side parser and semantic translation tests are present and a GitHub Actions workflow compiles/runs them. The ESP-IDF firmware and hardware path have not yet been built or tested.
- Raw reports enter a bounded queue in `hid_proxy.c`, are decoded through `hid_semantic.c`, and are submitted to TinyUSB's fixed keyboard/mouse report IDs. Unsupported report formats are not forwarded.

## Integration still pending

- Verify that Type-C 6 exposes USB OTG FS on GPIO24/GPIO25 after the runtime PHY selection.
- Verify that Type-C 4 / CH343P remains available for programming and logs after PHY selection.
- Verify simultaneous USB 2.0 HS Host on USB1–USB3 via U14/CH334F and USB 1.1 FS Device on Type-C 6 after the isolated Device test passes.
- Resolve the EP0 `CHECK_SHORT_DEV_DESC` transfer error using detailed Host/HUB/port diagnostics in the confirmed selector position.
- HID Host interface discovery/claiming, report descriptor retrieval and interrupt-IN report reception.
- Retrieval and storage of raw HID report descriptors.
- Per-interface generic HID 1.1 parser contexts, full descriptor semantics and parser-populated mapping metadata.
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

Basic semantic keyboard/mouse forwarding into the fixed TinyUSB report descriptor is implemented at source level. No transparent raw USB passthrough, USB MSC bridge or G HUB compatibility is claimed.


### Device-dependent Full-Speed hub comparison (2026-11-15)

New user test result:

- Full-Speed device `090C:1000` successfully enumerates through the CH334F. The observed log reports address 3, configuration 1 and one interface.
- Device `8564:1000` successfully enumerates directly at J8 / USBHSD1 but previously failed at the first child EP0 transfer through the CH334F.
- The CH334F can therefore successfully enumerate at least one Full-Speed child device. A general Full-Speed repeater failure and a purely Low-Speed-specific explanation are no longer leading hypotheses.

The next investigation is a controlled A/B/A comparison on the same CH334F downstream port using `090C:1000` (known-good) and `8564:1000` (known-failing). The Host application now logs `bMaxPacketSize0` (EP0_MPS) and `bcdUSB` for devices that complete enumeration.

Limitation: the application cannot read/log `bMaxPacketSize0` for `8564:1000` when it fails before `USB_HOST_CLIENT_EVENT_NEW_DEV`. The initial 8-byte descriptor payload, actual transfer length/status and handshake must then be captured inside the ESP-IDF enumeration/HCD path or with a USB protocol analyzer. No EP0 sizing, reset timing, PHY or hub-driver changes should be made before that evidence is available.

### Intermittent CH334F enumeration result (2026-11-15)

Latest UART capture confirms successful Full-Speed enumeration through the CH334F for both `090C:1000` and `8564:1000`. Both report `bMaxPacketSize0=64`; `bcdUSB` is `0x0210` and `0x0200`, respectively. The `090C:1000` device enumerates successfully, a subsequent attempt fails at `CHECK_SHORT_DEV_DESC` with address 0, and a later attempt succeeds again. `8564:1000` also succeeds at address 4.

This changes the assessment: the failure is intermittent, not a consistent incompatibility of `8564:1000`. Both devices can enumerate through the hub. A static EP0 MPS mismatch between them is not supported; a general Full-Speed hub failure is also ruled out by successful enumerations.

Next: repeat a clearly labelled A/B/A sequence on one CH334F downstream port and correlate each insertion with the UART timestamps. Investigate intermittent hub-port state/reset sequencing and the initial EP0 transfer. If reproduced, instrument the ESP-IDF enumeration control transfer for setup packet, HCD status, actual length, received bytes and timing after reset completion. No firmware timing, PHY, `fsls_only` or hub configuration changes until these measurements are available.

### Hub re-detection after brief contact interruption (2026-11-15)

The user reports that a very brief interruption of the device contacts on the CH334F hub can force a new detection attempt. In the supplied Port3 trace, the initial EP0 transfer fails at 207.970 s, Port3 is disabled while still connected, and the Host reports removal at address 0. After the brief contact interruption, device 8564:1000 successfully enumerates at 212.600 s (Full Speed, EP0_MPS=64, bcdUSB=0x0200, one interface).

This confirms that 8564:1000 can enumerate through the CH334F; a fixed device/hub incompatibility is ruled out. The 4.63 s is the interval between the logged failure and success, not the duration of the physical interruption. The observation shifts the leading hypothesis toward connection detection, port-state recovery, debounce/reset sequencing or retry behavior around the initial EP0 transfer. The excerpt does not establish which contact was interrupted or whether success followed a fresh hub connect event or an internal retry.

No Host firmware timing, PHY, fsls_only or hub configuration changes are justified yet. Next capture should timestamp the physical interruption and correlate hub port status/change bits, disconnect/connect, reset start/completion, port enable and the first GET_DESCRIPTOR(Device) SETUP. Do not intentionally short connector contacts.


### Configurable USB enumeration timing experiment (2026-11-15)

Prepared a controlled 10 ms enumeration pacing experiment in response to the intermittent initial EP0 failure and the user's observation that reconnecting D+/D- while VBUS remains present can trigger successful detection.

- Added Kconfig option `CONFIG_USB_GATEWAY_ENUMERATION_STAGE_DELAY_MS` (0–100 ms, default 10 ms).
- Added `scripts/apply_usb_enum_delay.ps1`, an idempotent helper that patches the resolved managed component source at `managed_components/espressif__usb/src/enum.c`.
- Added `docs/USB_HOST_ENUMERATION_TIMING_EXPERIMENT.md` with apply, A/B test and interpretation instructions.
- Set the fresh-build default to 10 ms in `sdkconfig.defaults`.
- The patch inserts the delay before the enumeration state machine advances via `set_next_stage()`. It affects all enumeration-stage transitions, not only the first EP0 request.
- No reset timing, debounce, PHY mapping, `fsls_only`, hub configuration or board selector setting was changed.

The patch has been committed as a reproducible project script, but it has **not yet been applied to the user's local managed component, compiled, flashed or tested on hardware**. The next step is to run the documented PowerShell script, build, then perform a controlled 0 ms versus 10 ms comparison with at least 10 insertions per setting on the same device and hub port. If 10 ms is ineffective, test 15 ms.


### Broad enumeration delay test result (2026-11-15)

The user tested enumeration-stage delays from 10 ms through 50 ms. No change in the failure behavior was observed. The broad delay experiment is negative and should not be extended to larger values.

- Changed the Kconfig default to 0 ms.
- Changed `sdkconfig.defaults` to keep the experimental delay disabled by default.
- Retained the optional Kconfig control and idempotent patch helper for future targeted testing.
- No reset-hold, reset-recovery, debounce, PHY, `fsls_only`, hub configuration or board routing changes were made.

Parked pending the user's electrical measurements. When resumed, instrument hub port status/change bits, reset completion and port enable, plus the first 8-byte `GET_DESCRIPTOR(Device)` control transfer. Capture the SETUP packet, HCD status, actual length, received descriptor bytes, channel errors and monotonic timestamps. If possible, correlate with a D+/D- capture at the CH334F downstream port. The current evidence does not justify further global delay changes.
