#include "usb_gateway.h"

#include "esp_log.h"
#include "dac_output.h"
#include "gpio_output.h"
#include "hid_parser.h"
#include "hid_proxy.h"
#include "hotkey.h"
#include "mapping.h"
#include "usb_device.h"
#include "usb_host.h"

static const char *TAG = "usb_gateway";

void usb_gateway_init(void)
{
	ESP_LOGI(TAG, "Initializing USB HID Gateway modules");

	dac_output_init();
	gpio_output_init();
	hid_parser_init();
	hotkey_init();
	mapping_init();
	hid_proxy_init();
	usb_device_init();
	usb_host_init();
}

void usb_gateway_process(void)
{
	usb_host_process();
	usb_device_process();
	hotkey_process();
	hid_proxy_process();
	mapping_process();
}

void usb_gateway_set_mode(usb_gateway_mode_t mode)
{
	hotkey_set_mode(mode);
}

usb_gateway_mode_t usb_gateway_get_mode(void)
{
	return hotkey_get_mode();
}
