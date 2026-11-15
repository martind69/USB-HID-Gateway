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

**Do not assume an H3 position from the previous routing description.** The earlier statement that H3=Host is the verified route for USB1–USB3 has been withdrawn: physical testing has shown hub-port activity on USB1–USB3 with H3=Device, while the corrected H3=Host test successfully enumerated the stick only on USB4. The complete U15-to-U14/J8 net mapping is still under investigation.

The user has reported successful USB-stick enumeration at the location designated USB4. Verify its physical connector designation against the exact board revision; do not assume it is only a test point.

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

The managed USB Host dependency is pinned to `espressif/usb ~1.6.0`. This component release requires ESP-IDF 5.5.3 or later; the user's ESP-IDF 6.1 environment satisfies that minimum.

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
USB Host diagnostic ready; verify H3 routing against the current hardware test notes; enumeration only
USB Host controller: HS OTG operating FS/LS-only for hub compatibility
USB Host client registered; waiting for devices
```

The firmware cannot electrically detect the H3 jumper position. Any H3 mode in a startup log is an instruction only, not a jumper-state measurement.

## Hardware test

1. Record the H3 position used for this test. Until the routing discrepancy is resolved, test both positions separately and do not infer the route from the silkscreen label alone.
2. Confirm that the board's USB-A VBUS supply is enabled and provides 5 V.
3. Connect one known-good USB stick to USB1.
4. Wait for the device enumeration log. Record VID, PID, speed and interface count.
5. Disconnect the stick and confirm the removal/disconnect log.
6. Repeat the same stick on USB2 and USB3, one port at a time.
7. If all three ports work, test the Logitech G815 and G502 through the intended USB1–USB3 hub path.

Expected Logitech identities from the existing captures:
- G815 DE: `046D:C33F`
- G502 HERO: `046D:C08B`

J8 / USBHSD1 is an alternate U15 branch according to the schematic interpretation; its relation to observed hub-port activity remains unresolved. USB4 is the working port in the corrected H3=Host test; verify its exact physical connector designation.

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
| USB4 | Successful enumeration of the USB stick |

The earlier report that USB3 enumerated in H3=Host mode was a user correction error and is superseded. USB4 is the working positive-control port for H3=Host. The user's 'USB4' label refers specifically to J8 / USBHSD1, not a USB4 Type-A port or test point.

Additional test with H3 in Device mode (2026-11-15):

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

Do not change USB Host firmware or mux routing solely on the basis of these observations. J8 / USBHSD1 (user label 'USB4') is the proven positive control in the corrected H3=Host test. The H3-to-port mapping must first be reconciled with the complete schematic nets and physical continuity measurements.

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
- [ESP Component Registry: espressif/usb 1.6.0](https://components.espressif.com/components/espressif/usb/versions/1.6.0)
