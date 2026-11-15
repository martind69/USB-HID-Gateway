# Generic HID parser implementation slice

Status: initial bounded descriptor-layout parser implemented; integration and conformance testing remain pending.

## Implemented

- Bounded descriptor retention (1024 bytes).
- HID short-item decoding and safe skipping of well-formed long items.
- Global Usage Page, Logical Minimum, Report Size, Report ID and Report Count handling.
- Bounded Global Push/Pop and Collection nesting validation.
- Separate report cursors for Input, Output and Feature reports, keyed by Report ID.
- Field metadata for Usage, Usage ranges, bit offset/width, report count, Variable/Array, Relative/Absolute and Constant flags.
- Explicit errors for malformed items, invalid Report IDs, unsupported item encoding, capacity overflow, stack underflow/overflow and collection imbalance.
- No heap allocation; fixed parser storage.

## Deliberate limitations of this first slice

- Parser state is currently a single active descriptor context. Per-device/per-interface contexts are the next required change before concurrent HID interfaces can be parsed.
- Usage expansion, Delimiter sets, Designator/String semantics, Physical/Unit metadata and full HID usage-width rules are not yet implemented.
- Array slot values are not resolved to active usages.
- Logical ranges are not yet exposed in field metadata.
- The parser does not yet populate mapping field values or route reports.
- No claim of complete HID 1.11 conformance is made.

## Next integration steps

1. Convert the parser to caller-owned per-interface contexts.
2. Add field accessors and Array usage resolution.
3. Add host-side tests using captured G815/G502 descriptors plus synthetic malformed descriptors.
4. Integrate the USB Host HID interface lifecycle and interrupt-IN transfers.
5. Select a PC-facing descriptor strategy that can represent the supported keyboard/mouse reports without truncation.
