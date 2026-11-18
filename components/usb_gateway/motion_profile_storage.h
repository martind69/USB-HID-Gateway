#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "learned_motion_model.h"
#include "motion_model_tuner.h"
#define MOTION_PROFILE_ENVELOPE_BINS 360U
#define MOTION_PROFILE_JSON_MAX_BYTES (512U*1024U)
typedef enum{MOTION_GPIO_PUSH_PULL=0,MOTION_GPIO_OPEN_DRAIN=1}motion_gpio_mode_t;typedef struct{motion_gpio_mode_t mode;bool active_low;}motion_gpio_behavior_t;typedef struct{uint8_t dac_channel,adc_channel;uint16_t neutral_code,safe_code;bool invert;}motion_analog_channel_t;typedef struct{uint16_t min_code,center_code,max_code;float deadzone;}motion_axis_calibration_t;typedef struct{motion_axis_calibration_t x,y;float radial_envelope[360];float safety_factor;}motion_stick_calibration_t;typedef struct{uint32_t schema_version;char id[32],name[65],description[257];float adc_full_scale_v,dac_reference_v,dac_output_min_v,dac_output_max_v;motion_analog_channel_t channel[8];motion_gpio_behavior_t sensor_power,fault_led;motion_stick_calibration_t left_stick,right_stick;motion_axis_calibration_t trigger[4];learned_motion_model_t learned;motion_model_tuning_t tuning;}motion_profile_t;esp_err_t motion_profile_load_json(const char*path,motion_profile_t*p);esp_err_t motion_profile_save_json_atomic(const char*path,const motion_profile_t*p);esp_err_t motion_profile_validate(const motion_profile_t*p,char*reason,size_t n);
