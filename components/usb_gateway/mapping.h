#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAPPING_MAX_FIELDS_PER_REPORT 64U
#define MAPPING_MAX_RULES 24U

typedef enum {
	MAPPING_MODE_PASSTHROUGH = 0,
	MAPPING_MODE_PROXY
} mapping_mode_t;

typedef struct {
	uint16_t usage_page;
	uint16_t usage;
	uint16_t bit_offset;
	uint8_t bit_size;
	int32_t neutral_value;
	int32_t value;
	bool is_array;
	bool is_relative;
} mapping_report_field_t;

typedef struct {
	uint16_t usage_page;
	uint16_t usage;
	bool enabled;
} mapping_rule_t;

typedef struct {
	size_t rule_count;
	mapping_rule_t rules[MAPPING_MAX_RULES];
} mapping_config_t;

typedef struct {
	uint16_t usage_page;
	uint16_t usage;
	int32_t value;
	bool is_array;
	bool is_relative;
} mapping_event_t;

typedef void (*mapping_event_callback_t)(
	const mapping_event_t *event,
	void *context);

typedef enum {
	MAPPING_OK = 0,
	MAPPING_ERR_ARGUMENT,
	MAPPING_ERR_CAPACITY,
	MAPPING_ERR_FIELD_RANGE,
	MAPPING_ERR_FIELD_SIZE
} mapping_result_t;

/* Configuration is copied only after complete validation. */
mapping_result_t mapping_set_config(const mapping_config_t *config);

/* The callback runs in the caller's task context and must not block. */
void mapping_set_event_callback(
	mapping_event_callback_t callback,
	void *context);

/*
 * Routes one HID Input report payload.
 * The payload excludes the Report ID byte; report_id is carried separately
 * by the caller and is not modified here.
 *
 * In Passthrough mode, output_payload is an exact copy of input_payload.
 * In Proxy mode, only configured fields are replaced by their neutral value.
 * Every other bit, including unknown fields and padding, is preserved.
 *
 * For Array fields, the parser must resolve usage_page/usage to the active
 * usage represented by the current array slot. Empty slots must use usage 0.
 * For Variable fields, usage_page/usage identify the descriptor field even
 * when its value represents a release/neutral state.
 */
mapping_result_t mapping_route_report(
	mapping_mode_t mode,
	const uint8_t *input_payload,
	size_t payload_length,
	const mapping_report_field_t *fields,
	size_t field_count,
	uint8_t *output_payload,
	size_t output_capacity);

#ifdef __cplusplus
}
#endif
