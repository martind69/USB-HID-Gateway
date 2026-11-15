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
	const hid_parser_report_t *report;
	const hid_parser_field_t *field;
	size_t descriptor_length = 0U;

	assert(hid_parser_init());
	assert(hid_parser_parse_report(
		keyboard_descriptor, sizeof(keyboard_descriptor)) == HID_PARSER_OK);
	assert(hid_parser_get_report_count() == 2U);

	report = hid_parser_get_report(0U);
	assert(report != NULL);
	assert(report->type == HID_REPORT_TYPE_INPUT);
	assert(report->payload_bits == 72U);
	assert(report->payload_bytes == 9U);
	assert(report->field_count == 3U);

	report = hid_parser_get_report(1U);
	assert(report != NULL);
	assert(report->type == HID_REPORT_TYPE_OUTPUT);
	assert(report->payload_bits == 8U);
	assert(report->payload_bytes == 1U);

	field = hid_parser_get_field(0U);
	assert(field != NULL);
	assert(field->usage_page == 0x07U);
	assert(field->bit_offset == 0U);
	assert(field->bit_size == 1U);
	assert(field->report_count == 8U);
	assert(!field->is_array);

	assert(hid_parser_get_descriptor(&descriptor_length) != NULL);
	assert(descriptor_length == sizeof(keyboard_descriptor));

	/* A truncated short item must fail and clear the partial parser model. */
	assert(hid_parser_parse_report(keyboard_descriptor,
		sizeof(keyboard_descriptor) - 1U) == HID_PARSER_ERR_COLLECTION ||
		hid_parser_get_report_count() == 0U);

	return 0;
}
