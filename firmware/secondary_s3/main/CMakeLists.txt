idf_component_register(
	SRCS
		"main.c"
		"usb_device.c"
		"spi_link.c"
	INCLUDE_DIRS "."
	REQUIRES esp_tinyusb driver esp_timer
)
