idf_component_register(
	SRCS
		"usb_gateway.c"
		"usb_host.c"
		"usb_device.c"
		"hid_parser.c"
		"hid_proxy.c"
		"hotkey.c"
		"mapping.c"
		"diagnostics.c"
		"dac_output.c"
		"gpio_output.c"
	INCLUDE_DIRS "." "include"
	REQUIRES log freertos esp_timer heap esp_tinyusb
)
