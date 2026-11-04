# USB HID Gateway Architecture

## Data path

1. USB Host enumerates downstream devices (keyboard, integrated hub, mouse).
2. HID descriptor parser discovers interfaces, report IDs, fields, and bit offsets.
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

1. Confirm ESP32-P4 board, USB connectors, and ESP-IDF version.
2. Build a minimal ESP-IDF application.
3. Verify PC-facing USB HID Device enumeration.
4. Verify Host enumeration through the downstream hub.
5. Implement descriptor-driven HID report forwarding.
6. Validate G HUB vendor-specific behavior.
7. Add hotkey routing, mappings, DAC, and GPIO outputs
