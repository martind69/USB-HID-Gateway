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

Set H3 to **Host** mode. According to the Waveshare schematic truth table, H3 controls U15 (FSUSB42UMX):

- H3 Host: U15 routes USB DM/DP to U14, the CH334F hub. This is the required route for USB1, USB2 and USB3.
- H3 Device: U15 routes USB DM/DP to J8 (USBHSD1_N / USBHSD1_P). This is not the required route for the USB-A hub-port test.

USB4 is only a test point (TP), not a normal USB-A receptacle.

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
USB Host diagnostic ready: H3=HOST, use USB1-USB3; enumeration only
USB Host controller: HS OTG operating FS/LS-only for hub compatibility
USB Host client registered; waiting for devices
```

The firmware cannot electrically detect the H3 jumper position. The H3=HOST log is a test instruction, not a jumper-state measurement.

## Hardware test

1. Set H3 to **Host** mode.
2. Confirm that the board's USB-A VBUS supply is enabled and provides 5 V.
3. Connect one known-good USB stick to USB1.
4. Wait for the device enumeration log. Record VID, PID, speed and interface count.
5. Disconnect the stick and confirm the removal/disconnect log.
6. Repeat the same stick on USB2 and USB3, one port at a time.
7. If all three ports work, test the Logitech G815 and G502 through the intended USB1–USB3 hub path.

Expected Logitech identities from the existing captures:
- G815 DE: `046D:C33F`
- G502 HERO: `046D:C08B`

J8 / USBHSD1 is the alternate U15 Device-mode branch and is not used in this Host-mode test. USB4 is a test point, not a socket.

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
- [ ] USB1 enumerates a directly connected USB stick through U14/CH334F.
- [ ] USB2 enumerates the same USB stick.
- [ ] USB3 enumerates the same USB stick.
- [ ] Disconnect/removal is reported for each test.
- [ ] G815 and G502 enumerate through the actual USB1–USB3 hub topology.
- [ ] PC-facing TinyUSB Device remains operational at the same time.

## Hardware test result (2026-11-15)

First USB-A port test with H3 in Host mode:

| Physical port | Result |
|---|---|
| USB1 | No successful enumeration observed |
| USB2 | No successful enumeration observed |
| USB3 | Successful enumeration |

USB3 UART output:

```text
I (97890) usb_host: Device enumerated, address=1
I (97890) usb_host: USB device addr=1 VID=8564 PID=1000 speed=FS configuration=1
I (97900) usb_host: USB configuration addr=1 interfaces=1 attributes=0x80
I (97900) gateway_diag: Host device + addr=1 VID:PID=8564:1000 manufacturer='' product='' interfaces=1 HID=0
```

The USB Host diagnostic reported `ready`, one device and zero errors. VID:PID `8564:1000` was enumerated at Full Speed with one configuration interface. This proves that the Host controller, H3 Host routing, U15-to-U14 upstream path and at least one CH334F downstream port (USB3) work for this device.

USB1 and USB2 are not yet diagnosed. The current log does not establish whether those ports lack VBUS, have a connector/data-line issue, or whether the hub reports a port-connect event that fails later in enumeration. Do not attribute the failure to a specific component until per-port VBUS and hub-port status are checked.

### Follow-up isolation procedure

1. Use the same USB stick, with no extension cable, and repeat USB3 as the positive control.
2. With the board powered and H3 unchanged in Host mode, move that same stick directly to USB1, wait several seconds, then USB2.
3. Repeat with the stick inserted before board power-up to distinguish hot-plug detection from cold-start behavior.
4. Measure VBUS between each USB-A port's 5 V and GND contacts. Compare USB1/USB2 against the working USB3 under load; do not short adjacent contacts or inject external 5 V.
5. If VBUS is present and stable, inspect the CH334F downstream port routing/connector soldering and capture the full boot and attach log for each port.
6. Record whether the host emits any hub-port connect, reset, enable or disconnect event for USB1/USB2. The current application-level log only reports successfully enumerated child devices and does not yet expose all hub-port state transitions.

Do not change USB Host firmware or mux routing solely on the basis of this first port comparison. USB3 is a proven positive control.

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
