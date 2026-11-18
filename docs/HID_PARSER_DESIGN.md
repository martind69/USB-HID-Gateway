# Universal HID Parser Design

Status: Design proposal. This document defines the parser architecture and API direction; it is not an implementation claim.

## 1. Objective

Implement a device-independent HID 1.11 report-descriptor parser. The parser must not contain Logitech-, mouse-, keyboard- or operating-system-specific assumptions.

The parser describes the format of HID reports. It does not decide how a report is forwarded or modified. Device enumeration, report transport, semantic interpretation and policy remain separate modules.

Primary goals:

- Parse standard HID short items and safely skip valid long items.
- Support multiple report IDs and all three report types: Input, Output and Feature.
- Calculate bit offsets independently for each report type and report ID.
- Preserve usages, usage ranges, logical ranges, flags and collection context.
- Support Variable and Array main items, signed and unsigned values, constants and relative fields.
- Reject malformed or resource-exhausting descriptors explicitly; never silently truncate the parsed model.
- Provide generic field read/write helpers with strict bounds checking.
- Keep the original descriptor available for forwarding to the PC-side USB Device implementation.

Out of scope for the first parser milestone:

- Implementing USB Host or USB Device enumeration.
- Translating vendor-specific protocols.
- Interpreting every HID usage semantically.
- Rebuilding or normalizing descriptors.
- Guaranteeing compatibility with proprietary configuration software.

## 2. Processing pipeline

1. USB Host obtains the HID report descriptor for one interface.
2. The descriptor bytes are retained unchanged by the device/interface context.
3. The parser validates the descriptor and builds a normalized report model.
4. The proxy forwards raw report bytes without parsing when no modification is needed.
5. A semantic layer resolves fields by Usage Page, Usage, report type and report ID.
6. A modification layer changes only explicitly selected bit ranges.
7. USB Device transmits the report using the matching PC-facing interface and report format.

The parser must not be called from a time-critical USB callback. Parse descriptors in task context during enumeration.

## 3. Parser state machine

The parser processes HID items in descriptor order.

### Global state

Maintain the current values for:

- Usage Page
- Logical Minimum / Maximum
- Physical Minimum / Maximum
- Unit and Unit Exponent
- Report Size
- Report Count
- Report ID

Implement the HID Push and Pop items with a bounded global-state stack. A stack overflow or underflow is a parse error.

### Local state

Maintain local items until the next Main item:

- Usage
- Usage Minimum / Maximum
- Designator and String items, including ranges

Local state is cleared after every Main item, including Collection and End Collection.

### Collection state

Maintain a bounded collection stack. Each parsed field records its collection ancestry or a stable collection reference. Collection underflow, excessive nesting and an unclosed collection are errors.

### Main items

For Input, Output and Feature:

- Compute the field bit offset from the current cursor for the matching (report type, report ID).
- Advance that cursor by Report Size multiplied by Report Count.
- Preserve the Main-item flags (Data/Constant, Array/Variable, Absolute/Relative, Wrap, Nonlinear, No Preferred, Null State, Volatile, Buffered Bytes).
- Expand Variable items into individual fields where practical.
- Preserve Array semantics as an array field; do not pretend each array slot has a fixed Usage.
- Associate local usages and usage ranges according to HID rules.

Collection and End Collection update the collection stack and clear local state.

## 4. Report layout model

Offsets are tracked separately for each combination of:

- Report type: Input, Output or Feature
- Report ID: zero for unnumbered reports, otherwise the descriptor's Report ID

A report layout stores the total payload bit count and byte count. If Report IDs are used, the on-wire report includes the Report ID byte; the layout's payload offsets begin after that byte. The transport adapter must handle this byte consistently.

Do not mix descriptor bit offsets with offsets into a transport buffer without explicitly accounting for the Report ID prefix.

The parser must reject invalid Report ID zero items, conflicting report-ID use, zero or impossible Report Size/Count combinations, integer overflow and layouts exceeding configured limits.

## 5. Field access

Field access operates on a parsed field and a report buffer:

- Verify report type and report ID.
- Verify that the supplied buffer contains the complete report.
- Verify bit offset and width against the layout.
- Extract or update little-endian HID bit fields, including fields crossing byte boundaries.
- Sign-extend values when Logical Minimum is negative.
- Validate writes against the field's representable/logical range.
- Preserve all bits outside the selected field.

For Array fields, provide raw slot access first. Usage interpretation is a separate helper because array values represent selectors rather than independent boolean fields.

The generic accessor must not assume that X/Y are 8, 16 or 32 bits, or that buttons are located in the first byte.

## 6. Proposed public API direction

The final API should expose opaque parser/device contexts rather than one global descriptor. One USB device can expose multiple HID interfaces, each with a different report descriptor.

Conceptual operations:

- Create/reset a parser context.
- Parse one descriptor into that context.
- Query report layouts by type and ID.
- Enumerate fields and collections.
- Find fields by Usage Page, Usage, report type and report ID.
- Read/write signed or unsigned field values.
- Retrieve parse diagnostics and required resource counts.

The public API should return explicit status codes such as OK, invalid argument, malformed descriptor, unsupported item, capacity exceeded and report too short. A Boolean result is insufficient for diagnosing real devices.

Do not expose internal parser storage layout as a permanent API until the capacity and allocation strategy has been selected.

## 7. Resource strategy for ESP32-P4

Use explicit compile-time limits initially, with all limits centralized in one configuration header. Candidate limits to evaluate:

- Maximum report descriptor bytes per interface
- Maximum parsed fields
- Maximum report layouts
- Maximum collection nesting
- Maximum global Push/Pop depth
- Maximum local Usage entries/ranges

The parser must report capacity exhaustion and fail that interface cleanly. It must never continue with a partially valid model.

A later implementation can use measured requirements to decide whether static pools, per-interface heap allocation or a hybrid is best. Avoid unbounded allocation and avoid large temporary copies.

## 8. Raw proxy and PC-facing descriptor

The raw proxy and parser have different responsibilities:

- Raw forwarding preserves the exact report bytes.
- The parser provides metadata for selective interpretation.
- The PC-facing USB Device must expose a compatible descriptor and report format.
- The proxy must preserve unknown fields and report IDs when modifying known fields.

Keeping the original descriptor bytes is essential. Reconstructing a descriptor from the normalized model is not part of the initial design.

A transparent USB HID proxy has limits: USB interface topology, endpoint characteristics, device identity, control requests and vendor-specific transfers may need explicit handling. A report parser alone cannot make every USB device transparent.

## 9. Validation plan

Build host-side unit tests for the parser, independent of ESP-IDF and USB hardware. Use captured descriptors and small synthetic descriptors.

Required test groups:

1. Basic boot-style mouse: buttons, relative X/Y and wheel.
2. Keyboard: modifier bits, key arrays and reserved/constant fields.
3. Multiple Report IDs with different report lengths.
4. Multiple report types: Input, Output and Feature.
5. 1-, 7-, 8-, 12-, 16- and 32-bit fields.
6. Signed values and fields crossing byte boundaries.
7. Usage ranges and Variable versus Array semantics.
8. Nested collections and Push/Pop.
9. Long items and unknown but skippable items.
10. Truncated items, malformed collections, invalid IDs and arithmetic overflow.
11. Capacity exhaustion at every configured limit.
12. Read-modify-write tests proving all unrelated bits remain unchanged.

For every valid test descriptor, verify both the normalized model and exact report bit offsets. For every invalid descriptor, verify a deterministic error and no partially usable model.

## 10. Implementation milestones

1. Define parser limits, status codes and opaque context lifecycle.
2. Implement bounded item decoding and global/local state handling.
3. Implement collection tracking and per-(type, ID) report cursors.
4. Implement field and report-layout storage.
5. Implement bit-level read/write helpers and signed conversion.
6. Add host-side unit tests and fuzz malformed descriptors.
7. Integrate parser output with the HID proxy.
8. Integrate descriptor passthrough with the PC-facing USB Device.
9. Add semantic helpers for Generic Desktop and Button usages used by the mouse/keyboard use case.

## 11. Normative references

- USB HID Specification 1.11: https://www.usb.org/sites/default/files/hid1_11.pdf
- HID Usage Tables: https://www.usb.org/hid

The parser should follow the specification rather than relying on examples from a particular device vendor.
