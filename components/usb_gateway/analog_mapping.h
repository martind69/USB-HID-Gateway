#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "motion_profile_storage.h"
#include "motion_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Conversion between normalized values and 12-bit converter codes using
 * the calibration of the motion profile. Pure C, host-testable.
 *
 * Axis:     -1..+1  <->  min_code..center_code..max_code (piecewise linear)
 * Trigger:   0..1   <->  min_code..max_code
 * invert mirrors the value before (axis/trigger -> code) or after
 * (code -> axis) the conversion.
 */

uint16_t analog_mapping_axis_to_code(
    const motion_axis_calibration_t *calibration,
    float value,
    bool invert);

float analog_mapping_code_to_axis(
    const motion_axis_calibration_t *calibration,
    uint16_t code,
    bool invert);

uint16_t analog_mapping_trigger_to_code(
    const motion_axis_calibration_t *calibration,
    float value,
    bool invert);

/*
 * Inverse of radial_envelope_normalize(): scales a normalized stick vector
 * (|v| <= 1) to the learned physical envelope (radius * safety factor).
 * Envelope bins <= 0 are treated as 1. Each axis is clamped to -1..+1.
 */
motion_vec2_t analog_mapping_envelope_scale(
    const float radius[MOTION_PROFILE_ENVELOPE_BINS],
    float safety_factor,
    motion_vec2_t value);

/* Limits a vector to the unit circle. */
motion_vec2_t analog_mapping_unit_circle(motion_vec2_t value);

#ifdef __cplusplus
}
#endif
