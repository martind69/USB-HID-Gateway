#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "p4_s3_protocol.h"

typedef struct {
    p4_s3_ota_phase_t phase;
    uint32_t bytes_written;
    uint32_t image_size;
    uint8_t progress_percent;
    esp_err_t error;
    char version[P4_S3_OTA_VERSION_LENGTH];
} s3_ota_progress_t;

typedef void (*s3_ota_progress_callback_t)(
    const s3_ota_progress_t *progress);

esp_err_t s3_ota_manager_init(void);
void s3_ota_manager_set_progress_callback(
    s3_ota_progress_callback_t callback);
esp_err_t s3_ota_manager_start_if_requested(void);
esp_err_t s3_ota_manager_start(const char *image_path);
bool s3_ota_manager_running(void);
