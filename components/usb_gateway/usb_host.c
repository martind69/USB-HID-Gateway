#include "usb_host.h"

#include "diagnostics.h"
#include "esp_log.h"

static const char *TAG = "usb_host";

void usb_host_init(void)
{
	/* Host transport is not implemented yet; do not report a false ready state. */
	diagnostics_set_host_state(DIAG_STATE_UNAVAILABLE, 0);
	ESP_LOGW(TAG, "USB Host enumeration is not implemented");
}

void usb_host_process(void)
{
	/* TODO: Forward real Host events and device lifecycle to diagnostics. */
}
