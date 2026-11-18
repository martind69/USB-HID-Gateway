# USB HID Gateway – Project Requirements

Status: Architecture baseline, 2026-11-12. Requirements are not implementation claims. Items marked **Open** require further evidence or a hardware decision.

## 1. Objective and scope

Build an ESP32-P4-based USB HID gateway with independent PC-facing USB Device and peripheral-facing USB Host roles. The initial target peripherals are:

- Logitech G815 DE keyboard, VID:PID `046D:C33F`
- Logitech G502 HERO mouse, VID:PID `046D:C08B`

The gateway is intended to process selected HID input events while preserving ordinary PC input for anything not explicitly mapped. It is not a conventional USB hub and is not assumed to provide transaction-level transparency for arbitrary USB devices.

## 2. Operating modes

### 2.1 Passthrough

- Default mode after power-up and reset.
- Forward supported keyboard and mouse Input reports to the PC without semantic modification.
- Preserve report IDs, report lengths and all bits not explicitly modified.
- Preserve unknown report fields.
- Forward relevant Output, Feature, class and vendor-specific traffic where the implemented USB architecture supports it.
- Do not claim full Logitech G HUB compatibility until validated end-to-end.

### 2.2 Proxy

- Apply the active JSON mapping configuration to eligible input events.
- A mapped input is consumed by default: it is processed by the gateway and is not forwarded as its ordinary PC input.
- An input that has no active mapping is forwarded to the PC normally and unchanged.
- Mapping is dynamic; changing the JSON configuration must not require recompiling firmware.
- A single input may have an explicitly defined routing policy in configuration if a future requirement needs simultaneous processing and forwarding. The initial default remains consume-if-mapped, otherwise forward.
- Physical mouse X/Y values remain available to the gateway for mapping and motion processing.
- Only the report fields required by the selected routing policy may be modified. Unrelated fields, buttons, wheel, pan, vendor fields and padding must be preserved.

### 2.3 Mode control: G815 G1

- The G1 key is the global mode-control input. M1 is not the mode hotkey.
- Power-up mode is Passthrough.
- A G1 rising edge toggles Passthrough ↔ Proxy.
- G1 release does not cause a second transition.
- G1 is handled by GatewayModeController before ordinary mapping and is reserved from the normal InputMappingEngine.
- G1 is not mapped to a digital output by default.
- Intended feedback: G1 LED off in Passthrough, on in Proxy.
- Exact G1 Input encoding and G1 LED Output command are **Open** pending controlled capture and verification. No guessed Usage ID or vendor command may be used.

## 3. Input mapping

The mapping engine consumes semantic HID events resolved from parsed descriptors. It must not depend on localized key labels or fixed report byte offsets.

Initial logical mappings:

- Keyboard W/A/S/D to a normalized left-stick XY target.
- Relative mouse X/Y to a normalized right-stick XY target.
- Configurable keyboard keys and mouse buttons to up to 12 logical digital outputs.

Keyboard vector:

`x = D - A`, `y = W - S`

Normalize diagonals so the vector magnitude does not exceed 1. Opposing directions cancel. Key release must update the target immediately without creating a stuck state.

Mouse mapping must define separate X/Y sensitivity, inversion, deadband, gamma response, radial limiting, inactivity timeout and return-to-center behavior. Relative deltas must be converted into a bounded target or integrated position according to the selected mapping configuration; they must not be mistaken for absolute coordinates.

Digital actions include Momentary, Toggle, Pulse, Set and Reset. Per-mapping configuration includes source, semantic input code, output channel, active polarity, debounce and pulse duration where applicable.

## 4. Motion and analog I/O

The motion system treats each joystick as one two-dimensional vector. X and Y must not be planned as unrelated axes.

- Input mapping produces targets, not time trajectories.
- HumanTrajectoryGenerator generates continuous XY motion from the current state to a target.
- Position, velocity and acceleration must remain continuous across target changes; jerk must be bounded by the effective motion model.
- ContinuousMotionLearner runs only after explicit user initiation and stops on explicit request.
- LearnedMotionModel is immutable raw learning output.
- MotionModelTuner produces a separate effective HumanMotionModel without altering raw learning data.

Current hardware design baseline:

- ADC: ADS7128, I²C
- DAC: DAC7578, I²C
- Two XY systems, four DAC output channels total
- Twelve logical digital outputs

Part selection is the current design baseline, not proof of assembled or electrically validated hardware. ADC channel assignment, voltage ranges, reference, calibration circuit, DAC output scaling, neutral values and GPIO pin allocation remain **Open**.

## 5. USB/HID requirements

- Parse HID 1.11 report descriptors generically and independently of Logitech-specific logic.
- Support multiple HID interfaces and Report IDs.
- Support Input, Output and Feature reports, Variable and Array items, signed/unsigned fields, arbitrary bit widths, fields crossing byte boundaries, collection hierarchy and Push/Pop.
- Preserve raw descriptor bytes and unknown report bits.
- Keep parsing, semantic interpretation, routing policy, report modification and USB transport separate.
- USB callbacks must remain short and non-blocking; use bounded queues for task-context processing.
- Distinguish generic HID report forwarding from complete USB transaction proxying.
- Treat vendor-specific G HUB traffic as a separate compatibility requirement.

## 6. Hardware and development

- MCU: ESP32-P4.
- Framework: ESP-IDF, C and FreeRTOS.
- IDE: VS Code with Espressif ESP-IDF extension.
- Host and Device roles must use independent controller instances if supported by the selected board and ESP-IDF release.
- Source indentation: tabs, tab width 4. Source comments: English.
- Exact board, board revision, ESP-IDF version, USB routing, PHY and Host VBUS design are **Open**.

## 7. Safety and fault handling

- Startup mode is Passthrough.
- Mode changes must clear mapping state, cancel active trajectories and place analog/digital outputs in configured safe states before enabling the new mode.
- Safe analog neutral values and digital output levels must be hardware-configured; do not assume DAC midscale or GPIO low is universally safe.
- Invalid JSON, unsupported schema versions, invalid mappings and resource exhaustion must fail safely and be reported.
- No stale key, button, toggle, pulse or trajectory state may survive a mode transition or device disconnect.

## 8. Validation requirements

- Host and Device enumeration and simultaneous operation on the selected board.
- Descriptor-driven enumeration of the monitor hub, G815 and G502.
- Generic parser unit tests and malformed-descriptor fuzzing.
- Passthrough report equality tests.
- Proxy routing tests for mapped/consumed and unmapped/forwarded inputs.
- G1 rising-edge toggle and LED feedback tests after protocol capture.
- G HUB vendor traffic and control-transfer compatibility tests.
- Mapping JSON validation and reload tests.
- XY trajectory continuity and jerk-limit tests.
- ADC/DAC calibration, timing, output-range and fault tests.
- Digital output action, polarity, debounce and pulse tests.

## 9. Open decisions and evidence gaps

1. Exact ESP32-P4 board and revision; simultaneous Host/Device connector routing.
2. ESP-IDF version and USB stack capabilities.
3. PC-facing composite HID descriptor and device identity strategy.
4. Raw HID report descriptor bytes for required physical interfaces.
5. Full G815 Input reports, including G1 key encoding and G1 LED command.
6. Full G502 mouse Input reports, including buttons, X/Y, wheel and pan.
7. Complete G HUB vendor/control/Feature transaction requirements.
8. ADC signal source, electrical range, calibration and sampling requirements.
9. DAC reference, output range, analog neutral calibration and output load.
10. Digital GPIO pin assignment and per-output safe state.
11. Exact JSON schema fields and runtime reload mechanism.
12. FreeRTOS task, queue and synchronization design.
13. HumanTrajectoryGenerator algorithm and computational budget.

## 10. Observed hardware evidence

The ASUS ROG monitor contains a four-port Genesys Logic hub (`05E3:0610`). G815 and G502 appear as sibling devices on this hub. The G502 is physically connected through the G815 USB pass-through cable, which is a direct cable path and not another hub.

The G502 exposes a boot mouse interface and a vendor/non-boot interface. Windows HID capabilities report up to 16 buttons, signed relative 16-bit X/Y, signed relative 8-bit wheel and AC Pan. The G815 exposes boot and non-boot/vendor interfaces. Both devices operate at Full-Speed with 1 ms polling in the observed setup.

Selected G815 RGB/profile and G502 DPI/profile/lighting Output transactions have been captured. Their exact evidence and limitations remain in the device-specific protocol documents. These observations do not establish complete G HUB compatibility.
