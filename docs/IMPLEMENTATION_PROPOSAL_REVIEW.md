# Review of external implementation proposal

Date: 2026-11-13

Source reviewed: `gateway_project_complete.zip`

## Decision

The proposal contains useful implementation ideas, but its C++ components are not imported into the firmware tree yet. Importing them unchanged would introduce known behavioral and build risks. The existing architecture and requirements documents remain authoritative.

## Accepted design contributions

### Motion learning

- Keep learning explicitly user-controlled: start, stop-and-freeze, and cancel.
- Keep the raw learned model separate from the editable tuning profile.
- Build the effective motion model from an immutable learned model plus tuning data.
- Keep persistence behind a narrow adapter so the learner is independent of the physical ADC/DAC implementation.
- Feed the learner only calibrated, deadzone-corrected and radially normalized XY samples.
- Derive velocity, acceleration and jerk from timestamped samples, with sample-interval validation.
- Keep backup/restore separate from normal tuning operations.

### Input mapping

- Use HID Usage IDs rather than localized key names.
- Keep keyboard vector mapping and relative mouse mapping configurable through versioned JSON.
- Keep mouse X/Y sensitivity, inversion, deadband, response curve, radial limit and inactivity return independently configurable.
- Keep digital actions (Momentary, Toggle, Pulse, Set and Reset) as data-driven bindings.
- Validate a complete candidate configuration before activating it.

### Gateway mode

- Keep mode control behind an abstract input interface.
- Debounce the physical key and toggle only on its rising edge.
- Keep LED feedback behind a device-specific abstraction.
- Invoke an explicit safe-state hook on every mode transition.

## Findings that block direct source import

1. **Wrong mode key in the proposal:** several files use `M1` and `M1_LED`. The project requirement is G815 **G1**. G1 input encoding and LED command are still unverified; no code or JSON may assign guessed codes.
2. **Proxy routing is incomplete:** the proposed `GatewayReportRouter` forwards a whole keyboard or mouse state in Passthrough, but in Proxy it sends the whole state only to the mapping engine. It has no consumed/unconsumed result and no raw-report/field context. Consequently, ordinary unmapped keys/buttons would be suppressed instead of continuing to the PC. This violates the required consume-if-mapped, otherwise-forward behavior.
3. **Mixed reports are not handled:** the router works with decoded whole-device states, not per-field routing or safe report reconstruction. It cannot preserve unrelated fields in a report containing both mapped and unmapped inputs.
4. **C++ language mismatch:** `human_motion_model.cpp` uses designated initializers while the component requests C++17. This is not a valid C++17 implementation.
5. **Persistence is not safely atomic:** the JSON writer removes the previous file before renaming the temporary file. A rename failure can therefore destroy the last valid model. Its short-circuit close expression can also skip `fclose()` after an earlier write/flush failure.
6. **Model loading needs stricter validation:** profile array cardinality, duplicate/missing bucket indices, integer range conversions and all tuning/statistic values are not comprehensively validated.
7. **Learning memory needs a board-specific budget:** the proposed default can retain up to 256 samples for each metric in each of 1,440 profile buckets. This can consume several megabytes and must be measured against the selected ESP32-P4 board's available RAM/PSRAM and intended learning duration.
8. **Save failure recovery is incomplete:** `stopLearningAndSave()` stops and freezes the learner before writing. If storage fails, the candidate model is not retained for a retry.
9. **Thread ownership is unresolved:** the proposal itself notes the lack of synchronization. The integration must be called from a defined task or protected by a queue/lock before concurrent control and sampling are allowed.
10. **Digital output limit differs:** the proposal permits 32 outputs, while the project requirement is 12 logical outputs. The implementation must enforce the project limit unless the requirement changes explicitly.

## Import plan

The following work is required before importing implementation code:

1. Implement a report-routing result that explicitly identifies consumed semantic fields while retaining original report bytes and descriptor context.
2. Route unmapped fields unchanged to the PC; define and test mixed-report reconstruction, including keyboard arrays and rollover behavior.
3. Replace all M1 placeholders with G1 names while keeping the actual G1 wire encoding and LED command unbound until captured.
4. Resolve the C++17/C++20 mismatch without silently raising the project's language standard.
5. Make persistence crash-safe: preserve the last valid file, check all I/O results, close files on every path, and validate the complete model before activation.
6. Define a memory budget and sampling policy for the selected board before fixing bucket/sample capacities.
7. Define FreeRTOS task ownership and ensure SD-card writes never run in the sampling callback.
8. Add host-side unit tests for mapping, mode transitions, learning statistics, JSON round trips and malformed input.

## Status

- Accepted: architecture and API-boundary ideas listed above.
- Not imported: proposal source files, example configurations and M1-based placeholders.
- Not claimed: firmware integration, successful build, hardware validation or verified G1 protocol.

The uploaded proposal is retained as a design reference outside the repository. This review records the decisions needed for parallel contributors and prevents incomplete prototype code from being mistaken for validated firmware.
