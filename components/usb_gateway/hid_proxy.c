#include "hid_proxy.h"

#include <stdlib.h>
#include <string.h>

void hid_proxy_init(void)
{
	/* Report transport queues are initialized by the USB backends. */
}

void hid_proxy_process(void)
{
	/* Transport scheduling remains owned by the USB gateway task. */
}

bool hid_proxy_route_input_report(
	mapping_mode_t mode,
	uint8_t report_id,
	const uint8_t *input_payload,
	size_t payload_length,
	const mapping_report_field_t *fields,
	size_t field_count,
	hid_proxy_forward_callback_t forward,
	void *context)
{
	uint8_t stack_payload[64];
	uint8_t *routed_payload = stack_payload;
	mapping_result_t result;
	bool used_heap = false;
	bool forwarded;

	if (input_payload == NULL || forward == NULL) {
		return false;
	}

	if (payload_length > sizeof(stack_payload)) {
		routed_payload = (uint8_t *)malloc(payload_length);
		if (routed_payload == NULL) {
			/* Preserve the original report if scratch allocation fails. */
			return forward(report_id, input_payload, payload_length, context);
		}
		used_heap = true;
	}

	result = mapping_route_report(
		mode,
		input_payload,
		payload_length,
		fields,
		field_count,
		routed_payload,
		payload_length);

	if (result != MAPPING_OK) {
		/* Fail open: never drop an input report because mapping failed. */
		forwarded = forward(report_id, input_payload, payload_length, context);
		if (used_heap) {
			free(routed_payload);
		}
		return false && forwarded;
	}

	forwarded = forward(
		report_id,
		routed_payload,
		payload_length,
		context);

	if (used_heap) {
		free(routed_payload);
	}

	return forwarded;
}
