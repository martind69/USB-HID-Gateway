#pragma once
#include <stdint.h>
#include "motion_types.h"
#define MOTION_ANGLE_BINS 360U
#define MOTION_MAG_BINS 4U
#define MOTION_MODEL_CELLS (MOTION_ANGLE_BINS * MOTION_MAG_BINS)
typedef struct { float v_sum,a_sum,j_sum,v_peak,a_peak,j_peak; uint32_t samples; } learned_motion_cell_t;
typedef struct { uint32_t version; learned_motion_cell_t cell[MOTION_MODEL_CELLS]; uint64_t total_samples; } learned_motion_model_t;
void learned_motion_model_reset(learned_motion_model_t *model);
void learned_motion_model_observe(learned_motion_model_t *model,const motion_state2_t *state);
motion_limits_t learned_motion_model_query(const learned_motion_model_t *model,motion_vec2_t direction,float magnitude);
