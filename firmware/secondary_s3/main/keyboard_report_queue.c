#include "keyboard_report_queue.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "sdkconfig.h"

static QueueHandle_t s_queue;
static uint32_t s_dropped;
static uint32_t s_high_watermark;

esp_err_t keyboard_report_queue_init(void)
{
    if (s_queue != NULL) {
        return ESP_OK;
    }

    s_queue = xQueueCreate(
        CONFIG_SECONDARY_S3_KEYBOARD_QUEUE_DEPTH,
        sizeof(s3_hid_report_t));
    return s_queue != NULL ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t keyboard_report_queue_enqueue(
    uint8_t interface_number,
    const uint8_t *data,
    size_t length)
{
    if (s_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (data == NULL || length == 0U || length > S3_HID_REPORT_MAX_SIZE) {
        return ESP_ERR_INVALID_ARG;
    }

    s3_hid_report_t report = {
        .interface_number = interface_number,
        .length = (uint8_t)length,
    };
    memcpy(report.data, data, length);

    if (xQueueSend(s_queue, &report, 0U) != pdPASS) {
        ++s_dropped;
        return ESP_ERR_NO_MEM;
    }

    const UBaseType_t waiting = uxQueueMessagesWaiting(s_queue);
    if ((uint32_t)waiting > s_high_watermark) {
        s_high_watermark = (uint32_t)waiting;
    }
    return ESP_OK;
}

bool keyboard_report_queue_receive(
    s3_hid_report_t *report,
    uint32_t timeout_ms)
{
    return s_queue != NULL && report != NULL &&
        xQueueReceive(
            s_queue,
            report,
            pdMS_TO_TICKS(timeout_ms)) == pdPASS;
}

void keyboard_report_queue_clear(void)
{
    if (s_queue != NULL) {
        xQueueReset(s_queue);
    }
}

uint32_t keyboard_report_queue_dropped(void)
{
    return s_dropped;
}

uint32_t keyboard_report_queue_high_watermark(void)
{
    return s_high_watermark;
}
