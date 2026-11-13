# Input mapping and report-routing interface

Status: first firmware interface slice; not yet connected to the HID parser or USB transports.

## Routing contract

The gateway uses a consume-if-mapped policy in Proxy mode:

- Passthrough: forward the original report payload byte-for-byte.
- Proxy: neutralize only configured semantic fields.
- Unmapped fields, unknown fields, report padding and all unrelated bits remain unchanged.
- A mapped Variable field is consumed for both press and release states.
- A mapped Array field is consumed only while its parser-resolved active usage is present. Empty Array slots use Usage 0 and remain neutral.
- The Report ID is transported separately and is never included in the field bit offsets.
- Field bit offsets are relative to the report payload after the Report ID byte has been removed.

The router validates all field metadata before editing the output. Invalid metadata, unsupported field widths, or insufficient scratch capacity cause the original report to be forwarded unchanged. This is fail-open behavior: a mapping/parser problem must not silently suppress normal keyboard or mouse input.

## Parser integration requirements

The generic HID parser must provide one `mapping_report_field_t` entry for each independently routable field/Array slot:

- Usage Page and resolved Usage
- bit offset and width
- logical neutral value
- decoded signed/unsigned value
- Variable/Array classification
- relative/absolute classification

For keyboard Array fields, the parser must resolve each slot's current usage. Usage 0 denotes an empty slot. Keyboard rollover/error usages must be preserved and must not be interpreted as ordinary mapped keys.

If a report format cannot be represented safely at field level, the parser must mark it unsupported for mapping and the router must forward it unchanged. It must not drop the entire report merely because one field is unsupported.

## Mapping engine boundary

`mapping_set_config()` installs a complete validated snapshot. The current interface accepts a bounded list of Usage Page/Usage rules. JSON decoding, profile versioning, WASD vector calculation, mouse shaping, digital action state machines and runtime snapshot swapping remain subsequent work.

The event callback is invoked synchronously by the routing call. It must be short and non-blocking; it should enqueue a semantic event for task-context processing rather than write DAC/GPIO hardware directly.

## Resource and concurrency notes

- The initial implementation supports at most 64 field descriptors per report and 24 mapping rules.
- The proxy uses a fixed 64-byte stack scratch buffer and performs no heap allocation in the report path. Larger reports are forwarded unchanged and reported as unsupported for mapping until a bounded transport-owned scratch buffer is provided.
- The configuration and callback setters are not internally synchronized. Configure them before starting USB report processing, or protect updates at the application/task boundary.
- The parser and transport must own report buffers for the duration of the synchronous routing call.

## Not yet implemented or verified

- HID parser population of field metadata
- Keyboard Array rollover/reconstruction tests against actual G815 reports
- JSON configuration loading and atomic activation
- Mouse X/Y shaping and WASD vector state
- Digital output actions and safe-state behavior
- USB Host-to-Device forwarding integration
- Host unit tests, ESP-IDF build and hardware validation

This interface does not claim end-to-end USB passthrough or G HUB compatibility.
