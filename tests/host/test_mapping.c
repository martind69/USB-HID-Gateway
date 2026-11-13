#include "mapping.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

static unsigned s_event_count;

static void on_mapping_event(const mapping_event_t *event, void *context)
{
	(void)context;
	assert(event != NULL);
	++s_event_count;
}

int main(void)
{
	const uint8_t input[] = { 0xDA, 0xA5, 0x7F };
	uint8_t output[sizeof(input)] = { 0 };
	mapping_config_t config = { 0 };
	mapping_report_field_t fields[2] = {
		{
			.usage_page = 0x07,
			.usage = 0x1A,
			.bit_offset = 6,
			.bit_size = 5,
			.neutral_value = 0,
			.value = 0x1A,
			.is_array = true,
			.is_relative = false
		},
		{
			.usage_page = 0x09,
			.usage = 1,
			.bit_offset = 16,
			.bit_size = 1,
			.neutral_value = 0,
			.value = 1,
			.is_array = false,
			.is_relative = false
		}
	};

	config.rule_count = 1;
	config.rules[0].usage_page = 0x07;
	config.rules[0].usage = 0x1A;
	config.rules[0].enabled = true;

	assert(mapping_set_config(&config) == MAPPING_OK);
	mapping_set_event_callback(on_mapping_event, NULL);

	assert(mapping_route_report(
		MAPPING_MODE_PASSTHROUGH,
		input,
		sizeof(input),
		fields,
		2,
		output,
		sizeof(output)) == MAPPING_OK);
	assert(memcmp(input, output, sizeof(input)) == 0);
	assert(s_event_count == 0);

	assert(mapping_route_report(
		MAPPING_MODE_PROXY,
		input,
		sizeof(input),
		fields,
		2,
		output,
		sizeof(output)) == MAPPING_OK);
	assert(output[0] == 0x1A);
	assert(output[1] == 0xA0);
	assert(output[2] == 0x7F);
	assert(s_event_count == 1);

	/* Invalid metadata must not produce a partially modified report. */
	fields[1].bit_offset = 24;
	memset(output, 0xCC, sizeof(output));
	assert(mapping_route_report(
		MAPPING_MODE_PROXY,
		input,
		sizeof(input),
		fields,
		2,
		output,
		sizeof(output)) == MAPPING_ERR_FIELD_RANGE);
	assert(output[0] == 0xCC);
	assert(output[1] == 0xCC);
	assert(output[2] == 0xCC);

	/* Empty Array slots are not consumed. */
	fields[0].usage = 0;
	fields[1].bit_offset = 16;
	assert(mapping_route_report(
		MAPPING_MODE_PROXY,
		input,
		sizeof(input),
		fields,
		1,
		output,
		sizeof(output)) == MAPPING_OK);
	assert(memcmp(input, output, sizeof(input)) == 0);

	return 0;
}
