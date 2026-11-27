#pragma once
#include "motion_types.h"
typedef struct{motion_vec2_t sum,center;uint32_t count;bool valid;}center_calibration_t;void center_calibration_reset(center_calibration_t*c);void center_calibration_add(center_calibration_t*c,motion_vec2_t s);bool center_calibration_finish(center_calibration_t*c,uint32_t n);motion_vec2_t center_calibration_apply(const center_calibration_t*c,motion_vec2_t s,float dz);
