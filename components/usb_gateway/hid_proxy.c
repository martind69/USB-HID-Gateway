#include "hid_proxy.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#define HID_PROXY_RAW_REPORT_QUEUE_LENGTH 16U
#define HID_PROXY_MAX_REPORT_PAYLOAD 64U

static QueueHandle_t s_raw_report_queue;

void hid_proxy_init(void)
{
	if (s_raw_report_queue == NULL) {
		s_raw_report_queue = xQueueCreate(
			HID_PROXY_RAW_REPORT_QUEUE_LENGTH,
			sizeof(hid_proxy_raw_report_t));
	}
}

void hid_proxy_process(void)
{
	/* The USB Device backend consumes queued reports after descriptor routing is configured. */
}

bool hid_proxy_enqueue_raw_report(
	uint8_t interface_id,
	const uint8_t *report,
	size_t report_length)
{
	hid_proxy_raw_report_t item;

	if (s_raw_report_queue == NULL || report == NULL ||
		report_length == 0U || report_length > sizeof(item.data)) {
		return false;
	}

	item.interface_id = interface_id;
	item.length = (uint8_t)report_length;
	memcpy(item.data, report, report_length);
	return xQueueSend(s_raw_report_queue, &item, 0U) == pdTRUE;
}

bool hid_proxy_receive_raw_report(
	hid_proxy_raw_report_t *report,
	uint32_t timeout_ticks)
{
	if (s_raw_report_queue == NULL || report == NULL) {
		return false;
	}
	return xQueueReceive(s_raw_report_queue, report, timeout_ticks) == pdTRUE;
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
		(void)forward(report_id, input_payload, payload_length, context);
		return false;
	}

	return forward(
		report_id,
		routed_payload,
		payload_length,
		context);
}
