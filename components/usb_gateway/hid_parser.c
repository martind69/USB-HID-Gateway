#include "hid_parser.h"

#include <string.h>


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
	int32_t logical_max;
	uint32_t report_size;
	uint32_t report_count;
	uint8_t report_id;
} hid_global_state_t;


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
	hid_parser_context_t *context,
	hid_report_type_t type,
	uint8_t report_id,
	hid_parser_report_cursor_t **cursor,
	hid_parser_report_t **report)
{
	size_t i;

	for (i = 0U; i < context->cursor_count; ++i) {
		if (context->cursors[i].report_id == report_id &&
			context->reports[i].type == type) {
			*cursor = &context->cursors[i];
			*report = &context->reports[i];
			return HID_PARSER_OK;
		}
	}

	if (context->cursor_count >= HID_PARSER_MAX_REPORTS) {
		return HID_PARSER_ERR_CAPACITY;
	}

	i = context->cursor_count++;
	memset(&context->cursors[i], 0, sizeof(context->cursors[i]));
	memset(&context->reports[i], 0, sizeof(context->reports[i]));
	context->cursors[i].report_id = report_id;
	context->reports[i].type = type;
	context->reports[i].report_id = report_id;
	context->report_count = context->cursor_count;
	*cursor = &context->cursors[i];
	*report = &context->reports[i];
	return HID_PARSER_OK;
}

void hid_parser_init(hid_parser_context_t *context)
{
	if (context != NULL) {
		memset(context, 0, sizeof(*context));
	}
}

hid_parser_result_t hid_parser_parse_report(
	hid_parser_context_t *context,
	const uint8_t *descriptor,
	size_t length)
{
	hid_global_state_t global = { 0 };
	hid_global_state_t global_stack[HID_PARSER_GLOBAL_STACK_DEPTH];
	uint32_t local_usages[HID_PARSER_LOCAL_USAGE_CAPACITY];
	size_t local_usage_count = 0U;
	uint32_t local_usage_min = 0U;
	uint32_t local_usage_max = 0U;
	uint8_t global_depth = 0U;
	uint8_t collection_depth = 0U;
	size_t offset = 0U;
	hid_parser_result_t result = HID_PARSER_OK;

	if (context == NULL || descriptor == NULL || length == 0U) {
		return HID_PARSER_ERR_ARGUMENT;
	}
	if (length > HID_PARSER_MAX_DESCRIPTOR_SIZE) {
		return HID_PARSER_ERR_DESCRIPTOR_SIZE;
	}

	hid_parser_init(context);
	memcpy(context->descriptor, descriptor, length);
	context->descriptor_length = length;

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
				global.logical_max = (global.logical_min < 0) ?
					read_signed(data, item_size) : (int32_t)value;
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
				if (global_depth >= HID_PARSER_GLOBAL_STACK_DEPTH) {
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
				if (local_usage_count >= HID_PARSER_LOCAL_USAGE_CAPACITY) {
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
				if (collection_depth == 0U && value == 1U &&
					local_usage_count > 0U) {
					context->top_level_usage_page = (uint16_t)global.usage_page;
					context->top_level_usage = (uint16_t)local_usages[0];
				}
				if (++collection_depth > HID_PARSER_COLLECTION_DEPTH) {
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
				hid_parser_report_cursor_t *cursor = NULL;
				hid_parser_report_t *report = NULL;
				uint64_t bit_count;
				hid_parser_field_t *field;

				if ((global.report_id != 0U && context->has_unnumbered_reports) ||
					(global.report_id == 0U && context->has_report_ids)) {
					result = HID_PARSER_ERR_REPORT_ID_MIX;
				} else if (global.report_size == 0U ||
					global.report_count == 0U ||
					global.report_size > 32U ||
					global.report_count > 1024U) {
					result = HID_PARSER_ERR_MALFORMED;
				} else if ((result = get_report_cursor(context, type,
					global.report_id, &cursor, &report)) == HID_PARSER_OK) {
					if (global.report_id != 0U) {
						context->has_report_ids = true;
					} else {
						context->has_unnumbered_reports = true;
					}
					bit_count = (uint64_t)global.report_size *
						global.report_count;
					if (bit_count > UINT16_MAX ||
						(uint64_t)cursor->bit_count + bit_count > UINT16_MAX ||
						context->field_count >= HID_PARSER_MAX_FIELDS) {
						result = HID_PARSER_ERR_CAPACITY;
					} else {
						field = &context->fields[context->field_count++];
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
						field->logical_min = global.logical_min;
						field->logical_max = global.logical_max;
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
		hid_parser_init(context);
	}
	return result;
}

size_t hid_parser_get_report_count(const hid_parser_context_t *context)
{
	return (context != NULL) ? context->report_count : 0U;
}

const hid_parser_report_t *hid_parser_get_report(
	const hid_parser_context_t *context,
	size_t index)
{
	return (context != NULL && index < context->report_count) ?
		&context->reports[index] : NULL;
}

size_t hid_parser_get_field_count(const hid_parser_context_t *context)
{
	return (context != NULL) ? context->field_count : 0U;
}

const hid_parser_field_t *hid_parser_get_field(
	const hid_parser_context_t *context,
	size_t index)
{
	return (context != NULL && index < context->field_count) ?
		&context->fields[index] : NULL;
}

const uint8_t *hid_parser_get_descriptor(
	const hid_parser_context_t *context,
	size_t *length)
{
	if (length != NULL) {
		*length = (context != NULL) ? context->descriptor_length : 0U;
	}
	return (context != NULL && context->descriptor_length > 0U) ?
		context->descriptor : NULL;
}

bool hid_parser_read_field_value(
	const hid_parser_field_t *field,
	const uint8_t *payload,
	size_t payload_length,
	uint16_t element_index,
	int32_t *value)
{
	uint32_t raw_value = 0U;
	uint32_t bit_offset;
	uint16_t bit;
	uint16_t byte_index;

	if (field == NULL || payload == NULL || value == NULL ||
		element_index >= field->report_count ||
		field->bit_size == 0U || field->bit_size > 32U) {
		return false;
	}

	bit_offset = (uint32_t)field->bit_offset +
		((uint32_t)element_index * field->bit_size);
	if ((uint64_t)bit_offset + field->bit_size >
		(uint64_t)payload_length * 8U) {
		return false;
	}

	for (bit = 0U; bit < field->bit_size; ++bit) {
		byte_index = (uint16_t)((bit_offset + bit) / 8U);
		if ((payload[byte_index] &
			(uint8_t)(1U << ((bit_offset + bit) % 8U))) != 0U) {
			raw_value |= (1UL << bit);
		}
	}

	if (field->logical_min < 0 && field->bit_size < 32U &&
		(raw_value & (1UL << (field->bit_size - 1U))) != 0U) {
		raw_value |= (~0UL) << field->bit_size;
	}

	*value = (int32_t)raw_value;
	return true;
}
