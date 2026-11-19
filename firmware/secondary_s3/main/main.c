#include "s3_proxy_reports.h"
#include "spi_link.h"
#include "usb_device.h"

#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "secondary_s3";

void app_main(void)
{
    ESP_LOGI(TAG, "Initialize proxy-report transport");
    ESP_ERROR_CHECK(s3_proxy_reports_init());

    ESP_LOGI(TAG, "Initialize USB device");
    ESP_ERROR_CHECK(usb_device_init());

    ESP_LOGI(TAG, "Initialize P4/S3 SPI slave");
    ESP_ERROR_CHECK(spi_link_init());

    const BaseType_t result = xTaskCreate(
        spi_link_task,
        "spi_link",
        4096U,
        NULL,
        18U,
        NULL);
    if (result != pdPASS) {
        ESP_LOGE(TAG, "Cannot create SPI link task");
        abort();
    }

    ESP_LOGI(TAG, "Initialization complete; SPI link task running");
}
