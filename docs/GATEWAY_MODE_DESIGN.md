# Gateway Mode and Report Routing Design

Status: Design baseline, 2026-11-12. G1 wire encoding and LED command remain unverified.

## 1. Modes

`GatewayMode` has two values:

- `Passthrough`: default after power-up/reset.
- `Proxy`: apply enabled JSON mappings; unmapped inputs continue to the PC.

## 2. G1 mode control

The Logitech G815 G1 key is the global mode toggle. M1 is not the mode key.

- Detect a G1 press transition (rising edge) from semantic input state.
- Toggle Passthrough ↔ Proxy exactly once per press.
- G1 release only rearms edge detection.
- Intercept G1 before the normal mapping engine.
- Reserve G1 from ordinary mappings by default.
- Feedback target: G1 LED off in Passthrough, on in Proxy.

The G1 Input code and LED Output command are unknown until captured and decoded. Do not substitute a guessed HID Usage or vendor command. Keep input detection and LED feedback behind interfaces so protocol knowledge can be added later.

## 3. Report routing policy

The initial Proxy policy is:

**Enabled mapping match → consume and process. No enabled mapping match → forward normally and unchanged.**

This policy applies dynamically to the JSON configuration. There is no fixed firmware list of all keys/buttons to suppress.

Examples:
- W mapped to left-stick: consume W from the PC input stream and update left-stick target.
- A mouse button mapped to a digital output: consume that button event; preserve unrelated mouse fields.
- An unmapped media key or mouse button: forward normally.
- G1: mode controller handles it; it is not passed through as an ordinary mapped input by default.

A future mapping schema may offer explicit forward-and-process behavior, but it is not part of the initial policy.

## 4. Mixed reports and field-level routing

One HID report can contain multiple semantic fields. A mapped field may need to be consumed while unrelated fields in the same report remain visible to the PC.

The router therefore cannot assume that consuming one event always means dropping the entire raw report. It must:
1. Retain the raw report and parsed field layout.
2. Identify all semantic events represented by the report.
3. Apply configured mapping decisions per event/field.
4. Construct a PC-facing report that removes or neutralizes consumed fields without changing unrelated fields.
5. Forward the resulting report only when it contains meaningful unmapped input or required protocol state.

The exact neutralization strategy depends on the PC-facing descriptor and report semantics. For array-style keyboard reports, simultaneous mapped and unmapped keys require special care: rebuilding the key array must preserve all remaining keys and avoid duplicate/rollover errors. If safe field-level routing is not possible for a particular report format, reject that mapping configuration or document a deliberate whole-report policy; never silently drop unrelated input.

## 5. Mode transition sequence

A transition must prevent stale input/output state:

1. Temporarily stop dispatching new mapping actions.
2. Release momentary digital outputs and resolve Toggle/Pulse state using configured safe-state policy.
3. Cancel both XY trajectories.
4. Set analog outputs to calibrated neutral values.
5. Set digital outputs to configured safe levels.
6. Clear held-key/button and mapper state.
7. Commit the new mode.
8. Update G1 LED feedback.

The mode change must not be reported as complete if a required hardware safe-state operation fails. The final fault policy and recovery path remain implementation decisions.

## 6. Disconnect and error behavior

- A device/interface disconnect clears all held inputs originating from that interface.
- A queue overflow or malformed report must not create stuck keys or outputs.
- Unknown/unclassified reports are forwarded unchanged when transport permits.
- Unsupported control transfers are explicitly recorded; do not fabricate successful responses.
- If G1 cannot be identified because its report is unavailable or malformed, the current mode remains unchanged.

## 7. Interfaces

Suggested responsibilities:
- `GatewayModeController`: mode state and G1 edge detection.
- `G815ModeFeedback`: abstract G1 LED state request; device-specific report implementation later.
- `GatewayReportRouter`: consume/forward/modify decision using active mapping configuration.
- `HidSemanticLayer`: generic descriptor-driven event extraction.
