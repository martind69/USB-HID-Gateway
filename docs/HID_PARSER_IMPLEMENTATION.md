# Generic HID parser implementation slice

Status: bounded caller-owned per-interface descriptor contexts implemented; integration and conformance testing remain pending.

## Implemented

- Bounded descriptor retention (1024 bytes).
- HID short-item decoding and safe skipping of well-formed long items.
- Global Usage Page, Logical Minimum, Report Size, Report ID and Report Count handling.
- Bounded Global Push/Pop and Collection nesting validation.
- Separate report cursors for Input, Output and Feature reports, keyed by Report ID.
- Field metadata for Usage, Usage ranges, bit offset/width, report count, logical range, Variable/Array, Relative/Absolute and Constant flags.
- Top-level Application collection Usage Page/Usage for basic keyboard/mouse classification.
- Bounds-checked little-endian bit extraction with signed-field extension.
- Explicit errors for malformed items, invalid Report IDs, unsupported item encoding, capacity overflow, stack underflow/overflow and collection imbalance.
- No heap allocation; fixed parser storage.

## Deliberate limitations of this first slice

- Each active USB HID interface owns a caller-allocated parser context. The context must remain alive for the interface lifetime.
- Usage expansion, Delimiter sets, Designator/String semantics, Physical/Unit metadata and full HID usage-width rules are not yet implemented.
- Array slot values are not resolved to active usages.
- Logical ranges are not yet exposed in field metadata.
- The parser does not yet populate mapping field values or route reports.
- No claim of complete HID 1.11 conformance is made.

## Next integration steps

1. Attach one parser context to each claimed USB HID interface and release it on disconnect.
2. Add field accessors and Array usage resolution.
3. Add host-side tests using captured G815/G502 descriptors plus synthetic malformed descriptors.
4. Integrate the USB Host HID interface lifecycle and interrupt-IN transfers.
5. Select a PC-facing descriptor strategy that can represent the supported keyboard/mouse reports without truncation.
