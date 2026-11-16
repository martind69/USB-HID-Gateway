# Firmware implementation status

# USB proxy implementation progress (2026-11-16)

A first host-independent Configuration descriptor model has been added:

- `usb_proxy_descriptor.h/.c` stores a bounded copy of the complete Configuration descriptor stream.
- Parses interface descriptors including alternate settings, HID class descriptors and report-descriptor lengths, and endpoint descriptors.
- Validates descriptor bounds, declared endpoint counts, unique interface count, endpoint zero/reserved address bits and duplicate endpoint addresses within one interface alternate setting.
- Exposes lookups for interface/alternate-setting and endpoint mappings.
- Added host-side tests and CI compilation with `-Wall -Wextra -Werror -pedantic`.
- Pinned `espressif/esp_tinyusb` to 3.0.1 (bundled TinyUSB 0.21.0~6).
- Added `tinyusb_control_hook.h` and `scripts/apply_tinyusb_control_hook.ps1`. The patch adds a generic pre-dispatch SETUP callback and a task-context defer API backed by TinyUSB's existing `USBD_EVENT_FUNC_CALL` queue event.

This is the descriptor-model foundation only. It is not yet connected to the Host device lifecycle or TinyUSB descriptor callbacks. The TinyUSB generic SETUP hook is prepared as a version-pinned patch, but it has not yet been applied or compiled in the user's managed component directory. The current Espressif HID Host class driver still owns HID interfaces; raw endpoint transport and generic EP0 forwarding are not implemented. No target build or hardware test is claimed for this increment.

Next implementation work:
1. Integrate a per-downstream-device descriptor snapshot into the existing Host client without allocating one full model per possible USB address.
2. Replace `usb_host_hid` interface ownership with a proxy-owned interface/endpoint manager.
3. Generate and publish the upstream Full-Speed descriptor set before connecting the Device port.
4. Add raw interrupt IN/OUT forwarding.
5. Apply and verify the pinned TinyUSB patch, then implement the asynchronous EP0 transaction manager using the generic SETUP hook and task-context defer API.


Date: 2026-11-15

### Confirmed upstream device identity requirement (2026-11-16)

The user confirmed that the PC must enumerate the G815 and G502 as two separate USB devices with their original VID/PID values and independent USB device addresses. A single composite proxy device is rejected.

The current P4 allocation uses one OTG controller as Host and one as Device. The preferred hardware direction for feasibility work is to retain these roles, add a second independently programmable USB Device MCU/controller, and connect both upstream Device controllers to a new PC-facing USB hub. The P4 remains the sole owner of the downstream Logitech Host bus; the second Device MCU receives control transactions and raw endpoint payloads over an inter-processor link. This architecture is not yet implemented or hardware-validated.

The PC-facing hub would expose two separate child devices but adds a hub node to the path. The observed topology places G815 and G502 as siblings behind the ASUS monitor's Genesys Logic hub (05E3:0610); the G815 pass-through is cable-only, not an internal hub. G HUB compatibility still requires checking whether the new hub/port path matters. Do not model G502 as a child of a G815 hub or continue with a merged composite descriptor. See `docs/USB_UPSTREAM_MULTI_DEVICE_ARCHITECTURE.md` and `docs/USB_PROXY_IMPLEMENTATION.md`.

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

The application Host client remains an enumeration/diagnostic client: it logs device descriptors and tracks device lifecycle. HID interface claiming and report reception are handled by the separate Espressif HID Host class driver described below. The latest HID integration has not yet been built or tested on the physical board.

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

## Generic HID parser and semantic forwarding

Files:
- `components/usb_gateway/hid_parser.h`
- `components/usb_gateway/hid_parser.c`
- `tests/host/test_hid_parser.c`
- `tests/host/test_hid_semantic.c`
- `docs/HID_PARSER_IMPLEMENTATION.md`
- `docs/USB_HOST_HID_INGRESS.md`
- `docs/USB_HID_SEMANTIC_ROUTING.md`

Implemented at source level:
- Fixed-capacity descriptor storage and parser metadata; no heap allocation.
- HID short-item decoding and bounded long-item skipping.
- Input, Output and Feature report layouts with independent bit cursors per Report ID.
- Basic Global Push/Pop and Collection nesting validation.
- Field metadata for usage page/usage/range, bit offset, width, count, logical range and Main-item flags.
- Top-level Application collection metadata and bounds-checked signed/unsigned bit extraction.
- Independent keyboard/mouse parser-context and semantic translation test sources.
- Espressif `usb_host_hid ~1.2.1` class-driver integration. It opens each HID interface, retrieves the raw report descriptor, parses a per-interface context and starts interrupt-IN report reception.
- Bounded raw report queue with reserved capacity for interface-removal events.
- Semantic translation of common keyboard modifiers/key arrays and common mouse buttons, relative X/Y, Wheel and AC Pan into the fixed TinyUSB keyboard/mouse report IDs.
- Aggregation of held keys/buttons across interfaces, disconnect release reports, endpoint backpressure and PC-detach queue reset.
- GitHub Actions workflow compiles/runs the host parser and semantic tests.

Not yet verified:
- The GitHub Actions workflow was added, but its result has not yet been checked.
- The firmware has not yet been compiled with the user's ESP-IDF 6.1 checkout after these changes.
- The HID Host and semantic path have not yet been tested with the physical G815/G502 devices.
- Full HID 1.11 usage semantics, full Array resolution, parser fuzzing and captured-descriptor regression tests remain pending.
- Unsupported Consumer Control, vendor-defined, Output and Feature reports are not forwarded.

## Integration still pending

- Verify that Type-C 6 exposes USB OTG FS on GPIO24/GPIO25 after the runtime PHY selection.
- Verify that Type-C 4 / CH343P remains available for programming and logs after PHY selection.
- Verify simultaneous USB 2.0 HS Host on USB1–USB3 via U14/CH334F and USB 1.1 FS Device on Type-C 6 after the isolated Device test passes.
- Resolve the EP0 `CHECK_SHORT_DEV_DESC` transfer error using detailed Host/HUB/port diagnostics in the confirmed selector position.
- Dynamic PC-facing HID descriptor generation/mirroring for upstream keyboard, mouse and vendor interfaces.
- Consumer Control/media keys, volume controls and extra mouse buttons beyond the five exposed by the current PC descriptor.
- NKRO beyond six simultaneous keyboard keys and full Array/Usage resolution.
- Upstream Output/Feature report forwarding, HID class control requests and vendor-specific control transfers.
- Logitech G HUB compatibility, including RGB, profiles, DPI and vendor-defined status/configuration reports.
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

Basic semantic keyboard/mouse forwarding into the fixed TinyUSB report descriptor is implemented at source level. No transparent raw USB passthrough, USB MSC bridge or G HUB compatibility is claimed. The current source-level changes still require an ESP-IDF build and physical G815/G502 verification.


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

## CH552 secondary USB Device: boot and interface handoff (2026-11-16)

The CH552 QT Py is specified as the second independent PC-facing USB Device controller. The ESP32-P4 remains the SPI master and firmware-update manager.

Architecture documentation now defines:
- UART ISP on the factory ROM bootloader's fixed P1.6 (RXD1) / P1.7 (TXD1) pins.
- Runtime SPI0 slave on P1.4 (SCS), P1.5 (MOSI), P1.6 (MISO) and P1.7 (SCK).
- Reuse of the same two P4 GPIO nets for UART and SPI through explicit peripheral-matrix and direction changes; separate UART and SPI P4 outputs must not be tied together.
- P3.2/INT0 as the application-assisted BOOT_REQ input, using the ch55xduino-style capacitive interrupt coupling and an application interrupt handler that jumps to the ROM bootloader.
- Explicit safe pin directions, CS pull-up, BOOT_REQ idle bias, transition order, version handshake and recovery boundaries.
- A power-path warning: switching only an external 3.3 V feed does not power-cycle the QT Py while USB VBUS still feeds its onboard regulator. All supply paths must be isolated before a controlled cold-power reset can be claimed.

The full sequence and electrical interface contract are in [CH552 boot, UART ISP and SPI runtime sequence](CH552_BOOT_SEQUENCE.md).

Status: **architecture documented; no CH552 firmware, P4 update-manager code, PCB wiring or hardware validation is claimed.** Required bench gates include 3.3 V UART ISP, BOOT_REQ handoff, P1.6 MISO tri-state, SPI timing, USB enumeration after update and power-path/back-power checks.

### Dedicated CH552 hardware RESET line (2026-11-16)

The interface now includes a dedicated ESP32-P4 GPIO connected to the QT Py RESET pad / CH552 RST input. The QT Py remains continuously powered during normal operation and firmware updates.

Electrical semantics are active HIGH: drive RST HIGH for the verified reset pulse, then LOW to release. The writable 16-bit CH552 Configuration Information Word is at 0x3FF8/0x3FF9; 0x3FFE/0x3FFF are the final factory chip-ID word, not configuration bits. Preserve the complete configuration word and all factory chip-ID words at 0x3FFA–0x3FFF unchanged. The Adafruit QT Py is treated as delivered with En_RST_RESET=1, while WCH raw-chip recommendations differ. Neither the application image nor the P4 update manager may erase or rewrite the configuration or chip-ID area. External reset restarts execution at address 0; it is not treated as a guaranteed UART-ISP entry mechanism. BOOT_REQ remains a separate application-assisted ROM-bootloader handoff.

The previous recommendation to use supply cycling as the normal reset/update mechanism is superseded. Cold power removal is now only a separate service/bring-up condition and must account for all QT Py supply paths, including USB VBUS. The WCH datasheet specifies a minimum external reset pulse width of 70 ns; the documented P4 firmware starting value is a conservative 1 ms pulse, subject to QT Py bench validation.

Status: pin allocation and sequence documented; P4 GPIO number and actual QT Py reset behavior remain to be confirmed. No CH552 configuration-bit change is authorized. The 1 ms reset pulse is a proposed conservative starting value, not a measured requirement. No hardware test is claimed.



### CH552 factory configuration preservation (2026-11-16)

The Adafruit QT Py is treated as delivered with En_RST_RESET=1. The dedicated P4-controlled active-HIGH RESET line remains part of the design, but no CH552 configuration bit may be changed.

The update manager must write application code only and must not erase or rewrite configuration bytes. The [LoveMHz/vnproch55x](https://github.com/LoveMHz/vnproch55x) source is USB-only and sends an erase command followed by application writes; it does not prove configuration-byte preservation. The [UNIT-Electronics CH55x package platform.txt](https://github.com/UNIT-Electronics/Uelectronics-CH552-Arduino-Package/blob/main/ch55x/platform.txt) maps USB uploads to `vnproch55x ... -c {upload.bootcfg}` and serial uploads to `vnproch55x -s {serial.port} -t {build.mcu} ...` without the `-c` argument. The separately reviewed [DeqingSun/vnproch551 source](https://github.com/DeqingSun/vnproch551) shows that omitting `-c` causes the default CH552 A8 boot-options write; `-c KEEP` skips that write. Therefore the stock serial recipe is not approved. The erase command's physical scope remains unverified.

The P4 UART1 updater still requires a dedicated implementation and validation. Its flash and erase scope must be demonstrated not to modify factory configuration or chip-ID data before it is used on the QT Py.


Configuration reference update (2026-11-16): `docs/CH552_BOOT_SEQUENCE.md` now records the complete WCH Table 6.2 bit assignments for the writable word at 0x3FF8/0x3FF9. The factory chip-ID words at 0x3FFA–0x3FFF are separately identified and must also be preserved unchanged. No programmer has yet been approved as safe for this requirement.


### CH552 UART ISP source review (2026-11-16)

The ch55xduino documentation confirms UART1 ISP support for CH552 (CH551 excluded) and an application-assisted interrupt handoff into the ROM bootloader. The DeqingSun `vnproch551/main.cpp` source implements UART1 at 57600 baud with TX prefix `57 AB` and RX prefix `55 AA`.

Critical hazard: this source sends an A8 boot-options write when `-c` is omitted. Its source comment describes an 8-byte write relative to `ROM_CFG_ADDR-8` and the default CH552 option byte `0x03` (P3.6/D+ boot selection, P1.5 cleared). The special `-c KEEP` argument skips that write, but the UNIT-Electronics serial recipe does not pass `-c KEEP`. Thus the stock serial recipe is not safe for this project. The A8 source comment's address mapping must be reconciled with the WCH datasheet's configuration/ID map before any use.

The source's erase command is `A4 01 00 08`; reverse-engineered discussion for a later V2.5 loader describes eight pages being deleted for V2.4 and later, but the page size and erase coverage for the QT Py's actual ROM version remain unverified. `-c KEEP` alone does not establish safe preservation of configuration or chip-ID memory.

Important datasheet correction: writable Configuration Information is at 0x3FF8/0x3FF9. Addresses 0x3FFA–0x3FFF hold factory chip-ID words; 0x3FFE/0x3FFF are the final ID word. The configuration fields in Table 6.2 are Code_Protect, No_Boot_Load, En_Long_Reset, En_RST_RESET, reserved bits 11:10, Must_1 bit 9, Must_0 bit 8, and All_1 bits 7:0. The prior documentation incorrectly described 0x3FFE/0x3FFF as the configuration word and used several incorrect field names; this has been corrected in CH552_BOOT_SEQUENCE.md.

Status: UART1 capability, 57600-baud framing and the A8 configuration-write hazard are source-confirmed. The stock UNIT-Electronics serial recipe omits -c KEEP and is not approved. The erase command's physical coverage and preservation of 0x3FF8–0x3FFF remain unverified. Automatic P4 flashing remains prohibited.

The serial programmer source review uses [DeqingSun/vnproch551](https://github.com/DeqingSun/vnproch551), while the installed package recipe is defined separately in [UNIT-Electronics platform.txt](https://github.com/UNIT-Electronics/Uelectronics-CH552-Arduino-Package/blob/main/ch55x/platform.txt). Do not assume the binary shipped in a particular package release is byte-for-byte identical to the reviewed source without checking its version/build provenance.


### CH552 management framework scaffold (2026-11-16)

Added `components/usb_gateway/ch552_manager.c` and `include/ch552_manager.h`, registered in the ESP-IDF component. The P4-side API defines a callback-based SPI transport and validates a compact application identity response (protocol revision, firmware version, build ID, status, capabilities and CRC16). The request/response protocol is documented in `docs/CH552_BOOT_SEQUENCE.md`.

The update API is deliberately fail-closed and returns `ESP_ERR_NOT_SUPPORTED`; it performs no UART or flash operation. The actual CH552 SPI slave, board-specific P4 transport integration, read-only ROM ISP probe and safe flash backend remain outstanding. The ROM erase range is still unverified, so automatic flashing is prohibited. No build, unit test or hardware test is claimed.


## QT Py CH552 application memory baseline (2026-11-16)

A first standalone CH55xduino sketch has been added at `firmware/qt_py_memory_test/qt_py_memory_test.ino`. It includes the temporary standard HID keyboard USB stack, the agreed P4↔CH552 SPI identity protocol (8-byte request / 17-byte response, CRC-16/CCITT-FALSE), and the P3.2/INT0 BOOT_REQ handoff. The sketch is intended to measure baseline code ROM, xRAM and internal RAM/stack use before adding Logitech vendor-specific behavior.

Build settings: CH552 Board, internal 16 MHz / 3.3 V, P3.6 D+ bootloader pin, USB RAM profile `user148`. The ch55xduino README documents 148 bytes reserved for USB endpoint RAM and 876 bytes remaining xRAM under this profile. The CH552 provides 16 KiB program ROM, 256 bytes internal iRAM and 1 KiB xRAM. These are capacity figures, not a measured build result.

The keyboard profile is only a temporary standard-HID stand-in. It does not implement G815/G502 descriptors or vendor control/report traffic, so its memory report is a lower-bound infrastructure measurement, not proof that the final Logitech emulator fits. No compiler is available in the current execution environment; the sketch has not been compiled, flashed or tested. Capture the ch55xduino compiler summary and generated map/mem files before drawing a capacity conclusion. No flash/configuration update is implemented.


### CH552 G815 composite USB target profile (2026-11-16)

The device allocation is now fixed for the next implementation stage: the CH552 QT Py emulates the Logitech G815 (`046D:C33F`); the ESP32-P4 emulates the G502 HERO (`046D:C08B`). This keeps the G502 regular mouse interrupt-IN report path (8-byte reports, approximately 1,000 reports/s in the supplied active capture excerpt) directly on the P4 rather than adding the CH552/SPI hop.

The supplied `DESCRIPTORs.pcapng` descriptor summary identifies the G815 as a 59-byte configuration with two HID interfaces: interface 0 is boot keyboard (65-byte HID report descriptor, endpoint 0x81 IN, 8-byte maximum packet, bInterval=1); interface 1 is vendor-defined HID (188-byte HID report descriptor, endpoint 0x82 IN, 64-byte maximum packet, bInterval=1). Both endpoints must be active concurrently. At Full Speed, bInterval=1 requests a 1 ms polling interval.

The sketch now records these target properties as inventory constants, but its current ch55xduino USBHIDKeyboard library remains a temporary single-interface stand-in. The constants do not implement the G815 descriptors. The exact HID report-descriptor byte arrays have not yet been extracted; they must be taken from the capture rather than guessed.

Added `docs/CH552_G815_USB_PROFILE.md` with the target descriptor profile and acceptance criteria. Updated the QT Py memory-baseline README and boot-sequence documentation. The next meaningful memory gate is a compile of the actual two-interface G815 profile with EP0, 8-byte endpoint 0x81, 64-byte endpoint 0x82, exact report descriptors, SPI manager and BOOT_REQ code. Endpoint RAM must be measured using the selected USB stack's actual allocation rules; do not infer it from packet sizes alone.

Status: device allocation and descriptor-derived target requirements documented; composite USB implementation, exact report-descriptor extraction, ROM/RAM measurement, compilation, flashing, hardware validation and G HUB compatibility remain outstanding.


### G815 WinHidDump collection descriptors received (2026-11-16)

Added `docs/captures/G815_WINHIDDUMP_COLLECTIONS.md` with the exact six physical G815 Windows HID collection descriptors supplied by the user, including the interface-0 keyboard collection and five interface-1 collections. Updated `docs/CH552_G815_USB_PROFILE.md` with the collection mapping and Report IDs.

Important distinction: WinHidDump exposes collection-level descriptors for Windows HID collection handles. They are not necessarily the byte-for-byte interface-level HID Report Descriptor streams referenced by the USB configuration descriptor (65 bytes for interface 0, 188 bytes for interface 1). The 460-byte descriptor shown under the separate `HID VHF Driver` path is a Windows virtual HID node, not the physical G815 USB interface, and is excluded from the G815 physical descriptor set.

G815 vendor collections:
- Report ID `0x11`: `0x98` bits = 19 report-data bytes (20 bytes including Report ID).
- Report ID `0x12`: `0x1F8` bits = 63 report-data bytes (64 bytes including Report ID).

These lengths align with the observed 20-byte and 64-byte vendor report sizes. Exact endpoint routing still requires correlation with the USB transfer capture. The raw 65-byte and 188-byte interface-level report descriptor streams remain to be extracted from USB `GET_DESCRIPTOR(Report)` responses. No descriptor implementation, compilation, memory measurement, flashing or hardware validation is claimed from this capture alone.


## CH552 G815 memory probe expansion (2026-11-16)

The QT Py memory-sizing sketch now includes the intended P4-to-CH552 report-path staging footprint in addition to the existing SPI identity exchange:

- 72-byte maximum request frame (6-byte header + 64-byte payload + 2-byte CRC);
- 17-byte identity/command response;
- 8-byte keyboard report staging buffer;
- 64-byte vendor report staging buffer.

This increases the explicit xRAM staging allocation from 25 bytes to 161 bytes. The existing two-interface USB endpoint DMA reservation remains separate and is provided by the ch55xduino `user148` profile. The new buffers are a memory-sizing probe; the variable-length SPI report transport and second USB IN endpoint are not yet implemented.

The G815 report-descriptor arrays reconstructed by the user are now treated as provisional implementation data rather than a blocker for memory feasibility. The raw 65-byte and 188-byte USB report-descriptor streams remain desirable for later byte-exact refinement.

**Next:** compile the expanded sketch with the selected ch55xduino package and `user148` setting. Record code ROM, xRAM, internal RAM/stack and map-file endpoint allocation. Then replace the temporary keyboard-only USB implementation with a CH552 dual-interface implementation using EP0 plus two independent interrupt-IN endpoints (8-byte keyboard and 64-byte vendor). No compile, flash or hardware test is claimed for the expanded memory probe.
