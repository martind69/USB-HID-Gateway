#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "motion_profile_storage.h"
esp_err_t profile_export_init(motion_profile_t *profile,const char *path);void profile_export_on_key(uint8_t modifiers,uint8_t usage,bool pressed);bool profile_export_busy(void);esp_err_t profile_export_last_error(void);
