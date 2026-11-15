#include "hid_parser.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

int main(void)
{
	static const uint8_t keyboard_descriptor[] = {
		0x05, 0x01, 0x09, 0x06, 0xA1, 0x01,
		0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7,
		0x15, 0x00, 0x25, 0x01, 0x75, 0x01,
		0x95, 0x08, 0x81, 0x02,
		0x95, 0x01, 0x75, 0x08, 0x81, 0x01,
		0x95, 0x05, 0x75, 0x01, 0x05, 0x08,
		0x19, 0x01, 0x29, 0x05, 0x91, 0x02,
		0x95, 0x01, 0x75, 0x03, 0x91, 0x01,
		0x95, 0x06, 0x75, 0x08, 0x15, 0x00,
		0x25, 0x65, 0x05, 0x07, 0x19, 0x00,
		0x29, 0x65, 0x81, 0x00, 0xC0
	};
	static const uint8_t mouse_descriptor[] = {
		0x05, 0x01, 0x09, 0x02, 0xA1, 0x01,
		0x09, 0x01, 0xA1, 0x00,
		0x05, 0x09, 0x19, 0x01, 0x29, 0x03,
		0x15, 0x00, 0x25, 0x01, 0x95, 0x03,
		0x75, 0x01, 0x81, 0x02,
		0x95, 0x01, 0x75, 0x05, 0x81, 0x01,
		0x05, 0x01, 0x09, 0x30, 0x09, 0x31,
		0x15, 0x81, 0x25, 0x7F, 0x75, 0x08,
		0x95, 0x02, 0x81, 0x06, 0xC0, 0xC0
	};
	hid_parser_context_t keyboard_context;
	hid_parser_context_t mouse_context;
	const hid_parser_report_t *report;
	const hid_parser_field_t *field;
	size_t descriptor_length = 0U;

	hid_parser_init(&keyboard_context);
	hid_parser_init(&mouse_context);
	assert(hid_parser_parse_report(&keyboard_context,
		keyboard_descriptor, sizeof(keyboard_descriptor)) == HID_PARSER_OK);
	assert(hid_parser_parse_report(&mouse_context,
		mouse_descriptor, sizeof(mouse_descriptor)) == HID_PARSER_OK);

	assert(hid_parser_get_report_count(&keyboard_context) == 2U);
	assert(hid_parser_get_report_count(&mouse_context) == 1U);
	report = hid_parser_get_report(&keyboard_context, 0U);
	assert(report != NULL && report->type == HID_REPORT_TYPE_INPUT);
	assert(report->payload_bits == 64U && report->payload_bytes == 8U);
	assert(report->field_count == 3U);
	report = hid_parser_get_report(&keyboard_context, 1U);
	assert(report != NULL && report->type == HID_REPORT_TYPE_OUTPUT);
	assert(report->payload_bits == 8U && report->payload_bytes == 1U);
	report = hid_parser_get_report(&mouse_context, 0U);
	assert(report != NULL && report->payload_bits == 16U);
	assert(report->payload_bytes == 2U);

	field = hid_parser_get_field(&keyboard_context, 0U);
	assert(field != NULL && field->usage_page == 0x07U);
	assert(field->bit_offset == 0U && field->bit_size == 1U);
	assert(field->report_count == 8U && !field->is_array);
	assert(hid_parser_get_descriptor(&keyboard_context,
		&descriptor_length) != NULL);
	assert(descriptor_length == sizeof(keyboard_descriptor));

	/* A malformed descriptor clears only its own interface context. */
	assert(hid_parser_parse_report(&mouse_context,
		mouse_descriptor, sizeof(mouse_descriptor) - 1U) != HID_PARSER_OK);
	assert(hid_parser_get_report_count(&mouse_context) == 0U);
	assert(hid_parser_get_report_count(&keyboard_context) == 2U);
	return 0;
}
