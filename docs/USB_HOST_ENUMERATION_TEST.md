# USB Host enumeration test firmware

## Scope

This milestone installs the ESP-IDF USB Host Library on the ESP32-P4 High-Speed OTG controller (`peripheral_map = BIT0`) and reports enumerated USB devices. It is an enumeration/descriptor diagnostic, not yet a HID class driver or a gateway data path.

The firmware:
- Installs the USB Host Library.
- Runs the required library daemon and client event tasks.
- Enables external hub support, including multi-level hubs.
- Opens each enumerated device and logs VID, PID, bus speed and active configuration interface count.
- Logs device removal and handles disconnects.
- Does not claim HID interfaces or submit interrupt transfers.
- Does not forward reports to the PC-facing TinyUSB device.
- Does not implement USB Mass Storage Class passthrough.

## Board routing required for this test

**The user has confirmed that the selector/jumper setting is now resolved.** Keep the board in that confirmed setting during the following enumeration diagnosis; do not change H3 while collecting the diagnostic trace. The earlier H3=Host/Device observations below are historical test results, not a request to repeat the selector investigation.

The user's 'USB4' label refers to J8 / USBHSD1, not a fourth USB-A downstream port.

## Build configuration

The standalone HID mouse test mode must be disabled because it deliberately skips `usb_host_init()`.

The repository default is now:

```text
CONFIG_USB_GATEWAY_USB_DEVICE_MOUSE_TEST=n
CONFIG_USB_HOST_HUBS_SUPPORTED=y
CONFIG_USB_HOST_HUB_MULTI_LEVEL=y
```

For the existing Windows build directory, explicitly change the active configuration. Do not rely only on `sdkconfig.defaults`, because an existing `sdkconfig` can retain its previous value.

From an ESP-IDF 6.1 PowerShell environment in the repository root:

```powershell
idf.py -B build/default menuconfig
```

Open **Component config -> USB HID Gateway** and clear **Standalone USB Device HID mouse test mode**. Save and exit. Then build:

```powershell
idf.py -B build/default reconfigure
idf.py -B build/default build
```

Alternatively, edit `build/default/sdkconfig` and set:

```text
CONFIG_USB_GATEWAY_USB_DEVICE_MOUSE_TEST=n
```

Then run `idf.py -B build/default reconfigure` and `idf.py -B build/default build`.

The managed USB Host dependency is `espressif/usb ~1.6.1`. Version 1.6.1 includes ESP32-P4 fixes for channel-error handling and external-hub device cleanup. It does not claim to fix this particular enumeration failure; it is the current diagnostic baseline.

## Flash and monitor

1. Connect Type-C 4 (CH343P) to the PC for flashing and UART output.
2. Do not use Type-C 6 as the flashing connection for this test.
3. Identify the CH343P COM port in Windows Device Manager.
4. Flash and start the monitor:

```powershell
idf.py -B build/default -p COMx flash monitor
```

Replace `COMx` with the actual CH343P port.

At startup, expect log messages similar to:

```text
USB Host diagnostic ready; verify the confirmed board selector setting; enumeration only
USB Host controller: HS OTG operating FS/LS-only for hub compatibility
USB Host client registered; waiting for devices
```

The firmware enables DEBUG logging for the USB Host, enumeration, hub and external-port components before installing the Host Library.

## Hardware test

1. Leave the selector/jumper in the position already confirmed by the user.
2. Use the same known-good USB stick and connect it directly to the failing downstream port, without an extension cable.
3. Capture the complete UART output from before insertion until the port is disabled or enumeration succeeds.
4. Repeat separately for the other failing downstream port; do not move the selector between tests.
5. Record the exact physical port and whether the stick was inserted before or after board power-up.

Expected Logitech identities from the existing captures:
- G815 DE: `046D:C33F`
- G502 HERO: `046D:C08B`

J8 / USBHSD1 was the working positive-control connection in the earlier H3=Host test (user label 'USB4'). The current investigation is focused on the failed EP0 control transfer in the already confirmed selector position.

## Expected serial output

The exact address and VID/PID depend on the USB stick. The important fields are:

```text
Device enumerated, address=<n>
USB device addr=<n> VID=<vid> PID=<pid> speed=FS configuration=<n>
USB configuration addr=<n> interfaces=<n> attributes=<hex>
Device removed, address=<n>
```

A USB stick will usually enumerate as a Full-Speed device in this test because the Host controller is configured with `fsls_only = true`. This forces High-Speed-capable devices to negotiate at Full Speed and is needed for Full-/Low-Speed devices behind a High-Speed hub when the hub path has no Transaction Translator support.

## Power

The USB-A host ports must provide valid 5 V VBUS to the attached device/hub. If the board does not enable host VBUS automatically, the board's VBUS power-switch circuit must be enabled according to the Waveshare schematic. Do not externally inject 5 V into a port that is already powered.

## Acceptance criteria

- [ ] Firmware builds with ESP-IDF 6.1 and the mouse-test option disabled.
- [ ] Host stack starts without an installation error.
- [ ] USB1 enumerates a directly connected USB stick through the active hub path (exact H3 routing under investigation).
- [ ] USB2 enumerates the same USB stick.
- [ ] USB3 enumerates the same USB stick.
- [ ] Disconnect/removal is reported for each test.
- [ ] G815 and G502 enumerate through the USB1–USB3 hub topology once H3 routing is verified.
- [ ] PC-facing TinyUSB Device remains operational at the same time.

## Hardware test result (2026-11-15)

Corrected USB port test result with H3 in Host mode (2026-11-15):

| Physical port | Result |
|---|---|
| USB1 | No successful enumeration observed |
| USB2 | No successful enumeration observed |
| USB3 | No successful enumeration observed |
| J8 / USBHSD1 (user label 'USB4') | Successful enumeration of the USB stick |

The earlier report that USB3 enumerated in H3=Host mode was a user correction error and is superseded. J8 / USBHSD1 (user label 'USB4') is the working positive-control connection for H3=Host. The user's 'USB4' label refers specifically to J8 / USBHSD1, not a USB4 Type-A port or test point.

Earlier additional test with H3 in Device mode (2026-11-15; historical observation, selector investigation subsequently resolved):

- The Host stack reports downstream Port1, Port2 and Port3 activity; Port4 remains inactive.
- Port1 and Port2 do not complete enumeration. The short Device Descriptor control transfer fails with `CHECK_SHORT_DEV_DESC FAILED`, and each port is disabled while still connected.
- This is evidence of hub-port activity, not successful device enumeration.

USB3 UART output:

```text
I (97890) usb_host: Device enumerated, address=1
I (97890) usb_host: USB device addr=1 VID=8564 PID=1000 speed=FS configuration=1
I (97900) usb_host: USB configuration addr=1 interfaces=1 attributes=0x80
I (97900) gateway_diag: Host device + addr=1 VID:PID=8564:1000 manufacturer='' product='' interfaces=1 HID=0
```

The USB Host diagnostic reported `ready`, one device and zero errors. VID:PID `8564:1000` was enumerated at Full Speed with one configuration interface on J8 / USBHSD1 (called 'USB4' by the user) in H3=Host mode. This proves that the Host controller and the selected U15-to-J8 path work for this device. It does not establish the complete H3/U15 routing.

USB1 and USB2 are not yet diagnosed. The current log does not establish whether those ports lack VBUS, have a connector/data-line issue, or whether the hub reports a port-connect event that fails later in enumeration. Do not attribute the failure to a specific component until per-port VBUS and hub-port status are checked.

### Follow-up isolation procedure

1. Use the same USB stick, with no extension cable, and repeat USB4 as the positive control.
2. With the board powered and H3 unchanged in Host mode, move that same stick directly to USB1, wait several seconds, then USB2.
3. Repeat with the stick inserted before board power-up to distinguish hot-plug detection from cold-start behavior.
4. Measure VBUS between each USB-A port's 5 V and GND contacts. Compare USB1/USB2 against the working USB3 under load; do not short adjacent contacts or inject external 5 V.
5. If VBUS is present and stable, inspect the CH334F downstream port routing/connector soldering and capture the full boot and attach log for each port.
6. Record whether the host emits any hub-port connect, reset, enable or disconnect event for USB1/USB2. The current application-level log only reports successfully enumerated child devices and does not yet expose all hub-port state transitions.

Do not change USB Host routing or selector settings during this enumeration diagnosis. The current failure is a USB control-transfer error after the downstream port reports connection/reset activity.


## Current failure analysis: CHECK_SHORT_DEV_DESC (2026-11-15)

The log line:

```text
Bad transfer status 1: CHECK_SHORT_DEV_DESC
```

must not be interpreted as proof that the device returned a descriptor that was too short. `CHECK_SHORT_DEV_DESC` is the USB Host enumeration state that requests the initial 8-byte Device Descriptor header. Transfer status value 1 is `USB_TRANSFER_STATUS_ERROR`; the USB Host API defines this as a transfer failure caused by excessive bus errors, for example no response or CRC errors. The Host stack therefore fails before a valid short descriptor can be parsed and before the application receives `USB_HOST_CLIENT_EVENT_NEW_DEV`.

The firmware now enables DEBUG logs for the `USBH`, `ENUM`, `HUB` and `EXT_PORT` tags before Host installation. This should expose the enumeration sequence, hub-port reset state and lower-level Host errors. It is diagnostic instrumentation, not a workaround.

### Required capture

- Keep the selector/jumper in the already confirmed position.
- Use the same USB stick and cable-free direct connection.
- Capture the complete UART log from boot through the failure, including DEBUG lines.
- Test one physical port at a time and record its connector label.
- Do not change `fsls_only`, PHY selection, reset timing or hub configuration yet; change one variable only after the trace identifies the failing layer.

### Interpretation guide

- `USB_TRANSFER_STATUS_ERROR` (1): low-level transfer error; investigate response/CRC/channel events and signal path.
- `USB_TRANSFER_STATUS_TIMED_OUT` (2): no successful completion before timeout.
- `USB_TRANSFER_STATUS_STALL` (4): device returned STALL; this differs from status 1.
- A successful transfer with an actual length below the expected 16 bytes (8-byte setup packet + 8-byte descriptor) would be a genuine short response and is reported separately by the enumeration code.

The current evidence does not yet distinguish a data-line/signal-integrity problem, hub downstream-port issue, reset/timing problem or controller/driver defect. Do not patch enumeration to accept the error or force a device address: the device has not completed the mandatory initial control transfer.

## CH334F-focused diagnosis (2026-11-15)

The user has identified the CH334F as the only relevant difference between the working direct connection at J8 / USBHSD1 and the failing downstream USB-A ports. The CH334F hub path is therefore the primary fault domain. Do not reopen the H3 selector/jumper investigation.

The CH334F is a USB 2.0 four-port hub controller. The WCH datasheet specifies HS/FS upstream support, HS/FS/LS downstream support and MTT capability. The ESP32-P4 Host is configured with `fsls_only = true`, so the upstream hub link is intentionally negotiated at Full Speed. This avoids the ESP-IDF Host stack's lack of a Transaction Translator layer for Full-/Low-Speed devices behind a High-Speed hub. In this configuration, MTT is not expected to be involved in translating child traffic because the hub's upstream connection is Full Speed.

The supplied log shows that the hub-port driver reaches downstream Port1, Port2 and Port3 and attempts enumeration, but the first child-device EP0 transfer fails. It does not show successful child enumeration. The next investigation must distinguish:

1. CH334F downstream port reset/release behavior and timing.
2. CH334F upstream negotiated speed and hub descriptor/port characteristics.
3. Downstream D+/D- signal integrity and routing from each CH334F DPx/DMx pair to its connector.
4. CH334F RESET#/CDP, 12 MHz clock, V5/VDD33 supply and PSELF power-mode strap.
5. Whether the Host stack is issuing the initial 8-byte GET_DESCRIPTOR(Device) request correctly after the port reset.

### Controlled test sequence

- Keep the confirmed selector setting unchanged.
- Use the same USB stick that successfully enumerates directly at J8 / USBHSD1.
- First confirm that the same stick still enumerates directly at J8 with the current firmware.
- Then connect it to one CH334F downstream port at a time and capture the full log.
- Compare the CH334F port LED/state and the exact reset-to-first-control-transfer timing across ports.
- If possible, capture D+/D- at the CH334F downstream port during reset and the first GET_DESCRIPTOR request. Verify reset release, Full-Speed idle/J state and device response/K/handshake activity.
- Measure CH334F V5 and VDD33 at the IC, not only USB-A VBUS. Check RESET# is high after power-up and the 12 MHz oscillator is running.
- Verify the CH334F PSELF strap against the board schematic and actual power topology; the WCH datasheet says PSELF is sampled during reset and defaults high (self-powered) unless pulled low.

Do not change Host reset delays, PHY selection, `fsls_only`, or the hub driver yet. First obtain the hub-side link state and one downstream D+/D- capture. The existing `USBH: Dev 0 EP 0 Error` message alone is not enough to justify a firmware workaround.

Reference: WCH CH334/CH335 datasheet, especially overview, CH334F pinout, PSELF definition, reset behavior and hub repeater/TT architecture: https://285624.selcdn.ru/syms1/iblock/d35/d35ceb2ab15bd757047c631a70c4f3e6/CH334DS1.PDF

## Not implemented in this milestone

- USB Mass Storage Class driver and volume mounting.
- SCSI command handling and Bulk-Only Transport.
- PC-facing USB Device MSC interface.
- USB-stick block-device passthrough to the PC.
- HID interface discovery and claiming.
- HID report descriptor retrieval.
- Interrupt-IN report transfers.
- Generic HID parsing from live devices.
- Host-to-Device report forwarding.
- USB vendor/control-transfer proxying or G HUB compatibility.

## References

- [Waveshare USB routing assessment](WAVESHARE_USB_ROUTING.md)
- [ESP-IDF ESP32-P4 USB Host documentation](https://docs.espressif.com/projects/esp-usb/en/latest/esp32p4/usb_host.html)
- [Espressif USB Host Library source](https://github.com/espressif/esp-usb/blob/master/host/usb/include/usb/usb_host.h)
- [ESP Component Registry: espressif/usb 1.6.1](https://components.espressif.com/components/espressif/usb/versions/1.6.0)


## Device-dependent Full-Speed enumeration comparison (2026-11-15)

A new positive result changes the fault assessment:

- Device `090C:1000` successfully enumerated through the CH334F at Full Speed.
- Device `8564:1000` previously failed at the initial child-device EP0 transfer (`CHECK_SHORT_DEV_DESC`) through the CH334F, while it successfully enumerated directly at J8 / USBHSD1.
- Both are reported as Full-Speed devices. A general CH334F Full-Speed forwarding failure and a purely Low-Speed-specific problem are therefore unlikely. The remaining possibilities include EP0 descriptor/packetization differences, device response timing, electrical margin, or a device-specific hub/host interaction.

The application now logs EP0 maximum packet size and USB revision for devices that complete enumeration:

```text
USB device addr=<n> VID=<vid> PID=<pid> speed=FS EP0_MPS=<n> bcdUSB=0x<xxxx> configuration=<n>
```

**Important limitation:** This log is emitted only after enumeration completes and `USB_HOST_CLIENT_EVENT_NEW_DEV` is delivered. It cannot report `bMaxPacketSize0` for a device that fails during the initial 8-byte descriptor request. The initial payload and low-level transfer completion must be captured inside the enumeration driver/HCD or with a USB protocol analyzer.

### Controlled A/B/A test

Use the same CH334F downstream connector, same cable-free setup, unchanged firmware and the same board power-cycle procedure:

| Test | Device | Known result |
|---|---|---|
| A | `090C:1000` | Successful Full-Speed enumeration through CH334F |
| B | `8564:1000` | Previously failed at `CHECK_SHORT_DEV_DESC` through CH334F |
| C | `090C:1000` | Repeat A after B to detect order/state dependence |
| D | `8564:1000` | Direct at J8 / USBHSD1 as positive control |

For each run, capture the complete UART log from before insertion until success/failure. Record the physical downstream port, insertion order and whether the board was power-cycled between devices. Do not change H3, `fsls_only`, PHY selection, reset delays or hub configuration.

### Values to compare

For successful enumeration of `090C:1000`, record `bMaxPacketSize0` (logged as `EP0_MPS`), `bcdUSB`, speed, VID/PID and configuration interface count.

For failed enumeration of `8564:1000`, the application cannot see the descriptor bytes. To compare the first EP0 transaction, capture or instrument:

- Setup packet: `bmRequestType=0x80`, `bRequest=0x06` (GET_DESCRIPTOR), `wValue=0x0100` (Device Descriptor), `wIndex=0`, `wLength=8`.
- Transfer completion status and actual received data length.
- The eight returned descriptor bytes, if any.
- Whether the device responds with DATA, NAK, STALL or no response.
- Timing from downstream reset completion / port enable to the first SETUP token.

Do not infer a `bMaxPacketSize0` mismatch from `CHECK_SHORT_DEV_DESC` alone. The state name does not prove that a short descriptor was received. A status-1 transfer error does not expose the actual descriptor contents.

### Interpretation

- If both devices report the same EP0 MPS on direct connection but only one fails behind the hub, investigate response timing, electrical signal margin and hub/host interaction before changing EP0 sizing.
- If the initial 8-byte response from `8564:1000` is absent or corrupted only behind the CH334F, inspect downstream D+/D- and hub forwarding.
- If the failure follows the device even on direct J8, investigate device behavior or the Host controller path rather than attributing it to the CH334F.
- If A succeeds, B fails, then C succeeds without a power cycle, investigate hub-port/device state recovery and order-dependent behavior.
- Do not patch the enumeration driver to accept a failed descriptor transfer or guess an EP0 packet size.

If UART logs remain insufficient, the next firmware-level step is temporary instrumentation in the ESP-IDF enumeration path around the initial short Device Descriptor control transfer. Keep this instrumentation diagnostic until the exact completion status, actual length and returned bytes are known.

## Intermittent enumeration and EP0 comparison log (2026-11-15)

The latest UART capture demonstrates that both previously discussed Full-Speed devices can successfully enumerate through the CH334F:

- `090C:1000`: successful at address 2, then a later enumeration attempt failed at `CHECK_SHORT_DEV_DESC`, then successful again at address 3.
- `8564:1000`: successful at address 4.
- Both successful enumerations report `speed=FS`, `EP0_MPS=64`, `configuration=1`, one interface.
- `bcdUSB` differs: `0x0210` for `090C:1000`, `0x0200` for `8564:1000`.

This supersedes the earlier assumption that `8564:1000` consistently fails behind the CH334F. The failure is intermittent in the supplied capture, and both devices are demonstrably capable of successful enumeration through the hub. A general Full-Speed hub forwarding failure, a Low-Speed-only issue, and an EP0 maximum-packet-size mismatch between these two devices are not supported by this evidence.

The `Device removed` messages at addresses 2, 3 and 4 are normal for the observed detach events. The failed attempt is reported at address 0 because it did not complete enumeration and receive an address. `USB Host has released all device resources` indicates cleanup after detach; it is not itself an enumeration failure.

### Remaining diagnostic question

The capture does not unambiguously label which physical device was connected at the exact time of the failed attempt at 23.940 s. Future captures must annotate each insertion/removal with the device VID/PID or a manual timestamp marker. Repeat a controlled sequence on the same downstream port: `090C:1000` (A), `8564:1000` (B), `090C:1000` (A), with the complete UART log and explicit device identity for every insertion.

Because a failure occurred between successful enumerations, prioritize intermittent port/hub state, reset/enable sequencing, connection debounce, and transfer-level timing over static descriptor differences. Do not change reset delays or hub/PHY settings yet.

The application-level `EP0_MPS` and `bcdUSB` fields are available only after successful enumeration. To diagnose the failed attempt, instrument the initial enumeration control transfer in ESP-IDF to record setup request, HCD transfer status, actual received length, returned bytes and timing relative to port reset completion. `CHECK_SHORT_DEV_DESC` alone does not establish that a short descriptor was actually received.

## Forced re-detection by brief contact interruption (2026-11-15)

The user reports a new observation on the CH334F hub path: a very brief interruption of the device contacts can force a new detection attempt.

UART excerpt:

```text
E (207970) USBH: Dev 0 EP 0 Error
E (207970) ENUM: Bad transfer status 1: CHECK_SHORT_DEV_DESC
E (207970) ENUM: CHECK_SHORT_DEV_DESC FAILED
W (207970) EXT_PORT: Port3 disabled while connected, reset attempts=1
I (207970) usb_host: Device removed, address=0
...
I (212600) usb_host: Device enumerated, address=9
I (212600) usb_host: USB device addr=9 VID=8564 PID=1000 speed=FS EP0_MPS=64 bcdUSB=0x0200 configuration=1
I (212600) usb_host: USB configuration addr=9 interfaces=1 attributes=0x80
```

### Established observations

- The test was performed through the hub, on downstream Port3.
- The first enumeration attempt failed during the initial Device Descriptor control transfer.
- The hub port was reported disabled while the device was still connected; the Host then reported removal at address 0.
- After the brief contact interruption used to force re-detection, the test sequence reached successful enumeration at address 9.
- The successful device identity is 8564:1000, Full Speed, EP0_MPS=64, bcdUSB=0x0200, one configuration and one interface.
- The success log occurs 4.63 seconds after the failed attempt. This is the interval between the two logged events, not a measurement of the physical contact interruption duration.

### Updated interpretation

This confirms that 8564:1000 is capable of successful enumeration through the CH334F. It is not a fixed device/hub incompatibility. The fact that a brief physical disconnect/reconnect can trigger successful enumeration shifts the leading investigation toward connection detection, port state recovery, debounce/reset sequencing or retry behavior around the first EP0 transfer.

The UART excerpt alone does not prove which electrical contact was interrupted, the exact interruption duration, or whether the successful attempt was initiated by a fresh hub connect event versus an internal retry. Do not yet change firmware reset delays or hub settings.

### Next diagnostic capture

If repeating this observation, record:
- Exact physical hub port (this excerpt identifies Port3).
- Whether the device is 8564:1000 for every attempt.
- Approximate duration of the contact interruption.
- Whether the interruption is on VBUS, D+, D-, or a complete connector disconnect (do not intentionally short pins).
- The complete UART trace around the disconnect, port-change event, reset, port-enable and first EP0 request.

The most useful firmware instrumentation is the hub downstream port status/change bitmap and timestamps for disconnect/connect, reset start/completion, port enable and first GET_DESCRIPTOR(Device) SETUP. Keep the current Host PHY, fsls_only, hub configuration and reset timing unchanged until this sequence is visible.
