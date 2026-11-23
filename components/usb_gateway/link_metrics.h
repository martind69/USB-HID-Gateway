#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * P4/S3 SPI link counters. Written by the link task, read by diagnostics.
 * Replaces gateway_test_metrics (formerly consumed by the removed self-test).
 */

typedef struct {
    uint32_t tx_frames;
    uint32_t rx_status;
    uint32_t crc_errors;
    uint32_t sequence_errors;
    uint32_t timeouts;
    uint32_t queue_overflows;
    uint32_t round_trip_last_us;
    uint32_t round_trip_max_us;
    uint64_t round_trip_sum_us;
} link_metrics_t;

void link_metrics_on_tx(void);
void link_metrics_on_rx(uint32_t round_trip_us);
void link_metrics_on_crc_error(void);
void link_metrics_on_sequence_error(void);
void link_metrics_on_timeout(void);
void link_metrics_on_queue_overflow(void);

/* Copies the current counters; values may be slightly inconsistent. */
void link_metrics_get(link_metrics_t *metrics);

#ifdef __cplusplus
}
#endif
