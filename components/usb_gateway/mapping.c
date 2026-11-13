#include "mapping.h"

#include <string.h>

static mapping_config_t s_config;
static mapping_event_callback_t s_event_callback;
static void *s_event_context;

static bool rule_matches(uint16_t usage_page, uint16_t usage)
{
	size_t i;

	for (i = 0; i < s_config.rule_count; ++i) {
		const mapping_rule_t *rule = &s_config.rules[i];

		if (rule->enabled &&
			rule->usage_page == usage_page &&
			rule->usage == usage) {
			return true;
		}
	}

	return false;
}

static bool field_is_valid(
	const mapping_report_field_t *field,
	size_t payload_length)
{
	size_t end_bit;

	if (field->bit_size == 0U || field->bit_size > 32U) {
		return false;
	}

	end_bit = (size_t)field->bit_offset + field->bit_size;

	return end_bit <= payload_length * 8U;
}

static void write_field_value(
	uint8_t *payload,
	const mapping_report_field_t *field,
	int32_t value)
{
	uint32_t encoded = (uint32_t)value;
	uint8_t bit;

	for (bit = 0U; bit < field->bit_size; ++bit) {
		const size_t absolute_bit = (size_t)field->bit_offset + bit;
		const size_t byte_index = absolute_bit / 8U;
		const uint8_t bit_index = (uint8_t)(absolute_bit % 8U);
		const uint8_t mask = (uint8_t)(1U << bit_index);

		if ((encoded & (1UL << bit)) != 0U) {
			payload[byte_index] |= mask;
		} else {
			payload[byte_index] &= (uint8_t)~mask;
		}
	}
}

mapping_result_t mapping_set_config(const mapping_config_t *config)
{
	size_t i;

	if (config == NULL) {
		return MAPPING_ERR_ARGUMENT;
	}

	if (config->rule_count > MAPPING_MAX_RULES) {
		return MAPPING_ERR_CAPACITY;
	}

	for (i = 0; i < config->rule_count; ++i) {
		if (config->rules[i].usage_page == 0U ||
			config->rules[i].usage == 0U) {
			return MAPPING_ERR_ARGUMENT;
		}
	}

	s_config = *config;
	return MAPPING_OK;
}

void mapping_set_event_callback(
	mapping_event_callback_t callback,
	void *context)
{
	s_event_callback = callback;
	s_event_context = context;
}

mapping_result_t mapping_route_report(
	mapping_mode_t mode,
	const uint8_t *input_payload,
	size_t payload_length,
	const mapping_report_field_t *fields,
	size_t field_count,
	uint8_t *output_payload,
	size_t output_capacity)
{
	size_t i;

	if (input_payload == NULL || output_payload == NULL ||
		(field_count > 0U && fields == NULL)) {
		return MAPPING_ERR_ARGUMENT;
	}

	if (payload_length > output_capacity ||
		field_count > MAPPING_MAX_FIELDS_PER_REPORT) {
		return MAPPING_ERR_CAPACITY;
	}

	/*
	 * Validate all parser metadata before modifying the output. An invalid
	 * descriptor field must never cause a partially modified report.
	 */
	for (i = 0; i < field_count; ++i) {
		if (!field_is_valid(&fields[i], payload_length)) {
			return fields[i].bit_size == 0U ||
				fields[i].bit_size > 32U ?
				MAPPING_ERR_FIELD_SIZE : MAPPING_ERR_FIELD_RANGE;
		}
	}

	memcpy(output_payload, input_payload, payload_length);

	if (mode == MAPPING_MODE_PASSTHROUGH) {
		return MAPPING_OK;
	}

	if (mode != MAPPING_MODE_PROXY) {
		return MAPPING_ERR_ARGUMENT;
	}

	for (i = 0; i < field_count; ++i) {
		const mapping_report_field_t *field = &fields[i];
		mapping_event_t event;

		/*
		 * Empty Array slots have no active usage. Variable fields are
		 * matched regardless of value so mapped key/button releases are
		 * consumed as well as presses.
		 */
		if (field->is_array && field->usage == 0U) {
			continue;
		}

		if (!rule_matches(field->usage_page, field->usage)) {
			continue;
		}

		event.usage_page = field->usage_page;
		event.usage = field->usage;
		event.value = field->value;
		event.is_array = field->is_array;
		event.is_relative = field->is_relative;

		write_field_value(output_payload, field, field->neutral_value);

		if (s_event_callback != NULL) {
			s_event_callback(&event, s_event_context);
		}
	}

	return MAPPING_OK;
}
