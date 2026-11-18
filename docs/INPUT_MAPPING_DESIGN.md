# Input Mapping Design

Status: Design baseline. Mapping schema and runtime reload mechanism are specified conceptually; exact JSON schema version is to be finalized in CONFIGURATION_FORMATS.md.

## 1. Responsibilities

InputMappingEngine translates semantic HID input events into logical targets and actions. It does not generate timed motion trajectories and does not own USB transport.

Inputs are identified by device/interface context and descriptor-derived semantic identity (Usage Page, Usage, report type/ID and collection context as needed). Localized key labels and fixed byte offsets are not identifiers.

## 2. Mapping behavior and routing

In Proxy mode:
- An enabled matching mapping is consumed by default and processed.
- An input without an enabled mapping is forwarded to the PC unchanged.
- G1 is intercepted by GatewayModeController and excluded from ordinary mappings.
- Mapping changes are loaded from JSON; firmware recompilation is not required.

A mapping match must be deterministic. Conflicting mappings for the same source event must be rejected during configuration validation unless a future schema defines explicit precedence.

## 3. Keyboard to left XY target

Initial WASD mapping uses keyboard HID usages:

| Key | Usage |
|---|---:|
| A | `0x04` |
| S | `0x16` |
| D | `0x07` |
| W | `0x1A` |

Let each direction be represented as a Boolean pressed state:

`x = pressed(D) - pressed(A)`

`y = pressed(W) - pressed(S)`

Normalize only when magnitude exceeds one:

`p = (x,y) / max(1, sqrt(x²+y²))`

Opposing directions cancel. Diagonal input has magnitude one. Key release updates the target; disconnect or mode change clears all held states.

## 4. Relative mouse to right XY target

Mouse X/Y are relative deltas, not absolute coordinates. Resolve them through the HID descriptor and convert them using configured sensitivity and response shaping.

Per-axis configuration:
- sensitivity
- inversion
- deadband
- gamma
- maximum input/reference delta
- inactivity timeout
- return-to-center behavior
- integration/decay policy

A candidate response curve is:

`f(d) = sign(d) × |clamp(d/dmax, -1, 1)|^gamma`

The design must define whether shaped deltas are accumulated into a bounded position target or used as velocity-like commands. This choice affects feel and must be configurable or settled by calibration; never treat a relative delta as a persistent absolute coordinate.

Apply a circular/radial limit after combining X/Y so diagonal magnitude remains bounded.

## 5. Digital output mapping

Up to 12 logical outputs are exposed independently of physical GPIO assignment.

Actions:
- `Momentary`: active while source is held.
- `Toggle`: invert state on a valid rising edge.
- `Pulse`: activate for configured duration on a valid edge.
- `Set`: set active state on a valid edge.
- `Reset`: set inactive state on a valid edge.

Per mapping:
- source device/interface
- semantic input source (keyboard usage or mouse button usage)
- output channel
- action
- active polarity
- debounce interval
- pulse duration, when applicable
- enabled flag

Use monotonic time for debounce and pulse timing. Repeated reports indicating a held key must not retrigger edge-based actions. Disconnect and mode transition must release momentary outputs and apply safe-state policy to latched actions.

## 6. Mapping validation

Reject:
- invalid source identifiers or unsupported usages
- output channels outside 0–11
- duplicate/conflicting source-to-output rules
- non-finite numeric values
- invalid sensitivity, gamma, deadband or timing ranges
- unsupported action parameters
- reserved G1 mapping
- configuration exceeding fixed firmware capacity

The mapping engine should expose a validated immutable active configuration snapshot. Reload should validate a candidate completely before replacing the current snapshot.
