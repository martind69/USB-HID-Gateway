#pragma once
#include <stdint.h>
typedef struct {
    uint64_t frames_sent;
    uint64_t frames_received;
    uint64_t crc_errors;
    uint64_t sequence_errors;
    uint64_t timeouts;
    uint64_t queue_overflows;
    uint64_t recoveries;
    uint32_t latency_min_us;
    uint32_t latency_max_us;
    uint64_t latency_sum_us;
} gateway_link_metrics_t;
void gateway_link_metrics_reset(void);
gateway_link_metrics_t gateway_link_metrics_snapshot(void);
void gateway_link_metrics_on_tx(void);
void gateway_link_metrics_on_rx(uint32_t latency_us);
void gateway_link_metrics_on_crc_error(void);
void gateway_link_metrics_on_sequence_error(void);
void gateway_link_metrics_on_timeout(void);
void gateway_link_metrics_on_queue_overflow(void);
void gateway_link_metrics_on_recovery(void);
