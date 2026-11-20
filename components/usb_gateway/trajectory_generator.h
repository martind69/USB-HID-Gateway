#pragma once
#include "human_motion_model.h"
typedef struct{motion_state2_t state;motion_vec2_t target;}trajectory_generator_t;void trajectory_generator_init(trajectory_generator_t*g,motion_vec2_t p,uint64_t t);void trajectory_generator_set_target(trajectory_generator_t*g,motion_vec2_t t);motion_state2_t trajectory_generator_step(trajectory_generator_t*g,const human_motion_model_t*m,uint64_t t);
