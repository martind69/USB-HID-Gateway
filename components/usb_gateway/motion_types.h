#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef struct{float x,y;}motion_vec2_t;typedef struct{motion_vec2_t position,velocity,acceleration,jerk;uint64_t timestamp_us;}motion_state2_t;typedef struct{float velocity_max,acceleration_max,jerk_max,deadzone,response,damping;}motion_limits_t;static inline float motion_clampf(float v,float a,float b){return v<a?a:(v>b?b:v);}
