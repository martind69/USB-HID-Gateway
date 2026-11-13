#pragma once

#include "mapping.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef bool (*hid_proxy_forward_callback_t)(
	uint8_t report_id,
	const uint8_t *payload,
	size_t payload_length,
	void *context);

void hid_proxy_init(void);
void hid_proxy_process(void);

/*
 * Route one decoded Input report. If mapping metadata is invalid, the
 * original report is forwarded unchanged (fail-open) and the function
 * returns false to let the caller record the parser/routing error.
 *
 * The callback receives the Report ID separately and the payload without
 * the Report ID byte.
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
