#include <stdlib.h>

#include "p4_s3_protocol.h"
#include "s3_mirror_session.h"
#include "s3_ota_receiver.h"
#include "s3_proxy_reports.h"
#include "spi_link.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define SPI_LINK_TASK_STACK     6144U
#define SPI_LINK_TASK_PRIORITY  18U

static const char *TAG = "secondary_s3";

void app_main(void)
{
    ESP_LOGI(TAG, "Initialize SPI OTA receiver");
    ESP_ERROR_CHECK(s3_ota_receiver_init());

    ESP_LOGI(TAG, "Initialize S3 -> P4 uplink");
    ESP_ERROR_CHECK(s3_proxy_reports_init());

    ESP_LOGI(TAG, "Initialize USB mirror (PC port disconnected until session)");
    ESP_ERROR_CHECK(s3_mirror_session_init());

    ESP_LOGI(TAG, "Initialize P4/S3 SPI slave");
    ESP_ERROR_CHECK(spi_link_init());

    /* Delivered with the first P4 poll: P4 re-sends the current session. */
    ESP_ERROR_CHECK(s3_proxy_send_event(P4_S3_MSG_MIRROR_REQUEST));

    if (xTaskCreate(
            spi_link_task,
            "spi_link",
            SPI_LINK_TASK_STACK,
            NULL,
            SPI_LINK_TASK_PRIORITY,
            NULL) != pdPASS) {
        ESP_LOGE(TAG, "Cannot create SPI link task");
        abort();
    }
    ESP_LOGI(TAG, "Initialization complete");
}
