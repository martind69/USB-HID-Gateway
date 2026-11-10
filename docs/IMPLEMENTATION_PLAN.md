# USB HID Gateway – Implementation Plan

Status: Planned. A milestone is complete only after its stated verification criteria pass. The current repository contains a software skeleton and design documents; no firmware build or hardware milestone is claimed as complete.

## Principles

- Verify the USB hardware topology before implementing the transport.
- Keep the HID parser independent of ESP-IDF and USB hardware.
- Build and test in small increments.
- Preserve raw descriptors and reports; interpret only what is needed.
- Do not claim passthrough or G HUB compatibility until tested on the actual hardware.
- Use bounded resources and explicit error handling.
- Keep USB callbacks non-blocking and move parsing/routing work to task context.

## Milestone 0 – Confirm hardware and toolchain

Tasks:

1. Identify exact ESP32-P4 development board and revision.
2. Inspect schematic and map both USB controller instances to accessible connectors/pins.
3. Confirm concurrent Host and Device operation for that board and ESP-IDF release.
4. Select and record the ESP-IDF version and toolchain.
5. Verify Host VBUS power, protection and connector wiring.

Verification:

- A checked-in hardware note identifies board revision, connector mapping and controller assignment.
- A minimal ESP-IDF project builds for the selected target.
- No assumption about connector/controller mapping remains unverified.

## Milestone 1 – USB Device proof of concept

Tasks:

1. Initialize the PC-facing USB Device controller.
2. Expose a minimal test HID interface.
3. Handle enumeration and basic HID class requests.
4. Send a known test report to the PC.

Verification:

- PC enumerates the ESP32-P4 device reliably.
- The test HID interface is recognized.
- Reports are received correctly over repeated reconnects.

## Milestone 2 – USB Host and hub enumeration

Tasks:

1. Initialize the independent Host controller.
2. Enumerate a directly connected test HID device.
3. Add hub support and enumerate the G810 integrated hub.
4. Enumerate the G502 behind that hub.
5. Track device and interface lifecycle, including disconnect/reconnect.

Verification:

- G810, its hub and G502 are discovered.
- Every HID interface and endpoint is identified.
- Disconnecting one downstream device does not corrupt the state of another.

## Milestone 3 – Universal HID parser

Tasks:

1. Define opaque per-interface parser contexts, status codes and centralized capacity limits.
2. Implement bounded HID item decoding, including short and long items.
3. Implement global/local state and Push/Pop handling.
4. Implement collection tracking.
5. Calculate independent report bit offsets for Input, Output and Feature reports per Report ID.
6. Store fields, usages, flags and report layouts.
7. Implement checked bit-level read/write helpers and signed conversion.
8. Add host-side unit tests and fuzz malformed descriptors.

Verification:

- Test descriptors cover mouse, keyboard, multiple Report IDs, Output/Feature reports, variable-width fields, signed values, arrays, collections and malformed input.
- Read-modify-write tests prove unrelated bits remain unchanged.
- Capacity exhaustion and malformed descriptors return deterministic errors.
- Parser tests run without ESP-IDF or USB hardware.

Detailed requirements are in [HID parser design](HID_PARSER_DESIGN.md).

## Milestone 4 – Raw HID report proxy

Tasks:

1. Create per-interface report queues and routing metadata.
2. Forward supported reports unchanged from Host to Device.
3. Preserve Report IDs and report lengths.
4. Handle backpressure, queue overflow and disconnects.
5. Keep report processing out of time-critical USB callbacks.

Verification:

- Captured reports match at ingress and egress in passthrough mode.
- Sustained input does not cause unbounded memory growth.
- Queue overflow and device removal are handled deterministically.

## Milestone 5 – PC-facing descriptors and compatibility

Tasks:

1. Select the PC-facing HID interface/composite-device strategy.
2. Expose compatible keyboard and mouse report descriptors.
3. Implement required standard and HID class control requests.
4. Identify and implement required vendor-specific transactions.
5. Validate G HUB operation with the actual G810/G502 setup.

Verification:

- PC recognizes keyboard and mouse interfaces.
- Keyboard and mouse functions work through the gateway.
- Required G HUB functions are demonstrated, not assumed.
- Unsupported vendor-specific behavior is documented.

## Milestone 6 – Hotkey mode and mouse report modification

Tasks:

1. Implement configurable hotkey detection from parsed keyboard reports.
2. Ensure hotkey detection does not suppress normal keyboard forwarding.
3. Add mode-dependent mouse report routing.
4. Retain physical X/Y values internally.
5. Replace only X/Y fields with synthetic/jitter values in hotkey mode.
6. Continue forwarding buttons, wheel and unrelated report bits.

Verification:

- Passthrough mode forwards mouse reports unchanged.
- Hotkey mode changes only the intended X/Y fields.
- Physical X/Y remains available internally.
- Button and wheel operation remains correct.
- Mode transitions do not leave stuck keys or buttons.

## Milestone 7 – DAC and GPIO mapping

Tasks:

1. Select the external SPI DAC and document electrical requirements.
2. Implement 4–6 analog output channels.
3. Assign and configure 12 digital GPIO outputs.
4. Define mapping configuration, scaling, limits and update rates.
5. Add safe initialization and output states.

Verification:

- Each output is independently addressable.
- Analog values and digital states match the configured mapping.
- Invalid channel/output indices are rejected.
- Startup, reset and fault behavior are safe and deterministic.

## Milestone 8 – Integration and robustness

Tasks:

1. Run simultaneous Host and Device traffic.
2. Test keyboard, mouse, hotkey and outputs together.
3. Exercise disconnect/reconnect and malformed report conditions.
4. Measure latency, queue usage, CPU load and memory usage.
5. Document known limitations and validated peripheral combinations.

Verification:

- No data corruption under sustained operation.
- No unbounded resource growth.
- Recovery from expected disconnect and error cases is demonstrated.
- All claimed compatibility is backed by repeatable tests.

## Current repository status

| Area | Status |
|---|---|
| ESP-IDF project skeleton | Present |
| Module separation | Present |
| Project requirements | Documented |
| USB topology | Documented logically; board routing open |
| HID parser design | Documented; implementation pending |
| USB Host | Placeholder |
| USB Device | Placeholder |
| HID proxy | Placeholder |
| Hotkey detection | Mode state only; detection pending |
| Mapping / DAC / GPIO | Placeholders |
| Firmware build | Not verified |
| Hardware validation | Not started |
