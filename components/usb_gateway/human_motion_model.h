#pragma once
#include "motion_model_tuner.h"
typedef struct{const learned_motion_model_t*learned;motion_model_tuning_t tuning;}human_motion_model_t;void human_motion_model_init(human_motion_model_t*m,const learned_motion_model_t*l);motion_limits_t human_motion_model_limits(const human_motion_model_t*m,motion_vec2_t d,float a);
