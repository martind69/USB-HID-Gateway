# USB HID Gateway Architecture

## Project modules

- `usb_host.c`: Downstream USB Host controller, hub and device enumeration.
- `usb_device.c`: PC-facing USB Device controller and HID functions.
- `hid_parser.c`: HID descriptor parsing, report IDs, usages and bit offsets.
- `hid_proxy.c`: Queued HID report forwarding between Host and Device.
- `hotkey.c`: Hotkey detection and operating-mode control.
- `mapping.c`: Mapping of input events to logical outputs.
- `dac_output.c`: SPI DAC interface for 4–6 analog channels.
- `gpio_output.c`: Control of 12 digital outputs.
- `usb_gateway.c`: Module initialization and processing coordinator.

## Data path

1. USB Host enumerates downstream devices (keyboard, integrated hub, mouse).
2. HID descriptor parser discovers interfaces, report IDs, fields and bit offsets.
3. Input router applies passthrough or hotkey-mode policy.
4. Mapping layer forwards selected values to 4–6 SPI DAC channels and 12 digital outputs.
5. USB Device presents a PC-facing composite HID device and forwards compatible reports.

## Design constraints

- USB Host and USB Device use independent controller instances.
- Preserve report bits and fields not explicitly modified by the gateway.
- Keep physical mouse XY data internally; hotkey mode may emit synthetic XY to the PC.
- Keep keyboard reports compatible with Logitech G HUB where possible.
- Vendor-specific control transfers and report formats require hardware validation.
- USB callbacks must not block; queue events for processing in task context.

## Milestones

1. Confirm ESP32-P4 board, USB connectors and ESP-IDF version.
2. Build a minimal ESP-IDF application.
3. Verify PC-facing USB HID Device enumeration.
4. Verify Host enumeration through the downstream hub.
5. Implement descriptor-driven HID report forwarding.
6. Validate G HUB vendor-specific behavior.
7. Add hotkey routing, mappings, DAC and GPIO outputs.
