#include "hid_semantic.h"

#include <string.h>

#define HID_SEMANTIC_INTERFACE_COUNT 256U
#define HID_SEMANTIC_KEY_COUNT 6U
#define HID_SEMANTIC_KEYBOARD_REPORT_ID 1U
#define HID_SEMANTIC_MOUSE_REPORT_ID 2U

typedef struct {
	bool active;
	bool keyboard;
	bool mouse;
	bool key_rollover;
	uint8_t modifiers;
	uint8_t keys[HID_SEMANTIC_KEY_COUNT];
	uint8_t buttons;
} hid_semantic_interface_state_t;

static hid_semantic_interface_state_t s_interfaces[HID_SEMANTIC_INTERFACE_COUNT];

static int32_t clamp_int8(int32_t value)
{
	if (value > INT8_MAX) {
		return INT8_MAX;
	}
	if (value < INT8_MIN) {
		return INT8_MIN;
	}
	return value;
}

static void add_key(uint8_t *keys, uint8_t key)
{
	size_t i;
	size_t free_index = HID_SEMANTIC_KEY_COUNT;

	if (key == 0U) {
		return;
	}
	for (i = 0U; i < HID_SEMANTIC_KEY_COUNT; ++i) {
		if (keys[i] == key) {
			return;
		}
		if (keys[i] == 0U && free_index == HID_SEMANTIC_KEY_COUNT) {
			free_index = i;
		}
	}
	if (free_index < HID_SEMANTIC_KEY_COUNT) {
		keys[free_index] = key;
	}
}

static void build_keyboard_report(uint8_t report[8])
{
	uint8_t merged_keys[HID_SEMANTIC_KEY_COUNT] = { 0 };
	uint8_t modifiers = 0U;
	bool rollover = false;
	size_t i;
	size_t j;

	for (i = 0U; i < HID_SEMANTIC_INTERFACE_COUNT; ++i) {
		const hid_semantic_interface_state_t *state = &s_interfaces[i];

		if (!state->active || !state->keyboard) {
			continue;
		}
		modifiers |= state->modifiers;
		rollover = rollover || state->key_rollover;
		for (j = 0U; j < HID_SEMANTIC_KEY_COUNT; ++j) {
			add_key(merged_keys, state->keys[j]);
		}
	}

	report[0] = modifiers;
	report[1] = 0U;
	if (rollover) {
		memset(&report[2], 0x01, HID_SEMANTIC_KEY_COUNT);
	} else {
		memcpy(&report[2], merged_keys, sizeof(merged_keys));
	}
}

static uint8_t build_mouse_buttons(void)
{
	uint8_t buttons = 0U;
	size_t i;

	for (i = 0U; i < HID_SEMANTIC_INTERFACE_COUNT; ++i) {
		if (s_interfaces[i].active && s_interfaces[i].mouse) {
			buttons |= s_interfaces[i].buttons;
		}
	}
	return buttons;
}

static bool get_report_payload(
	const hid_parser_context_t *parser,
	const uint8_t *raw_report,
	size_t raw_report_length,
	uint8_t *report_id,
	const uint8_t **payload,
	size_t *payload_length)
{
	size_t offset = parser->has_report_ids ? 1U : 0U;
	size_t i;

	if (raw_report_length < offset) {
		return false;
	}

	*report_id = parser->has_report_ids ? raw_report[0] : 0U;
	*payload = raw_report + offset;
	*payload_length = raw_report_length - offset;

	for (i = 0U; i < parser->report_count; ++i) {
		const hid_parser_report_t *report = &parser->reports[i];

		if (report->type == HID_REPORT_TYPE_INPUT &&
			report->report_id == *report_id) {
			return *payload_length >= report->payload_bytes;
		}
	}
	return false;
}

static uint16_t field_usage(const hid_parser_field_t *field, uint16_t index)
{
	if (field->usage_min != 0U || field->usage_max != 0U) {
		return (uint16_t)(field->usage_min + index);
	}
	return (uint16_t)(field->usage + index);
}

static void process_keyboard_fields(
	hid_semantic_interface_state_t *state,
	const hid_parser_context_t *parser,
	uint8_t report_id,
	const uint8_t *payload,
	size_t payload_length)
{
	bool modifier_field_seen = false;
	bool key_field_seen = false;
	bool rollover = false;
	uint8_t modifiers = state->modifiers;
	uint8_t keys[HID_SEMANTIC_KEY_COUNT];
	size_t i;

	memcpy(keys, state->keys, sizeof(keys));

	for (i = 0U; i < parser->field_count; ++i) {
		const hid_parser_field_t *field = &parser->fields[i];
		uint16_t element;

		if (field->type != HID_REPORT_TYPE_INPUT ||
			field->report_id != report_id ||
			field->usage_page != 0x07U || field->is_constant) {
			continue;
		}

		if (field->is_array) {
			key_field_seen = true;
			memset(keys, 0, sizeof(keys));
			for (element = 0U; element < field->report_count; ++element) {
				int32_t value;

				if (!hid_parser_read_field_value(field, payload,
					payload_length, element, &value)) {
					continue;
				}
				if (value == 1) {
					rollover = true;
				} else if (value > 1 && value <= UINT8_MAX) {
					add_key(keys, (uint8_t)value);
				}
			}
		} else {
			for (element = 0U; element < field->report_count; ++element) {
				uint16_t usage = field_usage(field, element);
				int32_t value;

				if (!hid_parser_read_field_value(field, payload,
					payload_length, element, &value)) {
					continue;
				}
				if (usage >= 0xE0U && usage <= 0xE7U) {
					modifier_field_seen = true;
					if (value != 0) {
						modifiers |= (uint8_t)(1U << (usage - 0xE0U));
					} else {
						modifiers &= (uint8_t)~(1U << (usage - 0xE0U));
					}
				} else {
					if (!key_field_seen) {
						memset(keys, 0, sizeof(keys));
						key_field_seen = true;
					}
					if (value != 0 && usage > 0U && usage <= UINT8_MAX) {
						add_key(keys, (uint8_t)usage);
					}
				}
			}
		}
	}

	if (modifier_field_seen) {
		state->modifiers = modifiers;
	}
	if (key_field_seen) {
		memcpy(state->keys, keys, sizeof(keys));
		state->key_rollover = rollover;
	}
}

static void process_mouse_fields(
	hid_semantic_interface_state_t *state,
	const hid_parser_context_t *parser,
	uint8_t report_id,
	const uint8_t *payload,
	size_t payload_length,
	int32_t *delta_x,
	int32_t *delta_y,
	int32_t *wheel,
	int32_t *pan)
{
	bool buttons_seen = false;
	size_t i;

	*delta_x = 0;
	*delta_y = 0;
	*wheel = 0;
	*pan = 0;

	for (i = 0U; i < parser->field_count; ++i) {
		const hid_parser_field_t *field = &parser->fields[i];
		uint16_t element;

		if (field->type != HID_REPORT_TYPE_INPUT ||
			field->report_id != report_id || field->is_constant) {
			continue;
		}

		if (field->usage_page == 0x09U) {
			buttons_seen = true;
			state->buttons = 0U;
			for (element = 0U; element < field->report_count; ++element) {
				uint16_t usage = field_usage(field, element);
				int32_t value;

				if (usage >= 1U && usage <= 8U &&
					hid_parser_read_field_value(field, payload,
						payload_length, element, &value) && value != 0) {
					state->buttons |= (uint8_t)(1U << (usage - 1U));
				}
			}
		} else if (field->usage_page == 0x01U && field->is_relative) {
			for (element = 0U; element < field->report_count; ++element) {
				uint16_t usage = field_usage(field, element);
				int32_t value;

				if (!hid_parser_read_field_value(field, payload,
					payload_length, element, &value)) {
					continue;
				}
				switch (usage) {
				case 0x30U:
					*delta_x = value;
					break;
				case 0x31U:
					*delta_y = value;
					break;
				case 0x38U:
					*wheel = value;
					break;
				default:
					break;
				}
			}
		} else if (field->usage_page == 0x0CU) {
			for (element = 0U; element < field->report_count; ++element) {
				uint16_t usage = field_usage(field, element);
				int32_t value;

				if (usage == 0x0238U &&
					hid_parser_read_field_value(field, payload,
						payload_length, element, &value)) {
					*pan = value;
				}
			}
		}
	}

	(void)buttons_seen;
}

void hid_semantic_init(void)
{
	memset(s_interfaces, 0, sizeof(s_interfaces));
}

bool hid_semantic_process_input_report(
	uint8_t interface_id,
	const hid_parser_context_t *parser,
	const uint8_t *raw_report,
	size_t raw_report_length,
	hid_semantic_emit_callback_t emit,
	void *emit_context)
{
	hid_semantic_interface_state_t *state;
	uint8_t report_id;
	const uint8_t *payload;
	size_t payload_length;

	if (interface_id == 0U || parser == NULL || raw_report == NULL ||
		emit == NULL ||
		!get_report_payload(parser, raw_report, raw_report_length,
			&report_id, &payload, &payload_length)) {
		return false;
	}

	state = &s_interfaces[interface_id];
	state->active = true;

	if (parser->top_level_usage_page == 0x01U &&
		parser->top_level_usage == 0x06U) {
		uint8_t keyboard_report[8];

		state->keyboard = true;
		process_keyboard_fields(state, parser, report_id,
			payload, payload_length);
		build_keyboard_report(keyboard_report);
		return emit(HID_SEMANTIC_KEYBOARD_REPORT_ID,
			keyboard_report, sizeof(keyboard_report), emit_context);
	}

	if (parser->top_level_usage_page == 0x01U &&
		parser->top_level_usage == 0x02U) {
		uint8_t mouse_report[5];
		int32_t delta_x;
		int32_t delta_y;
		int32_t wheel;
		int32_t pan;

		state->mouse = true;
		process_mouse_fields(state, parser, report_id, payload,
			payload_length, &delta_x, &delta_y, &wheel, &pan);
		mouse_report[0] = build_mouse_buttons();
		mouse_report[1] = (uint8_t)(int8_t)clamp_int8(delta_x);
		mouse_report[2] = (uint8_t)(int8_t)clamp_int8(delta_y);
		mouse_report[3] = (uint8_t)(int8_t)clamp_int8(wheel);
		mouse_report[4] = (uint8_t)(int8_t)clamp_int8(pan);
		return emit(HID_SEMANTIC_MOUSE_REPORT_ID,
			mouse_report, sizeof(mouse_report), emit_context);
	}

	return false;
}

void hid_semantic_remove_interface(
	uint8_t interface_id,
	hid_semantic_emit_callback_t emit,
	void *emit_context)
{
	hid_semantic_interface_state_t *state;

	if (interface_id == 0U) {
		return;
	}
	state = &s_interfaces[interface_id];
	if (!state->active) {
		return;
	}

	{
		bool was_keyboard = state->keyboard;
		bool was_mouse = state->mouse;
		uint8_t keyboard_report[8] = { 0 };
		uint8_t mouse_report[5] = { 0 };

		memset(state, 0, sizeof(*state));
		if (emit != NULL && was_keyboard) {
			build_keyboard_report(keyboard_report);
			(void)emit(HID_SEMANTIC_KEYBOARD_REPORT_ID,
				keyboard_report, sizeof(keyboard_report), emit_context);
		}
		if (emit != NULL && was_mouse) {
			mouse_report[0] = build_mouse_buttons();
			(void)emit(HID_SEMANTIC_MOUSE_REPORT_ID,
				mouse_report, sizeof(mouse_report), emit_context);
		}
	}
}
