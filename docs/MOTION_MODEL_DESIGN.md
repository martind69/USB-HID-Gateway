# Motion Model and Trajectory Design

Status: Design specification. Algorithms and resource limits remain to be validated on the ESP32-P4 target.

## 1. Separation of responsibilities

- InputMappingEngine generates normalized XY targets.
- HumanTrajectoryGenerator generates a time-continuous XY path toward the target.
- AnalogVectorIO converts normalized vectors to calibrated physical DAC values and ADC observations.
- KinematicEstimator2D estimates position, velocity, acceleration and jerk.
- ContinuousMotionLearner records observations only during an explicitly started learning session.
- LearnedMotionModel stores immutable raw learning results.
- MotionModelTuner applies independent tuning values.
- HumanMotionModel exposes effective parameters to the trajectory generator.

## 2. Vector model

Each joystick is a two-dimensional vector:

`p(t) = [x(t), y(t)]ᵀ`

Do not plan X and Y independently. Keep geometric path and time parameterization conceptually separate:

`p(t) = C(s(t))`

where C describes the XY path and s(t) describes progress along it.

## 3. Kinematic estimation

Estimate:
- position p
- velocity v
- acceleration a
- jerk j

Use measured timestamps rather than assuming perfectly periodic sampling. Apply filtering before numerical differentiation; differentiation amplifies sensor noise and timing jitter. Filter delay and estimator bandwidth must be measured and documented.

## 4. Motion phases and geometry

Candidate phases:
- Idle
- Departure
- Cruise
- Braking
- ReturnToCenter
- DirectionChange
- Tangential
- FineAdjustment

Radial and tangential velocity components may be computed relative to the current radial direction:

`v_r = v · r_hat`

`v_t = v · t_hat`

Phase classification thresholds must be derived from recorded data and validated; do not hard-code unverified human-performance limits.

## 5. Manual learning lifecycle

Learning is inactive by default. A user explicitly starts and stops it:

`start → update(observation) … → stop → immutable LearnedMotionModel`

No background or continuous adaptation is allowed after stop. The learning session must record sampling metadata, calibration identity, software/schema version, accepted/rejected sample counts and confidence indicators.

Robust statistics are preferred over raw extrema. Candidate statistics include median, Q90, Q95, Q99, mean and standard deviation. Velocity/acceleration/deceleration may use Q95 and jerk Q90 as initial candidates, but final quantiles require sample-size and repeatability validation.

## 6. Learned profile dimensions

A proposed model uses:
- 36 angular bins
- 5 radial bins
- 8 motion phases

This yields 1440 cells. Treat this as a candidate representation, not a fixed contract, until memory cost, sparse-cell behavior and sample sufficiency are measured. Store counts/confidence per cell and define interpolation for empty or low-confidence cells.

## 7. Raw learning and tuning

Raw LearnedMotionModel data is immutable after learner stop. Fine-tuning must never rewrite the raw model.

For each tunable parameter:

`effective = learned × scale + offset`

An optional absolute override may replace the calculated value. Tuning includes explicit validation bounds and provenance. HumanMotionModel combines raw learning and tuning into an effective immutable snapshot.

## 8. Trajectory continuity and constraints

For a target change, require continuity:

`p(t-) = p(t+)`
`v(t-) = v(t+)`
`a(t-) = a(t+)`

and enforce jerk bounds from the effective model. Target updates must not reset the current trajectory state to a new start point. The planner must support:
- target updates during motion
- direction reversal
- braking and return to center
- target hold
- cancellation on mode transition/disconnect
- bounded execution time and deterministic update period

A jerk-limited online trajectory algorithm must be selected and tested. The design must define behavior when a new target is infeasible under current velocity/acceleration; continuity takes priority over instant target arrival.

## 9. Validation

Host-side deterministic tests should cover vector normalization, estimator behavior under noise/jitter, phase transitions, sparse learned bins, tuning immutability, target changes and continuity. Numerical tolerances and maximum jerk must be explicit in test fixtures.
