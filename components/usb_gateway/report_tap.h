#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "input_state.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Read-only tap of all HID input reports (mirrored and "tap" devices).
 * Reports are copied into a queue and decoded in a separate task with the
 * runtime report descriptor (hid_report_decoder); the mirror path is never
 * delayed. The decoded state feeds task 3 (analog_engine).
 */

typedef void (*report_tap_edge_handler_t)(
    input_usage_t usage,
    bool pressed,
    uint8_t modifiers);

typedef struct {
    uint32_t submitted;
    uint32_t processed;
    uint32_t dropped;
    uint32_t undecodable;
} report_tap_stats_t;

esp_err_t report_tap_init(void);

/* Called for every key/button change, in the tap task; must not block. */
void report_tap_set_edge_handler(report_tap_edge_handler_t handler);

/* Non-blocking; called from the HID host callback. */
void report_tap_submit(uint8_t interface_id, const uint8_t *report, size_t length);

/* Releases all usages of a removed interface. */
void report_tap_interface_removed(uint8_t interface_id);

/* Copies the input state and consumes the accumulated relative motion. */
void report_tap_take_snapshot(input_state_t *snapshot);

void report_tap_get_stats(report_tap_stats_t *stats);

#ifdef __cplusplus
}
#endif
