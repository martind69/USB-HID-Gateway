#pragma once
#include "motion_profile_storage.h"
#include "esp_err.h"
esp_err_t profile_export_init(motion_profile_t*p,const char*path);void profile_export_on_key(uint8_t modifiers,uint8_t usage,bool pressed);
