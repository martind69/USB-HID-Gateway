#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "p4_s3_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t sequence;
    uint8_t status;
    uint32_t round_trip_us;
    uint8_t payload_length;
    uint8_t payload[P4_S3_PAYLOAD_SIZE];
} p4_s3_ack_t;

/*
 * Unsolicited S3 -> P4 event (currently P4_S3_MSG_MIRROR_REQUEST).
 * Called in the link task; must not block.
 */
typedef void (*p4_s3_link_event_handler_t)(
    uint8_t type,
    const uint8_t *payload,
    size_t length);

esp_err_t p4_s3_link_init(void);
void p4_s3_link_start(void);

void p4_s3_link_set_event_handler(p4_s3_link_event_handler_t handler);

bool p4_s3_link_enqueue(
    uint8_t type,
    const uint8_t *payload,
    size_t length);

bool p4_s3_link_wait_status(
    p4_s3_ack_t *acknowledgement,
    uint32_t timeout_ms);

esp_err_t p4_s3_link_command(
    uint8_t type,
    const uint8_t *payload,
    size_t length,
    p4_s3_ack_t *acknowledgement,
    uint32_t timeout_ms);

esp_err_t p4_s3_link_begin_exclusive(void);
void p4_s3_link_end_exclusive(void);

uint32_t p4_s3_link_dropped(void);

#ifdef __cplusplus
}
#endif
