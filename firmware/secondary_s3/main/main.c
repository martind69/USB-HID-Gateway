#include "esp_log.h"

#include "spi_link.h"
#include "usb_device.h"

static const char *TAG = "secondary_s3";

void app_main(void)
{
	esp_err_t result;

	ESP_LOGI(TAG, "ESP32-S3 secondary USB Device starting");

	result = usb_device_init();
	if (result != ESP_OK) {
		ESP_LOGE(TAG, "USB Device initialization failed: %s",
			esp_err_to_name(result));
		return;
	}

	result = spi_link_init();
	if (result != ESP_OK) {
		ESP_LOGE(TAG, "SPI link initialization failed: %s",
			esp_err_to_name(result));
		return;
	}

	ESP_LOGI(TAG, "USB Device and SPI slave framework initialized");
	spi_link_task();
}
