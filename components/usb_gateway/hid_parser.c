#include "hid_parser.h"

#include <string.h>

#define HID_GLOBAL_STACK_DEPTH 4U
#define HID_COLLECTION_DEPTH 8U
#define HID_LOCAL_USAGE_CAPACITY 64U

#define HID_ITEM_TYPE_MAIN 0U
#define HID_ITEM_TYPE_GLOBAL 1U
#define HID_ITEM_TYPE_LOCAL 2U

#define HID_MAIN_INPUT 8U
#define HID_MAIN_OUTPUT 9U
#define HID_MAIN_COLLECTION 10U
#define HID_MAIN_FEATURE 11U
#define HID_MAIN_END_COLLECTION 12U

#define HID_GLOBAL_USAGE_PAGE 0U
#define HID_GLOBAL_LOGICAL_MIN 1U
#define HID_GLOBAL_LOGICAL_MAX 2U
#define HID_GLOBAL_REPORT_SIZE 7U
#define HID_GLOBAL_REPORT_ID 8U
#define HID_GLOBAL_REPORT_COUNT 9U
#define HID_GLOBAL_PUSH 10U
#define HID_GLOBAL_POP 11U

#define HID_LOCAL_USAGE 0U
#define HID_LOCAL_USAGE_MIN 1U
#define HID_LOCAL_USAGE_MAX 2U

typedef struct {
	uint32_t usage_page;
	int32_t logical_min;
	uint32_t report_size;
	uint32_t report_count;
	uint8_t report_id;
} hid_global_state_t;

typedef struct {
	uint32_t bit_count;
	uint8_t report_id;
} hid_report_cursor_t;

static uint8_t s_descriptor[HID_PARSER_MAX_DESCRIPTOR_SIZE];
static size_t s_descriptor_length;
static hid_parser_report_t s_reports[HID_PARSER_MAX_REPORTS];
static size_t s_report_count;
static hid_parser_field_t s_fields[HID_PARSER_MAX_FIELDS];
static size_t s_field_count;
static hid_report_cursor_t s_cursors[HID_PARSER_MAX_REPORTS];
static size_t s_cursor_count;

static uint32_t read_unsigned(const uint8_t *data, uint8_t size)
{
	uint32_t value = 0U;
	uint8_t i;

	for (i = 0U; i < size; ++i) {
		value |= (uint32_t)data[i] << (8U * i);
	}
	return value;
}

static int32_t read_signed(const uint8_t *data, uint8_t size)
{
	uint32_t value = read_unsigned(data, size);

	if (size == 0U || size >= 4U) {
		return (int32_t)value;
	}
	if ((value & (1UL << (size * 8U - 1U))) != 0U) {
		value |= (~0UL) << (size * 8U);
	}
	return (int32_t)value;
}

static hid_parser_result_t get_report_cursor(
	hid_report_type_t type,
	uint8_t report_id,
	hid_report_cursor_t **cursor,
	hid_parser_report_t **report)
{
	size_t i;

	for (i = 0U; i < s_cursor_count; ++i) {
		if (s_cursors[i].report_id == report_id &&
			s_reports[i].type == type) {
			*cursor = &s_cursors[i];
			*report = &s_reports[i];
			return HID_PARSER_OK;
		}
	}

	if (s_cursor_count >= HID_PARSER_MAX_REPORTS) {
		return HID_PARSER_ERR_CAPACITY;
	}

	i = s_cursor_count++;
	memset(&s_cursors[i], 0, sizeof(s_cursors[i]));
	memset(&s_reports[i], 0, sizeof(s_reports[i]));
	s_cursors[i].report_id = report_id;
	s_reports[i].type = type;
	s_reports[i].report_id = report_id;
	s_reports[i].field_first = (uint16_t)s_field_count;
	s_report_count = s_cursor_count;
	*cursor = &s_cursors[i];
	*report = &s_reports[i];
	return HID_PARSER_OK;
}

bool hid_parser_init(void)
{
	s_descriptor_length = 0U;
	s_report_count = 0U;
	s_field_count = 0U;
	s_cursor_count = 0U;
	memset(s_reports, 0, sizeof(s_reports));
	memset(s_fields, 0, sizeof(s_fields));
	memset(s_cursors, 0, sizeof(s_cursors));
	return true;
}

hid_parser_result_t hid_parser_parse_report(
	const uint8_t *descriptor,
	size_t length)
{
	hid_global_state_t global = { 0 };
	hid_global_state_t global_stack[HID_GLOBAL_STACK_DEPTH];
	uint32_t local_usages[HID_LOCAL_USAGE_CAPACITY];
	size_t local_usage_count = 0U;
	uint32_t local_usage_min = 0U;
	uint32_t local_usage_max = 0U;
	uint8_t global_depth = 0U;
	uint8_t collection_depth = 0U;
	size_t offset = 0U;
	hid_parser_result_t result = HID_PARSER_OK;

	if (descriptor == NULL || length == 0U) {
		return HID_PARSER_ERR_ARGUMENT;
	}
	if (length > HID_PARSER_MAX_DESCRIPTOR_SIZE) {
		return HID_PARSER_ERR_DESCRIPTOR_SIZE;
	}

	hid_parser_init();
	memcpy(s_descriptor, descriptor, length);
	s_descriptor_length = length;

	while (offset < length) {
		uint8_t prefix = descriptor[offset++];
		uint8_t size_code;
		uint8_t item_size;
		uint8_t item_type;
		uint8_t item_tag;
		uint32_t value;
		const uint8_t *data;

		if (prefix == 0xFEU) {
			uint8_t long_size;
			if (offset + 2U > length) {
				result = HID_PARSER_ERR_MALFORMED;
				break;
			}
			long_size = descriptor[offset++];
			offset++;
			if (offset + long_size > length) {
				result = HID_PARSER_ERR_MALFORMED;
				break;
			}
			offset += long_size;
			continue;
		}

		size_code = prefix & 0x03U;
		item_size = (size_code == 3U) ? 4U : size_code;
		item_type = (prefix >> 2U) & 0x03U;
		item_tag = (prefix >> 4U) & 0x0FU;

		if (offset + item_size > length || item_type == 3U) {
			result = HID_PARSER_ERR_MALFORMED;
			break;
		}
		data = &descriptor[offset];
		value = read_unsigned(data, item_size);
		offset += item_size;

		if (item_type == HID_ITEM_TYPE_GLOBAL) {
			switch (item_tag) {
			case HID_GLOBAL_USAGE_PAGE:
				global.usage_page = value;
				break;
			case HID_GLOBAL_LOGICAL_MIN:
				global.logical_min = read_signed(data, item_size);
				break;
			case HID_GLOBAL_LOGICAL_MAX:
				(void)value;
				break;
			case HID_GLOBAL_REPORT_SIZE:
				global.report_size = value;
				break;
			case HID_GLOBAL_REPORT_ID:
				if (value == 0U || value > 255U) {
					result = HID_PARSER_ERR_MALFORMED;
				} else {
					global.report_id = (uint8_t)value;
				}
				break;
			case HID_GLOBAL_REPORT_COUNT:
				global.report_count = value;
				break;
			case HID_GLOBAL_PUSH:
				if (global_depth >= HID_GLOBAL_STACK_DEPTH) {
					result = HID_PARSER_ERR_GLOBAL_STACK;
				} else {
					global_stack[global_depth++] = global;
				}
				break;
			case HID_GLOBAL_POP:
				if (global_depth == 0U) {
					result = HID_PARSER_ERR_GLOBAL_STACK;
				} else {
					global = global_stack[--global_depth];
				}
				break;
			default:
				break;
			}
		} else if (item_type == HID_ITEM_TYPE_LOCAL) {
			switch (item_tag) {
			case HID_LOCAL_USAGE:
				if (local_usage_count >= HID_LOCAL_USAGE_CAPACITY) {
					result = HID_PARSER_ERR_CAPACITY;
				} else {
					local_usages[local_usage_count++] = value;
				}
				break;
			case HID_LOCAL_USAGE_MIN:
				local_usage_min = value;
				break;
			case HID_LOCAL_USAGE_MAX:
				local_usage_max = value;
				break;
			default:
				break;
			}
		} else if (item_type == HID_ITEM_TYPE_MAIN) {
			if (item_tag == HID_MAIN_COLLECTION) {
				if (++collection_depth > HID_COLLECTION_DEPTH) {
					result = HID_PARSER_ERR_COLLECTION;
				}
				local_usage_count = 0U;
				local_usage_min = 0U;
				local_usage_max = 0U;
			} else if (item_tag == HID_MAIN_END_COLLECTION) {
				if (collection_depth == 0U) {
					result = HID_PARSER_ERR_COLLECTION;
				} else {
					collection_depth--;
				}
				local_usage_count = 0U;
				local_usage_min = 0U;
				local_usage_max = 0U;
			} else if (item_tag == HID_MAIN_INPUT ||
				item_tag == HID_MAIN_OUTPUT ||
				item_tag == HID_MAIN_FEATURE) {
				hid_report_type_t type =
					(item_tag == HID_MAIN_INPUT) ? HID_REPORT_TYPE_INPUT :
					(item_tag == HID_MAIN_OUTPUT) ? HID_REPORT_TYPE_OUTPUT :
					HID_REPORT_TYPE_FEATURE;
				hid_report_cursor_t *cursor = NULL;
				hid_parser_report_t *report = NULL;
				uint64_t bit_count;
				hid_parser_field_t *field;

				if (global.report_size == 0U ||
					global.report_count == 0U ||
					global.report_size > 32U ||
					global.report_count > 1024U) {
					result = HID_PARSER_ERR_MALFORMED;
				} else if ((result = get_report_cursor(type,
					global.report_id, &cursor, &report)) == HID_PARSER_OK) {
					bit_count = (uint64_t)global.report_size *
						global.report_count;
					if (bit_count > UINT16_MAX ||
						(uint64_t)cursor->bit_count + bit_count > UINT16_MAX ||
						s_field_count >= HID_PARSER_MAX_FIELDS) {
						result = HID_PARSER_ERR_CAPACITY;
					} else {
						field = &s_fields[s_field_count++];
						memset(field, 0, sizeof(*field));
						field->type = type;
						field->report_id = global.report_id;
						field->usage_page = (uint16_t)global.usage_page;
						field->usage = (local_usage_count > 0U) ?
							(uint16_t)local_usages[0] : 0U;
						field->usage_min = (uint16_t)local_usage_min;
						field->usage_max = (uint16_t)local_usage_max;
						field->bit_offset = (uint16_t)cursor->bit_count;
						field->bit_size = (uint16_t)global.report_size;
						field->report_count = (uint16_t)global.report_count;
						field->main_flags = value;
						field->is_array = (value & 0x02U) == 0U;
						field->is_relative = (value & 0x04U) != 0U;
						field->is_constant = (value & 0x01U) != 0U;
						cursor->bit_count += (uint32_t)bit_count;
						report->payload_bits = (uint16_t)cursor->bit_count;
						report->payload_bytes = (uint16_t)(
							(report->payload_bits + 7U) / 8U);
						report->field_count++;
					}
				}
				local_usage_count = 0U;
				local_usage_min = 0U;
				local_usage_max = 0U;
			}
		}

		if (result != HID_PARSER_OK) {
			break;
		}
	}

	if (result == HID_PARSER_OK &&
		(collection_depth != 0U || global_depth != 0U)) {
		result = (collection_depth != 0U) ?
			HID_PARSER_ERR_COLLECTION : HID_PARSER_ERR_GLOBAL_STACK;
	}

	if (result != HID_PARSER_OK) {
		hid_parser_init();
	}
	return result;
}

size_t hid_parser_get_report_count(void)
{
	return s_report_count;
}

const hid_parser_report_t *hid_parser_get_report(size_t index)
{
	return (index < s_report_count) ? &s_reports[index] : NULL;
}

size_t hid_parser_get_field_count(void)
{
	return s_field_count;
}

const hid_parser_field_t *hid_parser_get_field(size_t index)
{
	return (index < s_field_count) ? &s_fields[index] : NULL;
}

const uint8_t *hid_parser_get_descriptor(size_t *length)
{
	if (length != NULL) {
		*length = s_descriptor_length;
	}
	return (s_descriptor_length > 0U) ? s_descriptor : NULL;
}
