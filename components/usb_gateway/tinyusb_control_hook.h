#pragma once

#include "tusb.h"

/*
 * Project extension applied to TinyUSB 0.21.0~6.
 *
 * Return true when the application has accepted ownership of this SETUP
 * request. The callback is then invoked again for DATA and ACK stages.
 * Return false to let TinyUSB's normal standard/class/vendor dispatch run.
 *
 * A handler may complete asynchronously by retaining its own transaction
 * state and calling tud_control_xfer() or tud_control_status() after the
 * downstream USB Host transfer completes. It must not block the TinyUSB task.
 */
void tud_defer_func(void (*func)(void *), void *param);

bool tud_control_request_cb(
	uint8_t rhport,
	uint8_t stage,
	tusb_control_request_t const *request);
