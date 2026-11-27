# Configuration and Persistence Formats

Status: Design baseline. Concrete JSON Schemas and firmware parser limits are to be versioned before implementation.

## 1. Filesystem layout

Proposed persistent layout:

```text
/sdcard/gateway/gateway_config.json
/sdcard/mapping/input_mapping.json
/sdcard/motion/joystick_learned.json
/sdcard/motion/joystick_tuning.json
/sdcard/motion/backup/learned_<timestamp>.json
```

## 2. Versioning

Every file must include:
- `schema_version`
- `created_utc` or equivalent timestamp where meaningful
- `device/calibration identity` where the data depends on hardware calibration
- explicit units for physical quantities
- source firmware version for diagnostics

Unknown major schema versions must be rejected. A compatible minor version may be accepted only when unknown fields can safely be ignored. Migration must be explicit and testable; do not silently reinterpret old values.

## 3. Runtime configuration

Gateway and mapping configuration may be changed without recompiling firmware. Load into a candidate object, validate all fields and cross-references, then atomically activate the complete snapshot. A failed reload leaves the previous valid configuration active.

G1 remains reserved for mode control and cannot be remapped by the ordinary mapping file.

## 4. Learned model immutability

`joystick_learned.json` stores the original frozen learning result. Tuning changes belong only in `joystick_tuning.json`. Loading, tuning, exporting or backing up a model must not mutate the raw learning dataset.

## 5. Safe writes and backup

- Write to a temporary file in the same filesystem.
- Flush file contents and metadata where supported.
- Validate the written file before replacement.
- Use the filesystem's atomic rename/replace semantics.
- Retain a bounded number of timestamped backups.
- Verify checksums or integrity metadata before restore.
- Never destroy the last known-good file before a replacement is validated.

## 6. Validation

Validate types, required fields, finite numeric values, ranges, array sizes, channel indices, duplicate mappings, reserved G1 assignment, model dimensions, units and schema compatibility. Bound file size and parser allocation. Treat malformed/truncated files as invalid and report the reason.
