#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Generic HID report descriptor decoder (input reports only).
 * Pure C, no ESP-IDF dependency, host-testable. Replaces hid_parser.
 *
 * Decoding a report yields one callback per usage:
 *     variable field: every element, value = field content (sign-extended
 *                     if logical minimum < 0)
 *     array field:    every non-empty slot, value = 1 (usage is active)
 */

#define HID_DECODER_MAX_FIELDS      64U
#define HID_DECODER_MAX_USAGES      16U

#define HID_FIELD_FLAG_CONSTANT     0x01U
#define HID_FIELD_FLAG_VARIABLE     0x02U
#define HID_FIELD_FLAG_RELATIVE     0x04U

typedef struct {
    uint8_t report_id;              /* 0 = report without ID */
    uint8_t flags;
    uint8_t bit_size;               /* 1..32 */
    uint8_t usage_count;            /* explicit usages, 0 = use min/max */
    uint16_t bit_offset;            /* relative to the first byte after the ID */
    uint16_t report_count;
    uint16_t usage_page;
    uint16_t usage_minimum;
    uint16_t usage_maximum;
    int32_t logical_minimum;
    int32_t logical_maximum;
    uint16_t usages[HID_DECODER_MAX_USAGES];
} hid_decoder_field_t;

typedef struct {
    bool uses_report_ids;
    bool truncated;                 /* more fields than HID_DECODER_MAX_FIELDS */
    uint8_t field_count;
    hid_decoder_field_t fields[HID_DECODER_MAX_FIELDS];
} hid_decoder_layout_t;

typedef enum {
    HID_DECODER_OK = 0,
    HID_DECODER_ERR_ARGUMENT,
    HID_DECODER_ERR_DESCRIPTOR,
} hid_decoder_result_t;

typedef void (*hid_decoder_usage_cb_t)(
    uint16_t usage_page,
    uint16_t usage,
    int32_t value,
    bool relative,
    void *context);

hid_decoder_result_t hid_decoder_parse(
    const uint8_t *descriptor,
    size_t length,
    hid_decoder_layout_t *layout);

/* report includes the Report ID byte if the layout uses report IDs. */
hid_decoder_result_t hid_decoder_decode(
    const hid_decoder_layout_t *layout,
    const uint8_t *report,
    size_t length,
    hid_decoder_usage_cb_t callback,
    void *context);

/* Boot protocol fallback: top-level usage Generic Desktop / Keyboard or Mouse. */
bool hid_decoder_has_usage(
    const hid_decoder_layout_t *layout,
    uint16_t usage_page,
    uint16_t usage);

#ifdef __cplusplus
}
#endif
