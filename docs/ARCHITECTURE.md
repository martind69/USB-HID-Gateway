# USB HID Gateway Architecture

## 1. Architectural boundaries

The design is divided into independently testable layers:

1. USB transport owns enumeration, endpoint/control-transfer transport and device lifecycle.
2. Generic HID parser owns descriptor interpretation and report bit layout.
3. HID semantic layer resolves descriptor fields into generic input events.
4. Gateway controller owns operating mode and mode-control key handling.
5. Report router applies the configured consume/forward/modify policy.
6. InputMappingEngine converts selected events into logical targets/actions.
7. HumanTrajectoryGenerator converts XY targets into continuous trajectories.
8. AnalogVectorIO and DigitalIO drive physical outputs.
9. ContinuousMotionLearner and persistence manage explicitly requested calibration data.

No layer may silently absorb responsibilities from another layer.

## 2. Logical module structure

```text
main/
  usb/
    usb_host.c
    usb_device.c
    hid_parser.c
    hid_semantic.c
    hid_proxy.c
  gateway/
    gateway_mode.c
    gateway_router.c
    g815_mode_feedback.c
  mapping/
    input_mapping.c
    keyboard_stick_mapper.c
    mouse_stick_mapper.c
    digital_output_mapper.c
  motion/
    kinematic_estimator.c
    continuous_learner.c
    learned_model.c
    human_motion_model.c
    motion_tuner.c
    motion_serializer.c
    human_trajectory.c
  analog/
    ads7128.c
    dac7578.c
    analog_vector_io.c
  digital/
    digital_io.c
  storage/
    json_config.c
    model_backup.c
  usb_gateway.c
```

This is a logical decomposition. ESP-IDF component boundaries and source placement may be adjusted to match dependency and build requirements. The current repository source is still skeletal; this tree is a target design, not an implementation claim.

## 3. USB and HID pipeline

```text
Peripheral USB Host
        |
Enumeration / Interface lifecycle
        |
Raw HID report descriptor
        |
Generic HID 1.11 parser
        |
Semantic HID events + original report context
        |
GatewayModeController
        |
GatewayReportRouter <---- JSON routing/mapping configuration
        |
PC-facing USB Device
```

The parser is device-independent. Logitech protocol classifiers, where required, belong in separate device-specific code and must never contaminate generic HID parsing.

Raw reports and transfer context must remain available alongside semantic events so that unchanged forwarding can preserve exact payloads and so selective modifications can retain unknown bits.

## 4. Modes and routing policy

### Passthrough

Passthrough is the power-up default. Supported keyboard/mouse reports are forwarded unchanged. The router does not apply normal input mappings.

### Proxy

In Proxy, routing is configuration-driven:

- If an input event matches an enabled mapping, that event is consumed by default and processed by the mapping engine.
- If no enabled mapping matches, the input is forwarded to the PC unchanged.
- G1 is intercepted by the mode controller before ordinary mapping and toggles the mode on its rising edge.
- G1 release is ignored for mode switching.
- G1 is reserved and excluded from ordinary mappings by default.
- Unrelated fields in a report are preserved. A report containing both mapped and unmapped fields may require descriptor-aware field-level reconstruction or splitting; this behavior must be explicitly implemented and tested rather than assumed.

The configuration model may later expose an explicit forward-and-process option, but the initial behavior is consume-if-mapped, otherwise forward.

## 5. Mode transition sequence

On a mode transition:

1. Stop accepting new mapping actions for the old mode.
2. Release momentary digital outputs and clear toggle/pulse state according to configured safe policy.
3. Cancel active left/right trajectories.
4. Set analog outputs to their calibrated neutral values.
5. Apply configured safe digital output levels.
6. Clear mapper and semantic held-input state as appropriate.
7. Commit the new mode.
8. Update G1 LED feedback through the device-specific feedback abstraction.

If a hardware safe-state operation fails, report the fault and do not claim the transition completed safely. The exact fail-safe mode policy is a separate implementation decision.

## 6. Mapping and motion pipeline

```text
HID semantic input
       |
InputMappingEngine
  |        |          |
Left XY  Right XY   Digital actions
  |        |
HumanTrajectoryGenerator (two independent XY instances)
  |        |
AnalogVectorIO
  |
DAC7578 (four logical axis outputs)

ADS7128 -> AnalogVectorIO -> calibrated physical XY observations
         -> KinematicEstimator2D -> ContinuousMotionLearner
```

Input mapping produces targets. It does not generate timed movement. The trajectory generator owns position/velocity/acceleration continuity and jerk constraints. Each joystick is planned as one vector, not as independent X/Y axes.

## 7. Analog and digital outputs

- ADS7128 and DAC7578 are separate I²C hardware backends.
- AnalogVectorIO owns calibration, normalized vector conversion, deadzone and radial envelope handling.
- DAC7578 provides four used output channels for the two XY systems; unused channels remain reserved unless the hardware design changes.
- DigitalIO owns 12 logical outputs, polarity, safe states and action execution.
- Physical pin mapping and electrical safe states are board configuration, not hard-coded assumptions in mapping logic.

The ADS7128 input signal source and relation to the DAC outputs must be documented by the hardware design. The ADC is not assumed to automatically measure the DAC outputs.

## 8. Learning and effective motion model

The learner is inactive by default and only collects samples after explicit start. It produces an immutable LearnedMotionModel. Fine-tuning is stored separately and generates an effective HumanMotionModel.

```text
ADS7128 observations
        |
AnalogVectorIO normalization
        |
KinematicEstimator2D (p, v, a, j)
        |
ContinuousMotionLearner (manual start/stop)
        |
LearnedMotionModel (immutable raw learning)
        |
MotionModelTuner + tuning profile
        |
HumanMotionModel (effective parameters)
        |
HumanTrajectoryGenerator
```

Robust statistics and angular/radial/phase bins are design candidates. The proposed 36 × 5 × 8 = 1440 profile layout must be validated for memory, sample sufficiency and runtime cost before it becomes a fixed implementation contract.

## 9. Persistence

Persistent configuration is separated by responsibility:

- Gateway mode/configuration
- Input mapping
- Learned raw motion model
- Motion tuning profile
- Timestamped backups

Files are versioned and validated. Learned raw data is never overwritten by tuning. Writes use a temporary file and an atomic replacement strategy supported by the selected filesystem; backup rotation and recovery are explicit storage responsibilities.

## 10. USB transparency boundary

Generic HID proxying is not full USB transaction proxying. The design must separately handle or explicitly limit:

- Device identity and descriptors
- Multiple interfaces and endpoints
- Standard and HID class requests
- Feature reports
- Vendor-specific control transfers
- Hub topology and lifecycle
- PC-facing composite HID strategy

G HUB compatibility is an evidence-driven integration target, not an architectural guarantee.

## 11. Implementation status

All module names and flows above describe the intended architecture. USB transport, parser, router, mapping, motion, storage and hardware backends must be marked implemented only after code and tests exist. Board-level Host/Device concurrency remains subject to Milestone 0 verification.
