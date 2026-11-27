#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "p4_s3_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * S3 side of the mirror session. Receives the descriptors of the device
 * attached to the P4 (BEGIN, DESCRIPTOR chunks, COMMIT) and hands them to
 * the shared mirror device (component usb_mirror).
 *
 * All functions except init are called from the SPI link task only and
 * return a p4_s3_status_code_t for the STATUS response.
 */

/* Installs TinyUSB (PC port stays disconnected until COMMIT). */
esp_err_t s3_mirror_session_init(void);

uint8_t s3_mirror_session_begin(const uint8_t *payload, size_t length);
uint8_t s3_mirror_session_descriptor(const uint8_t *payload, size_t length);
uint8_t s3_mirror_session_commit(void);
uint8_t s3_mirror_session_end(void);

uint8_t s3_mirror_session_input(
    uint8_t interface_number,
    const uint8_t *report,
    size_t length);

void s3_mirror_session_stats(p4_s3_mirror_stats_t *stats);

#ifdef __cplusplus
}
#endif
