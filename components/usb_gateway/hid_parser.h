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

typedef enum {
	HID_REPORT_TYPE_INPUT = 0,
	HID_REPORT_TYPE_OUTPUT,
	HID_REPORT_TYPE_FEATURE
} hid_report_type_t;

typedef enum {
	HID_PARSER_OK = 0,
	HID_PARSER_ERR_ARGUMENT,
	HID_PARSER_ERR_DESCRIPTOR_SIZE,
	HID_PARSER_ERR_MALFORMED,
	HID_PARSER_ERR_CAPACITY,
	HID_PARSER_ERR_GLOBAL_STACK,
	HID_PARSER_ERR_COLLECTION
} hid_parser_result_t;

typedef struct {
	hid_report_type_t type;
	uint8_t report_id;
	uint16_t payload_bits;
	uint16_t payload_bytes;
	uint16_t field_first;
	uint16_t field_count;
} hid_parser_report_t;

typedef struct {
	hid_report_type_t type;
	uint8_t report_id;
	uint16_t usage_page;
	uint16_t usage;
	uint16_t usage_min;
	uint16_t usage_max;
	uint16_t bit_offset;
	uint16_t bit_size;
	uint16_t report_count;
	uint32_t main_flags;
	bool is_array;
	bool is_relative;
	bool is_constant;
} hid_parser_field_t;

bool hid_parser_init(void);
hid_parser_result_t hid_parser_parse_report(
	const uint8_t *descriptor,
	size_t length);
size_t hid_parser_get_report_count(void);
const hid_parser_report_t *hid_parser_get_report(size_t index);
size_t hid_parser_get_field_count(void);
const hid_parser_field_t *hid_parser_get_field(size_t index);
const uint8_t *hid_parser_get_descriptor(size_t *length);

#ifdef __cplusplus
}
#endif
