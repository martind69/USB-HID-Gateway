#pragma once
#include "kinematic_estimator.h"
#include "learned_motion_model.h"
typedef struct{bool active;kinematic_estimator_t estimator;learned_motion_model_t working;}continuous_motion_learner_t;void continuous_motion_learner_init(continuous_motion_learner_t*l,float alpha);void continuous_motion_learner_start(continuous_motion_learner_t*l);void continuous_motion_learner_add(continuous_motion_learner_t*l,motion_vec2_t p,uint64_t t);bool continuous_motion_learner_stop(continuous_motion_learner_t*l,learned_motion_model_t*out);
