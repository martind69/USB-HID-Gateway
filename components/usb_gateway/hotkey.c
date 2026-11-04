#include "hotkey.h"

static usb_gateway_mode_t s_mode = USB_GATEWAY_MODE_PASSTHROUGH;

void hotkey_init(void)
{
	/* TODO: Configure hotkey detection from parsed keyboard reports. */
}

void hotkey_process(void)
{
	/* TODO: Detect configured key combinations without blocking USB callbacks. */
}

void hotkey_set_mode(usb_gateway_mode_t mode)
{
	s_mode = mode;
}

usb_gateway_mode_t hotkey_get_mode(void)
{
	return s_mode;
}
