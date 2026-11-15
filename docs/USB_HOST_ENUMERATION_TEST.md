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
