#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "p4_s3_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t s3_proxy_reports_init(void);

/*
 * Queues a PC -> G815 SET_REPORT for transfer to the P4.
 * data must contain the complete report; for numbered reports data[0] is the
 * Report ID. Called from the TinyUSB task.
 */
esp_err_t s3_proxy_send_set_report(
    uint8_t instance,
    uint8_t report_id,
    uint8_t report_type,
    const uint8_t *data,
    size_t length);

/* Fetches the next queued frame. Called from the SPI link task. */
bool s3_proxy_next_frame(p4_s3_frame_t *frame);

uint32_t s3_proxy_dropped(void);

#ifdef __cplusplus
}
#endif
