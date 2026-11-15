#pragma once

#include "mapping.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HID_PROXY_RAW_REPORT_MAX_SIZE 64U

typedef struct {
	uint8_t interface_id;
	uint8_t length;
	uint8_t data[HID_PROXY_RAW_REPORT_MAX_SIZE];
} hid_proxy_raw_report_t;

typedef bool (*hid_proxy_forward_callback_t)(
	uint8_t report_id,
	const uint8_t *payload,
	size_t payload_length,
	void *context);

void hid_proxy_init(void);
void hid_proxy_process(void);
bool hid_proxy_enqueue_raw_report(
	uint8_t interface_id,
	const uint8_t *report,
	size_t report_length);
bool hid_proxy_receive_raw_report(
	hid_proxy_raw_report_t *report,
	uint32_t timeout_ticks);

/*
 * Route one decoded Input report. If mapping metadata is invalid, the
 * original report is forwarded unchanged (fail-open) and the function
 * returns false to let the caller record the parser/routing error.
 */
bool hid_proxy_route_input_report(
	mapping_mode_t mode,
	uint8_t report_id,
	const uint8_t *input_payload,
	size_t payload_length,
	const mapping_report_field_t *fields,
	size_t field_count,
	hid_proxy_forward_callback_t forward,
	void *context);

#ifdef __cplusplus
}
#endif
