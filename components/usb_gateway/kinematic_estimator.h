#pragma once
#include "motion_types.h"
typedef struct{motion_state2_t state;float alpha;bool initialized;}kinematic_estimator_t;void kinematic_estimator_init(kinematic_estimator_t*e,float alpha);motion_state2_t kinematic_estimator_update(kinematic_estimator_t*e,motion_vec2_t p,uint64_t t);
