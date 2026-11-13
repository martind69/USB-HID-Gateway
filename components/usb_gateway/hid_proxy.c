#include "hid_proxy.h"

#define HID_PROXY_MAX_REPORT_PAYLOAD 64U

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
	uint8_t routed_payload[HID_PROXY_MAX_REPORT_PAYLOAD];
	mapping_result_t result;

	if (input_payload == NULL || forward == NULL) {
		return false;
	}

	/*
	 * Keep the report path allocation-free. The observed G815/G502 reports
	 * fit within Full-Speed HID packet sizes. Larger reports are forwarded
	 * unchanged until a bounded transport-owned scratch buffer is provided.
	 */
	if (payload_length > sizeof(routed_payload)) {
		(void)forward(report_id, input_payload, payload_length, context);
		return false;
	}

	result = mapping_route_report(
		mode,
		input_payload,
		payload_length,
		fields,
		field_count,
		routed_payload,
		sizeof(routed_payload));

	if (result != MAPPING_OK) {
		/* Fail open: never drop an input report because mapping failed. */
		(void)forward(report_id, input_payload, payload_length, context);
		return false;
	}

	return forward(
		report_id,
		routed_payload,
		payload_length,
		context);
}
