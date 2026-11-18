# Test Strategy

Status: Test plan. No test group is considered passed until automated results or repeatable hardware evidence are checked in.

## 1. Host-side unit tests

Run without ESP-IDF hardware where practical.

### HID parser
- Descriptor item parsing, collections, Push/Pop, Report IDs and per-type offsets.
- Signed/unsigned bitfields crossing byte boundaries.
- Bounds checks, malformed descriptors, capacity exhaustion and fuzzing.
- Read/modify/write preserves unrelated bits.

### Gateway and routing
- Power-up mode is Passthrough.
- G1 rising edge toggles exactly once; release does not toggle.
- G1 is not exposed to normal mapping.
- Enabled mapping consumes the matching semantic input.
- Unmapped inputs are forwarded unchanged.
- Mixed reports preserve unrelated/unmapped fields.
- Disconnect and malformed reports clear held state.

### Mapping
- WASD cardinal and diagonal vectors.
- Opposing-key cancellation and release behavior.
- Mouse scaling, inversion, deadband, gamma and radial limit.
- Digital Momentary, Toggle, Pulse, Set and Reset actions.
- Debounce, repeat-report handling and disconnect cleanup.
- Invalid/conflicting JSON mappings are rejected.

### Motion
- Timestamp-aware derivative estimation under noise and jitter.
- Robust statistics and sparse-bin confidence.
- Learned model remains immutable after tuning.
- Trajectory position/velocity/acceleration continuity.
- Jerk bounds under target changes, reversal, cancellation and return-to-center.

### Persistence
- Schema validation, incompatible versions and migration.
- Failed reload leaves prior configuration active.
- Interrupted write recovery and backup restore.
- Raw learned model remains unchanged after tuning.

## 2. Hardware integration tests

- ESP32-P4 board revision and simultaneous USB Host/Device roles.
- Monitor hub, G815 and G502 enumeration and disconnect/reconnect.
- PC-facing composite HID enumeration.
- Passthrough equality and latency measurements.
- G1 input capture, toggle behavior and LED feedback.
- G HUB control/Feature/vendor traffic and actual supported feature set.
- ADS7128 channel scan timing and analog calibration.
- DAC7578 voltage range, neutral calibration and load behavior.
- Twelve digital outputs: polarity, safe state and action timing.
- Long-duration operation, queue saturation, I²C errors, brownout and recovery.

## 3. Evidence policy

Record board revision, firmware commit, ESP-IDF version, device firmware, G HUB version, capture source, test procedure and result. Distinguish observed, inferred and unverified behavior. A successful unit test does not establish hardware compatibility.
