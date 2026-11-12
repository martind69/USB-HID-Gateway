# USB HID Gateway Architecture

## Project modules

- `usb_host.c`: Downstream USB Host controller, monitor-hub and device enumeration.
- `usb_device.c`: PC-facing USB Device controller and HID functions.
- `hid_parser.c`: HID descriptor parsing, report IDs, usages and bit offsets.
- `hid_proxy.c`: Queued HID report forwarding between Host and Device.
- `hotkey.c`: Hotkey detection and operating-mode control.
- `mapping.c`: Mapping of input events to logical outputs.
- `dac_output.c`: SPI DAC interface for 4–6 analog channels.
- `gpio_output.c`: Control of 12 digital outputs.
- `usb_gateway.c`: Module initialization and processing coordinator.

## Physical USB topology

The current observed peripheral setup is:

- The ASUS ROG monitor contains a four-port Genesys Logic USB hub (`05E3:0610`).
- The ASUS Aura HID function (`0B05:18DA`), Logitech G502 HERO mouse (`046D:C08B`) and Logitech G815 keyboard (`046D:C33F`) are connected as separate downstream devices on that monitor hub.
- The G502 is physically connected through the G815's USB pass-through cable. The pass-through is a direct cable path, not an additional hub. The Windows USB tree therefore shows the G502 and G815 as sibling devices on the monitor hub.
- The ASUS Aura function is a separate vendor-defined HID device. It is not part of the Logitech keyboard/mouse path and is outside the initial HID forwarding scope.

The ESP32-P4 Host must enumerate the monitor hub and its downstream devices. It must not model the G815 as a hub or the G502 as a USB child of the G815. See [USB topology](USB_TOPOLOGY.md) and [USB device enumeration](hardware/USB_DEVICE_ENUMERATION.md).

## Data path

1. USB Host enumerates the monitor's Genesys Logic hub and downstream HID devices.
2. HID descriptor parser discovers interfaces, report IDs, fields and bit offsets independently for each interface.
3. Input router applies passthrough or hotkey-mode policy.
4. Mapping layer forwards selected values to 4–6 SPI DAC channels and 12 digital outputs.
5. USB Device presents a PC-facing composite HID device and forwards compatible reports.

## Design constraints

- USB Host and USB Device use independent controller instances; simultaneous operation must be verified on the selected ESP32-P4 board and ESP-IDF release.
- Preserve report bits and fields not explicitly modified by the gateway.
- Keep physical mouse X/Y data internally; hotkey mode may emit synthetic X/Y to the PC.
- Keep keyboard reports compatible with Logitech G HUB where possible, but do not claim compatibility before end-to-end hardware validation.
- Vendor-specific control transfers and report formats require hardware validation. Selected G815 RGB/profile and G502 DPI/profile/lighting transactions have been captured; see the device-specific protocol documents.
- Generic HID passthrough and vendor-specific control-transfer transparency are separate requirements. Do not assume that forwarding HID interrupt reports alone preserves G HUB functionality.
- USB callbacks must not block; queue events for processing in task context.

## Milestones

1. Confirm ESP32-P4 board, USB connectors and ESP-IDF version.
2. Build a minimal ESP-IDF application.
3. Verify PC-facing USB HID Device enumeration.
4. Verify Host enumeration through the monitor's downstream hub.
5. Implement descriptor-driven HID report forwarding.
6. Validate G HUB vendor-specific behavior with the G815/G502.
7. Add hotkey routing, mappings, DAC and GPIO outputs.
