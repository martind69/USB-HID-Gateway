#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "hid_report_decoder.h"
#include "input_state.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * M7: neutralization of mapped HID usages in mirrored input reports.
 * Pure C, no ESP-IDF dependency, host-testable.
 *
 * While active (analog engine in GENERATE), every usage that output.json
 * maps to a DAC channel or GPIO is removed from the report before it is
 * mirrored to the PC. Everything else stays byte-identical.
 *
 *     variable field, absolute   value -> 0 (or logical minimum if 0 is
 *                                outside the logical range)
 *     variable field, relative   value -> 0
 *     array field                mapped usages removed, remaining slots
 *                                shifted forward, freed slots -> empty value
 *     constant field             never touched
 *
 * Fail-open per report (report forwarded unchanged, counted):
 *     - report descriptor not parsed or layout truncated
 *     - report shorter than the fields of its Report ID require
 *     - array field without a representable empty value or > 64 slots
 *     - report longer than 64 bytes
 * Reports whose Report ID has no input field are forwarded unchanged and
 * not counted (vendor / unknown reports).
 *
 * Thread safety: rules are set once before the filter is activated; the
 * active flag and the counters are single-word variables.
 */
#define REPORT_FILTER_MAX_USAGES    32U

typedef enum {
    REPORT_FILTER_UNCHANGED = 0,    /* nothing mapped in this report */
    REPORT_FILTER_MODIFIED,         /* at least one mapped usage neutralized */
    REPORT_FILTER_FAIL_OPEN,        /* forwarded unchanged, see above */
} report_filter_result_t;

typedef struct {
    uint32_t inspected;
    uint32_t modified;
    uint32_t fail_open;
} report_filter_stats_t;

/* Removes all rules and deactivates the filter. */
void report_filter_reset(void);

/* Adds a usage (page:usage). Duplicates and usage 0 are ignored. */
bool report_filter_add_usage(input_usage_t usage);

/* Number of rules (diagnostics). */
size_t report_filter_rule_count(void);

void report_filter_set_active(bool active);
bool report_filter_is_active(void);

/*
 * Neutralizes mapped usages in place. report includes the Report ID byte
 * if the layout uses report IDs. Does nothing while inactive.
 * parsed: hid_decoder_parse() returned HID_DECODER_OK for this layout.
 */
report_filter_result_t report_filter_apply(
    const hid_decoder_layout_t *layout,
    bool parsed,
    uint8_t *report,
    size_t length);

void report_filter_get_stats(report_filter_stats_t *stats);

#ifdef __cplusplus
}
#endif
