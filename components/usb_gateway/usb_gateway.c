#include "usb_gateway.h"

#include "esp_log.h"

static const char *TAG = "usb_gateway";
static usb_gateway_mode_t s_mode = USB_GATEWAY_MODE_PASSTHROUGH;

void usb_gateway_init(void)
{
	ESP_LOGI(TAG, "Core initialized; USB Host/Device bring-up pending");
}
void usb_gateway_process(void)
{
	/* TODO: Process queued USB HID reports without blocking callbacks. */
}
void usb_gateway_set_mode(usb_gateway_mode_t mode)
{
	s_mode = mode;
}
usb_gateway_mode_t usb_gateway_get_mode(void)
{
	return s_mode;
}
