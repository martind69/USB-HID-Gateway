# USB HID Gateway – Implementation Plan

Status: Planned. A milestone is complete only after its verification criteria pass. The repository currently contains an ESP-IDF skeleton and design/evidence documents; no firmware build or hardware milestone is claimed complete.

## Development principles

- Verify the exact ESP32-P4 board and USB hardware before transport implementation.
- Keep the generic HID parser independent of ESP-IDF and device-specific protocols.
- Preserve raw descriptors, reports and unknown fields.
- Separate USB transport, semantic interpretation, routing policy, mapping, trajectory generation and physical I/O.
- Treat Proxy routing as dynamic JSON configuration: mapped inputs are consumed; unmapped inputs continue to the PC unchanged.
- G1 rising edge toggles Passthrough/Proxy; G1 release does not toggle. G1 encoding and LED command remain unverified.
- Keep USB callbacks short and non-blocking. Use bounded queues and task-context processing.
- Do not claim G HUB compatibility until end-to-end tests pass.

## Milestone 0 – Board, electrical design and toolchain

Tasks:
1. Identify exact ESP32-P4 development board and revision.
2. Inspect schematic and map both USB controller instances to connectors/pins.
3. Verify simultaneous Host and Device operation on the selected board and ESP-IDF release.
4. Record ESP-IDF/toolchain versions.
5. Verify Host VBUS source, current limit, protection and connector wiring.
6. Define I²C bus topology, ADS7128 input signal sources, DAC7578 reference/output range and GPIO safe states.

Verification:
- Board revision, controller routing and power design are documented.
- Minimal ESP-IDF project builds for the selected target.
- Electrical ranges and safe output states are specified.

## Milestone 1 – PC-facing USB Device proof of concept

Tasks:
1. Initialize the PC-facing USB Device controller.
2. Expose a minimal test HID interface.
3. Handle enumeration and required standard/HID class requests.
4. Send known test reports to the PC.

Verification:
- PC enumerates reliably across reconnects.
- Test reports are received correctly.

## Milestone 2 – Peripheral-facing USB Host and hub enumeration

Tasks:
1. Initialize the independent Host controller.
2. Enumerate a directly connected test HID device.
3. Add hub support and enumerate the monitor's Genesys Logic hub.
4. Enumerate G815 and G502 as separate downstream devices.
5. Track per-device/interface lifecycle and disconnect/reconnect.

Verification:
- Hub, G815 and G502 are identified independently.
- G815 pass-through is not treated as a hub.
- Required interfaces/endpoints are identified.
- Removing one device does not corrupt another device's state.

## Milestone 3 – Generic HID 1.11 parser

Tasks:
1. Define bounded parser contexts, capacities, errors and lifecycle.
2. Implement short/long item decoding, global/local state, Push/Pop and collections.
3. Calculate bit offsets independently by report type and Report ID.
4. Support Variable/Array, signed/unsigned and cross-byte fields.
5. Implement checked field read/write helpers.
6. Add host-side unit tests and fuzz malformed descriptors.

Verification:
- Parser tests cover keyboard, mouse, multiple interfaces/IDs and all report types.
- Read-modify-write preserves unrelated bits.
- Invalid descriptors and capacity exhaustion fail deterministically.
- Tests run without ESP-IDF or USB hardware.

See [HID parser design](HID_PARSER_DESIGN.md).

## Milestone 4 – Semantic layer and raw HID proxy

Tasks:
1. Retain raw descriptor and report context per interface.
2. Resolve descriptor fields into generic semantic input events.
3. Create bounded per-interface queues and routing metadata.
4. Forward supported reports unchanged in Passthrough.
5. Handle queue backpressure, overflow and disconnect.
6. Preserve Output/Feature/control-transfer context where supported.

Verification:
- Passthrough ingress/egress reports match.
- No unbounded memory growth.
- Unknown fields and report IDs remain intact.

## Milestone 5 – PC-facing descriptor strategy and vendor compatibility

Tasks:
1. Select composite keyboard/mouse descriptor and identity strategy.
2. Expose compatible report descriptors and endpoints.
3. Implement required standard and HID class requests.
4. Capture and classify G815/G502 vendor-specific and Feature traffic.
5. Implement only evidenced transactions needed by the supported feature set.

Verification:
- PC recognizes keyboard and mouse functions.
- Required G HUB functions are demonstrated on actual hardware.
- Unsupported transactions and limitations are documented.

## Milestone 6 – Gateway mode and dynamic report routing

Tasks:
1. Implement GatewayModeController with Passthrough default.
2. Add G1 rising-edge toggle through an abstract input interface.
3. Add G1 LED feedback abstraction; bind it only after capture verifies the command.
4. Implement consume-if-mapped, otherwise-forward routing.
5. Support field-level routing for mixed reports without dropping unrelated keys/buttons.
6. Implement safe mode transition and disconnect cleanup.

Verification:
- G1 toggles exactly once per press; release does not toggle.
- Mapped events are consumed.
- Unmapped events reach the PC normally.
- Mixed reports preserve all unmapped fields.
- No held key/button/output survives mode transitions.

## Milestone 7 – JSON configuration and input mapping

Tasks:
1. Define versioned gateway and input-mapping JSON formats.
2. Implement validation and immutable active configuration snapshots.
3. Implement WASD to left XY target.
4. Implement relative mouse X/Y shaping to right XY target.
5. Implement up to 12 digital output actions.
6. Add safe runtime reload without recompilation.

Verification:
- Invalid or conflicting mappings are rejected.
- Mapping changes activate atomically.
- WASD, mouse and digital actions pass deterministic unit tests.
- Unmapped events continue to the PC.

## Milestone 8 – AnalogVectorIO and physical outputs

Tasks:
1. Implement ADS7128 I²C backend.
2. Implement DAC7578 I²C backend.
3. Implement calibration, normalization, deadzone and radial envelope in AnalogVectorIO.
4. Map four DAC channels to left/right XY.
5. Implement 12 logical DigitalIO outputs with polarity and safe states.

Verification:
- ADC scan timing and skew are measured.
- DAC output range and neutral calibration are verified.
- All logical outputs are independently controllable.
- I²C failure and reset states are safe.

## Milestone 9 – Motion estimation and manual learning

Tasks:
1. Implement timestamp-aware KinematicEstimator2D.
2. Filter sensor observations before differentiation.
3. Implement manually started/stopped ContinuousMotionLearner.
4. Calculate robust statistics and confidence per model cell.
5. Serialize immutable LearnedMotionModel data.

Verification:
- No learning occurs unless explicitly started.
- Stopping learning freezes the raw model.
- Noise, jitter, sparse samples and invalid data are handled deterministically.

## Milestone 10 – Fine-tuning and HumanMotionModel

Tasks:
1. Implement separate tuning parameters and optional overrides.
2. Validate effective parameter ranges.
3. Combine raw model and tuning into an immutable effective snapshot.
4. Implement versioned save/load and backup/restore.

Verification:
- Tuning never modifies raw learning data.
- Invalid tuning is rejected.
- Save/load round trips preserve values and metadata.

## Milestone 11 – HumanTrajectoryGenerator

Tasks:
1. Select a jerk-limited online XY trajectory algorithm.
2. Preserve position, velocity and acceleration across target updates.
3. Handle reversal, braking, return-to-center and target hold.
4. Bound computation time and define behavior for infeasible target changes.
5. Integrate two independent XY instances with AnalogVectorIO.

Verification:
- Position/velocity/acceleration continuity tests pass.
- Jerk limits hold under target changes and reversal.
- Runtime meets the selected update budget on ESP32-P4.

## Milestone 12 – System integration and robustness

Tasks:
1. Run simultaneous USB Host/Device traffic.
2. Test G815/G502, G1 mode control, mappings and outputs together.
3. Test G HUB control/Feature/vendor traffic.
4. Exercise disconnect/reconnect, queue saturation, I²C errors and malformed reports.
5. Measure latency, CPU, RAM, queue high-water marks and storage behavior.
6. Document exact supported device/firmware/software combinations.

Verification:
- No report corruption under sustained load.
- No stuck input/output state.
- No unbounded resource growth.
- Compatibility claims are backed by repeatable evidence.

## Current repository status

| Area | Status |
|---|---|
| ESP-IDF project skeleton | Present |
| USB topology and device evidence | Documented |
| G815 RGB/profile protocol | Partially reverse-engineered |
| G502 DPI/profile/lighting protocol | Partially reverse-engineered |
| Raw HID descriptors | Retrieval pending |
| G815/G502 Input report captures | Pending |
| Generic HID parser design | Documented; implementation pending |
| USB Host/Device | Placeholder |
| Semantic layer and report router | Design only |
| G1 input/LED protocol | Unverified |
| JSON mapping | Design only |
| ADS7128/DAC7578 hardware | Design baseline; electrical validation pending |
| Motion learner/model/trajectory | Design only |
| Firmware build | Not verified |
| Hardware validation | Not started |
