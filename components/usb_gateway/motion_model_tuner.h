#pragma once
#include "learned_motion_model.h"
typedef struct{float velocity_scale,acceleration_scale,jerk_scale,deadzone_override,response_scale,damping_override;}motion_model_tuning_t;void motion_model_tuning_defaults(motion_model_tuning_t*t);motion_limits_t motion_model_tuner_apply(motion_limits_t l,const motion_model_tuning_t*t);
