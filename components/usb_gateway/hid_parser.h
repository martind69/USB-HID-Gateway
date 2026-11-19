#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HID_PARSER_MAX_DESCRIPTOR_SIZE 1024U
#define HID_PARSER_MAX_REPORTS 24U
#define HID_PARSER_MAX_FIELDS 64U
#define HID_PARSER_GLOBAL_STACK_DEPTH 4U
#define HID_PARSER_COLLECTION_DEPTH 8U
#define HID_PARSER_LOCAL_USAGE_CAPACITY 64U

typedef enum {
	GATEWAY_HID_REPORT_TYPE_INPUT = 0,
	GATEWAY_HID_REPORT_TYPE_OUTPUT,
	GATEWAY_HID_REPORT_TYPE_FEATURE
} gateway_hid_report_type_t;

typedef enum {
	HID_PARSER_OK = 0,
	HID_PARSER_ERR_ARGUMENT,
	HID_PARSER_ERR_DESCRIPTOR_SIZE,
	HID_PARSER_ERR_MALFORMED,
	HID_PARSER_ERR_CAPACITY,
	HID_PARSER_ERR_GLOBAL_STACK,
	HID_PARSER_ERR_COLLECTION,
	HID_PARSER_ERR_REPORT_ID_MIX
} hid_parser_result_t;

typedef struct {
	gateway_hid_report_type_t type;
	uint8_t report_id;
	uint16_t payload_bits;
	uint16_t payload_bytes;
	uint16_t field_count;
} hid_parser_report_t;

typedef struct {
	gateway_hid_report_type_t type;
	uint8_t report_id;
	uint16_t usage_page;
	uint16_t usage;
	uint16_t usage_min;
	uint16_t usage_max;
	uint16_t bit_offset;
	uint16_t bit_size;
	uint16_t report_count;
	int32_t logical_min;
	int32_t logical_max;
	uint32_t main_flags;
	bool is_array;
	bool is_relative;
	bool is_constant;
} hid_parser_field_t;

typedef struct {
	uint32_t bit_count;
	uint8_t report_id;
} hid_parser_report_cursor_t;

/* One caller-owned context is required for each active HID interface. */
typedef struct {
	uint8_t descriptor[HID_PARSER_MAX_DESCRIPTOR_SIZE];
	size_t descriptor_length;
	hid_parser_report_t reports[HID_PARSER_MAX_REPORTS];
	size_t report_count;
	hid_parser_field_t fields[HID_PARSER_MAX_FIELDS];
	size_t field_count;
	hid_parser_report_cursor_t cursors[HID_PARSER_MAX_REPORTS];
	size_t cursor_count;
	bool has_report_ids;
	bool has_unnumbered_reports;
	uint16_t top_level_usage_page;
	uint16_t top_level_usage;
} hid_parser_context_t;

void hid_parser_init(hid_parser_context_t *context);
hid_parser_result_t hid_parser_parse_report(
	hid_parser_context_t *context,
	const uint8_t *descriptor,
	size_t length);
size_t hid_parser_get_report_count(const hid_parser_context_t *context);
const hid_parser_report_t *hid_parser_get_report(
	const hid_parser_context_t *context,
	size_t index);
size_t hid_parser_get_field_count(const hid_parser_context_t *context);
const hid_parser_field_t *hid_parser_get_field(
	const hid_parser_context_t *context,
	size_t index);
const uint8_t *hid_parser_get_descriptor(
	const hid_parser_context_t *context,
	size_t *length);
bool hid_parser_read_field_value(
	const hid_parser_field_t *field,
	const uint8_t *payload,
	size_t payload_length,
	uint16_t element_index,
	int32_t *value);

#ifdef __cplusplus
}
#endif
