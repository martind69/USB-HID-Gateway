#pragma once
#include <stdbool.h>
#include "motion_profile_storage.h"
esp_err_t motion_profile_boot_load(const char *json_path,const char *binary_path,motion_profile_t *profile,bool *rebuilt);
