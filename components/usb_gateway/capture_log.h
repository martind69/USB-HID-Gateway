#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Binary event log on the SD card (formerly verbose_capture).
 * Active only with CONFIG_USB_GATEWAY_CAPTURE_LEVEL_SD; otherwise all
 * functions return ESP_ERR_NOT_SUPPORTED / false.
 *
 * Session directory: /sdcard/logs/session-<boot time in us>/events-NNN.bin
 * Record: 16-byte header + 64-byte payload (see capture_log.c).
 */

#define CAPTURE_LOG_ROOT    "/sdcard/logs"

/* Creates the writer task and starts a session if the SD card is mounted. */
esp_err_t capture_log_init(void);

/* duration_s = 0: run until capture_log_stop() or the size budget is used. */
esp_err_t capture_log_start(uint32_t duration_s);
esp_err_t capture_log_stop(void);

bool capture_log_write(
    uint16_t source,
    uint16_t event,
    uint32_t sequence,
    const void *payload,
    size_t length);

uint32_t capture_log_dropped(void);

#ifdef __cplusplus
}
#endif
