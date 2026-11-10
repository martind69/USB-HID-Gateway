# USB HID Gateway – Project Requirements

Status: Requirements baseline. Items marked **Open** require a hardware or design decision; they must not be treated as implemented.

## 1. Objective

Build an ESP32-P4-based USB gateway between a PC and USB HID peripherals. The gateway has two independent USB roles:

- PC-facing USB Device.
- Peripheral-facing USB Host.

Normal operation should behave as a transparent HID passthrough wherever the USB topology, descriptors, endpoint behavior and control requests permit it. A second operating mode allows selected mouse report data to be modified while retaining the original physical input internally.

The first target setup is:

- Logitech G815 keyboard.
- Logitech G502 mouse connected through the G815 USB pass-through connection.
- Both USB connections terminate at separate downstream ports of the USB hub integrated into the ASUS ROG monitor.
- PC connected to the USB Device side of the gateway.

The G815 pass-through is not a hub. The monitor's Genesys Logic hub is the actual downstream hub that the ESP32-P4 Host must enumerate.

This is a target configuration, not a claim that Logitech G HUB compatibility has already been achieved.

## 2. Operating modes

### 2.1 Passthrough mode

- Forward supported keyboard and mouse HID reports from Host to Device.
- Preserve report IDs, report lengths and all report bits that are not intentionally modified.
- Preserve keyboard functionality and, where technically possible, Logitech G HUB functionality.
- Do not apply synthetic mouse movement in this mode.
- Unknown report fields must not be discarded merely because the firmware does not interpret them.

### 2.2 Hotkey mode

- Detect a configurable keyboard hotkey and change the gateway operating mode.
- Continue forwarding keyboard reports so normal keyboard operation is retained.
- Continue forwarding mouse buttons and wheel unless a later explicit mapping rule says otherwise.
- Retain the physical mouse X/Y values internally.
- Send the configured synthetic/jitter X/Y values to the PC instead of the physical X/Y values.
- Modify only the intended X/Y bit fields; preserve all other report bits.

The exact hotkey combination, synthetic movement algorithm, amplitude, timing and activation feedback are **Open**.

## 3. HID handling

- Parse HID report descriptors dynamically; do not hard-code the G810 or G502 report layout.
- Support multiple HID interfaces per USB device.
- Support multiple Report IDs and Input, Output and Feature reports.
- Handle variable-width and signed fields, fields crossing byte boundaries, usage ranges, collections and nested global state.
- Preserve original descriptor bytes.
- Keep descriptor parsing, semantic interpretation, report modification and USB transport as separate responsibilities.
- Forward reports unchanged when no modification is required.
- Preserve unknown fields and report bits during selective modifications.
- Use bounded resources and report explicit parser errors; never silently continue with a truncated descriptor model.

See [HID parser design](HID_PARSER_DESIGN.md) for the detailed parser specification.

## 4. USB behavior and compatibility

- Use independent ESP32-P4 USB controller instances for Host and Device roles.
- Host side must enumerate the monitor's Genesys Logic hub and its downstream devices.
- The G815 pass-through must not be modeled as an additional hub or USB tier.
- The ASUS ROG monitor Aura function is a separate vendor-defined HID device on the same hub; enumerate and isolate it from the Logitech keyboard/mouse report path.
- Device side must expose a PC-compatible HID interface configuration and handle the required enumeration and control-transfer behavior.
- The PC-facing descriptor/interface strategy must be compatible with the downstream devices' reports.
- A report parser alone does not provide transparent USB forwarding. Device identity, interface topology, endpoint characteristics, class requests and vendor-specific control transfers must be considered separately.
- Logitech G HUB uses functionality beyond ordinary generic mouse/keyboard input in some configurations. Its vendor-specific reports and control requests must be captured and validated on the actual hardware; compatibility must not be assumed.

Exact ESP-IDF version, board revision, connector routing, endpoint limits and descriptor strategy are **Open**.

## 5. Output mapping

The gateway is intended to expose:

- 4–6 analog output channels using an external SPI DAC.
- 12 digital GPIO outputs.

The mapping layer converts selected HID input values/events into logical output values. Mapping configuration, scaling, ranges, update rates, DAC part number, DAC resolution, output voltage range and GPIO pin assignment are **Open**.

## 6. Software and development constraints

- Target MCU: ESP32-P4.
- Framework: ESP-IDF, C and FreeRTOS.
- Development environment: VS Code with Espressif ESP-IDF extension.
- Keep modules separated by responsibility.
- USB callbacks must remain short and non-blocking; queue work for task context.
- Parse descriptors in task context during enumeration, not in time-critical USB callbacks.
- Prefer bounded queues and explicit capacity/error handling.
- Keep the parser independently testable on a host computer without ESP-IDF or USB hardware.
- Source formatting: tabs for indentation, tab width 4; comments in English.

The exact ESP-IDF release and compiler/toolchain version are **Open**.

## 7. Target hardware and validation

The initial validation hardware is the G810/G502 combination described above. Tests must cover:

- Enumeration of the monitor's Genesys Logic hub.
- Enumeration of the G815 and G502 on separate monitor hub ports, including the G502 connection through the G815 pass-through cable.
- Enumeration and isolation of the monitor's ASUS Aura HID function.
- PC-side enumeration of the gateway.
- Keyboard and mouse passthrough.
- Report IDs and multiple HID interfaces.
- Hotkey mode switching.
- Physical mouse X/Y capture while synthetic X/Y is sent to the PC.
- Button and wheel forwarding during hotkey mode.
- G HUB behavior, including vendor-specific traffic.
- DAC and GPIO output mapping.

No compatibility or performance target is considered met until it has been tested on the actual board and peripherals.

## 8. Explicit non-goals for the initial milestone

- Full USB proxy support for arbitrary non-HID devices.
- Guaranteed support for every vendor-specific peripheral protocol.
- Reconstructing all downstream USB descriptors from a normalized HID model.
- Implementing a complete Logitech configuration-software emulator.

## 9. Open decisions

1. Exact ESP32-P4 board and hardware revision.
2. Physical connector and USB controller mapping for Host and Device.
3. ESP-IDF version.
4. PC-facing USB descriptor and composite-interface strategy.
5. Hotkey combination and mode feedback.
6. Synthetic mouse movement algorithm and timing.
7. External DAC part, resolution and analog output requirements.
8. GPIO pin allocation and electrical characteristics.
9. Required G HUB features and the vendor-specific transactions they use.


## 10. Observed peripheral enumeration (2026-11-09)

A complete Windows USB device-tree/HID capability capture is available for the monitor hub, its ASUS Aura HID function, the Logitech G502 HERO and the Logitech G815:

- Logitech G502 HERO: `VID:PID 046D:C08B`, revision `0x2703`.
- Logitech G815: `VID:PID 046D:C33F`, revision `0x3102`.

The monitor contains a four-port Genesys Logic USB hub (`05E3:0610`), with a paired SuperSpeed companion hub (`05E3:0620`). The G502 and G815 were enumerated at Full-Speed on sibling port chains (`2-7-3` and `2-7-4`). This is consistent with the G502 being connected through the G815 pass-through: the pass-through is a cable path, not a hub, and therefore creates no additional USB hub tier. The ASUS Aura HID function is on port `2-7-1`; port `2-7-2` is empty.

The G502 mouse collection reports 16 buttons, signed relative 16-bit X/Y, and signed 8-bit wheel and AC Pan fields. Both Logitech devices expose vendor-defined HID report collections. The monitor's ASUS Aura function is a separate vendor-defined HID interface with 64-byte interrupt IN and OUT endpoints.

Raw HID report descriptor bytes and actual report/control-transfer payloads were not captured. The G815 is the observed keyboard; the former G810 target label must not be used for this hardware capture.

See [USB device enumeration](hardware/USB_DEVICE_ENUMERATION.md) for the detailed tables, limitations and follow-up capture checklist. These observations do not establish G HUB compatibility.
