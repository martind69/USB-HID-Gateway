#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "hid_parser.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef bool (*hid_semantic_emit_callback_t)(
	uint8_t report_id,
	const uint8_t *payload,
	size_t payload_length,
	void *context);

void hid_semantic_init(void);
bool hid_semantic_process_input_report(
	uint8_t interface_id,
	const hid_parser_context_t *parser,
	const uint8_t *raw_report,
	size_t raw_report_length,
	hid_semantic_emit_callback_t emit,
	void *emit_context);
void hid_semantic_remove_interface(
	uint8_t interface_id,
	hid_semantic_emit_callback_t emit,
	void *emit_context);

#ifdef __cplusplus
}
#endif
