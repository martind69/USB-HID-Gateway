#pragma once
#include "motion_types.h"
#define RADIAL_ENVELOPE_BINS 360U
typedef struct{float radius[360];uint16_t samples[360];float safety_factor;bool valid;}radial_envelope_t;void radial_envelope_reset(radial_envelope_t*e,float sf);void radial_envelope_observe(radial_envelope_t*e,motion_vec2_t v);void radial_envelope_finalize(radial_envelope_t*e);motion_vec2_t radial_envelope_normalize(const radial_envelope_t*e,motion_vec2_t v);
