#include "hid_proxy.h"

#include <string.h>

#include "usb_host_hid.h"
#include "hid_semantic.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#define HID_PROXY_RAW_REPORT_QUEUE_LENGTH 16U
#define HID_PROXY_MAX_REPORT_PAYLOAD 64U

static QueueHandle_t s_raw_report_queue;
static hid_proxy_forward_callback_t s_output_callback;
static void *s_output_context;
static hid_proxy_raw_report_t s_pending_report;
static bool s_pending_report_valid;

void hid_proxy_init(void)
{
	hid_semantic_init();
	if (s_raw_report_queue == NULL) {
		s_raw_report_queue = xQueueCreate(
			HID_PROXY_RAW_REPORT_QUEUE_LENGTH,
			sizeof(hid_proxy_raw_report_t));
	}
}

void hid_proxy_set_output_callback(
	hid_proxy_forward_callback_t callback,
	void *context)
{
	s_output_callback = callback;
	s_output_context = context;
}

static bool hid_proxy_emit_standard_report(
	uint8_t report_id,
	const uint8_t *payload,
	size_t payload_length,
	void *context)
{
	(void)context;
	if (s_output_callback == NULL) {
		return false;
	}
	return s_output_callback(report_id, payload, payload_length, s_output_context);
}

bool hid_proxy_enqueue_interface_removed(uint8_t interface_id)
{
	hid_proxy_raw_report_t item = { 0 };

	if (s_raw_report_queue == NULL || interface_id == 0U) {
		return false;
	}
	item.interface_id = interface_id;
	item.length = 0U;
	return xQueueSend(s_raw_report_queue, &item, 0U) == pdTRUE;
}

void hid_proxy_process(void)
{
	unsigned int processed = 0U;

	while (processed < 4U) {
		usb_host_hid_interface_info_t interface_info;
		hid_semantic_result_t route_result;

		if (!s_pending_report_valid) {
			if (!hid_proxy_receive_raw_report(&s_pending_report, 0U)) {
				break;
			}
			s_pending_report_valid = true;
		}

		if (s_pending_report.length == 0U) {
			if (!hid_semantic_remove_interface(s_pending_report.interface_id,
				hid_proxy_emit_standard_report, NULL)) {
				break;
			}
			s_pending_report_valid = false;
			processed++;
			continue;
		}

		if (!usb_host_hid_get_interface_info(
			s_pending_report.interface_id, &interface_info)) {
			s_pending_report_valid = false;
			processed++;
			continue;
		}

		route_result = hid_semantic_process_input_report(
			s_pending_report.interface_id,
			interface_info.parser_context,
			s_pending_report.data,
			s_pending_report.length,
			hid_proxy_emit_standard_report,
			NULL);
		if (route_result == HID_SEMANTIC_OUTPUT_BUSY) {
			break;
		}
		if (route_result != HID_SEMANTIC_OK) {
			/* Unknown report formats are not translated in this implementation stage. */
			s_pending_report_valid = false;
			processed++;
			continue;
		}
		s_pending_report_valid = false;
		processed++;
	}
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
