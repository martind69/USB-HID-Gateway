#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
esp_err_t sd_storage_mount(void);void sd_storage_unmount(void);bool sd_storage_is_mounted(void);esp_err_t sd_storage_ensure_directories(void);uint64_t sd_storage_free_bytes(void);
