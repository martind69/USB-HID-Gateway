#pragma once
#include "motion_types.h"
typedef struct { float velocity_scale,acceleration_scale,jerk_scale,deadzone_override,response_scale,damping_override; } motion_model_tuning_t;
void motion_model_tuning_defaults(motion_model_tuning_t *tuning);
motion_limits_t motion_model_tuner_apply(motion_limits_t learned,const motion_model_tuning_t *tuning);
