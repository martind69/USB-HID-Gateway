#include "link_metrics.h"

#include <stddef.h>

static volatile link_metrics_t s_metrics;

void link_metrics_on_tx(void)
{
    ++s_metrics.tx_frames;
}

void link_metrics_on_rx(uint32_t round_trip_us)
{
    ++s_metrics.rx_status;
    s_metrics.round_trip_last_us = round_trip_us;
    s_metrics.round_trip_sum_us += round_trip_us;
    if (round_trip_us > s_metrics.round_trip_max_us) {
        s_metrics.round_trip_max_us = round_trip_us;
    }
}

void link_metrics_on_crc_error(void)
{
    ++s_metrics.crc_errors;
}

void link_metrics_on_sequence_error(void)
{
    ++s_metrics.sequence_errors;
}

void link_metrics_on_timeout(void)
{
    ++s_metrics.timeouts;
}

void link_metrics_on_queue_overflow(void)
{
    ++s_metrics.queue_overflows;
}

void link_metrics_get(link_metrics_t *metrics)
{
    if (metrics == NULL) {
        return;
    }
    metrics->tx_frames = s_metrics.tx_frames;
    metrics->rx_status = s_metrics.rx_status;
    metrics->crc_errors = s_metrics.crc_errors;
    metrics->sequence_errors = s_metrics.sequence_errors;
    metrics->timeouts = s_metrics.timeouts;
    metrics->queue_overflows = s_metrics.queue_overflows;
    metrics->round_trip_last_us = s_metrics.round_trip_last_us;
    metrics->round_trip_max_us = s_metrics.round_trip_max_us;
    metrics->round_trip_sum_us = s_metrics.round_trip_sum_us;
}
