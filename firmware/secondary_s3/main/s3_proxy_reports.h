#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "p4_s3_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * S3 -> P4 uplink queue. The S3 is SPI slave; queued frames are delivered
 * when the P4 polls (idle NOP transactions).
 */

esp_err_t s3_proxy_reports_init(void);

/*
 * Queues a PC -> device SET_REPORT for transfer to the P4.
 * data contains the complete report; for numbered reports data[0] is the
 * Report ID. report_type: 2 = output, 3 = feature. Called from the TinyUSB task.
 */
esp_err_t s3_proxy_send_set_report(
    uint8_t interface_number,
    uint8_t report_id,
    uint8_t report_type,
    const uint8_t *data,
    size_t length);

/* Queues an event frame without payload (e.g. P4_S3_MSG_MIRROR_REQUEST). */
esp_err_t s3_proxy_send_event(uint8_t type);

/* Fetches the next queued frame. Called from the SPI link task. */
bool s3_proxy_next_frame(p4_s3_frame_t *frame);

uint32_t s3_proxy_dropped(void);

#ifdef __cplusplus
}
#endif
