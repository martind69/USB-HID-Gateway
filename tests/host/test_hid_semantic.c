#include "hid_parser.h"
#include "hid_semantic.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct {
	uint8_t report_id;
	uint8_t payload[8];
	size_t length;
	unsigned int calls;
} test_output_t;

static bool capture_report(
	uint8_t report_id,
	const uint8_t *payload,
	size_t payload_length,
	void *context)
{
	test_output_t *output = (test_output_t *)context;

	assert(payload_length <= sizeof(output->payload));
	output->report_id = report_id;
	output->length = payload_length;
	memcpy(output->payload, payload, payload_length);
	output->calls++;
	return true;
}

int main(void)
{
	static const uint8_t keyboard_descriptor[] = {
		0x05, 0x01, 0x09, 0x06, 0xA1, 0x01,
		0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7,
		0x15, 0x00, 0x25, 0x01, 0x75, 0x01,
		0x95, 0x08, 0x81, 0x02,
		0x95, 0x01, 0x75, 0x08, 0x81, 0x01,
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
	hid_parser_context_t keyboard;
	hid_parser_context_t mouse;
	test_output_t output = { 0 };
	static const uint8_t keyboard_report[] = { 0x02, 0x00, 0x04, 0, 0, 0, 0, 0 };
	static const uint8_t mouse_report[] = { 0x01, 0xFE, 0x03 };
	static const uint8_t keyboard_release[] = { 0, 0, 0, 0, 0, 0, 0, 0 };

	hid_parser_init(&keyboard);
	hid_parser_init(&mouse);
	hid_semantic_init();

	assert(hid_parser_parse_report(&keyboard,
		keyboard_descriptor, sizeof(keyboard_descriptor)) == HID_PARSER_OK);
	assert(hid_parser_parse_report(&mouse,
		mouse_descriptor, sizeof(mouse_descriptor)) == HID_PARSER_OK);

	assert(hid_semantic_process_input_report(1U, &keyboard,
		keyboard_report, sizeof(keyboard_report), capture_report,
		&output) == HID_SEMANTIC_OK);
	assert(output.report_id == 1U && output.length == 8U);
	assert(output.payload[0] == 0x02U && output.payload[2] == 0x04U);

	assert(hid_semantic_process_input_report(2U, &mouse,
		mouse_report, sizeof(mouse_report), capture_report,
		&output) == HID_SEMANTIC_OK);
	assert(output.report_id == 2U && output.length == 5U);
	assert(output.payload[0] == 0x01U);
	assert((int8_t)output.payload[1] == -2);
	assert((int8_t)output.payload[2] == 3);

	assert(hid_semantic_remove_interface(1U, capture_report, &output));
	assert(output.report_id == 1U && output.payload[0] == 0U);
	assert(hid_semantic_remove_interface(2U, capture_report, &output));
	assert(output.report_id == 2U && output.payload[0] == 0U);

	(void)keyboard_release;
	return 0;
}
