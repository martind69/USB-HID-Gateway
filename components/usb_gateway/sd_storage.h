#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* SD card (SDMMC 1-bit, FAT) mounted at /sdcard. Layout: gateway_paths.h. */

esp_err_t sd_storage_mount(void);
void sd_storage_unmount(void);
bool sd_storage_is_mounted(void);
esp_err_t sd_storage_ensure_directories(void);

/* Free bytes of the FAT volume; 0 if not mounted or unknown. */
uint64_t sd_storage_free_bytes(void);

#ifdef __cplusplus
}
#endif
