#include "analog_mapping.h"

#include <math.h>

#define TWO_PI 6.28318530718f

static float clamp(float value, float minimum, float maximum)
{
    return value < minimum ? minimum : (value > maximum ? maximum : value);
}

static uint16_t round_code(float code)
{
    return (uint16_t)clamp(code + 0.5f, 0.0f, 4095.0f);
}

uint16_t analog_mapping_axis_to_code(
    const motion_axis_calibration_t *calibration,
    float value,
    bool invert)
{
    if (calibration == NULL) {
        return 2048U;
    }
    value = clamp(invert ? -value : value, -1.0f, 1.0f);
    const float center = (float)calibration->center_code;
    const float span = value >= 0.0f
        ? (float)calibration->max_code - center
        : center - (float)calibration->min_code;
    return round_code(center + value * span);
}

float analog_mapping_code_to_axis(
    const motion_axis_calibration_t *calibration,
    uint16_t code,
    bool invert)
{
    if (calibration == NULL) {
        return 0.0f;
    }
    const float center = (float)calibration->center_code;
    const float delta = (float)code - center;
    const float span = delta >= 0.0f
        ? (float)calibration->max_code - center
        : center - (float)calibration->min_code;
    const float value = span > 0.0f ? clamp(delta / span, -1.0f, 1.0f) : 0.0f;
    return invert ? -value : value;
}

uint16_t analog_mapping_trigger_to_code(
    const motion_axis_calibration_t *calibration,
    float value,
    bool invert)
{
    if (calibration == NULL) {
        return 0U;
    }
    value = clamp(value, 0.0f, 1.0f);
    if (invert) {
        value = 1.0f - value;
    }
    const float minimum = (float)calibration->min_code;
    const float maximum = (float)calibration->max_code;
    return round_code(minimum + value * (maximum - minimum));
}

motion_vec2_t analog_mapping_unit_circle(motion_vec2_t value)
{
    const float magnitude = hypotf(value.x, value.y);
    if (magnitude > 1.0f) {
        value.x /= magnitude;
        value.y /= magnitude;
    }
    return value;
}

motion_vec2_t analog_mapping_envelope_scale(
    const float radius[MOTION_PROFILE_ENVELOPE_BINS],
    float safety_factor,
    motion_vec2_t value)
{
    value = analog_mapping_unit_circle(value);
    if (radius == NULL || (value.x == 0.0f && value.y == 0.0f)) {
        return value;
    }
    float angle = atan2f(value.y, value.x);
    if (angle < 0.0f) {
        angle += TWO_PI;
    }
    const unsigned bin = (unsigned)(angle * (float)MOTION_PROFILE_ENVELOPE_BINS / TWO_PI) %
        MOTION_PROFILE_ENVELOPE_BINS;
    float scale = radius[bin] > 0.0f ? radius[bin] : 1.0f;
    if (safety_factor > 0.0f) {
        scale *= safety_factor;
    }
    return (motion_vec2_t) {
        .x = clamp(value.x * scale, -1.0f, 1.0f),
        .y = clamp(value.y * scale, -1.0f, 1.0f),
    };
}
