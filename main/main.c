#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "usb_gateway.h"

static const char *TAG = "usb_hid_gateway";

void app_main(void)
{
	ESP_LOGI(TAG, "USB HID Gateway starting");
	usb_gateway_init();

	while (1) {
		usb_gateway_process();
		vTaskDelay(pdMS_TO_TICKS(10));
	}
}
