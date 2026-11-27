#include "s3_proxy_reports.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#define PROXY_FRAME_QUEUE_DEPTH     16U
#define PROXY_MAX_REPORT_LENGTH     512U
#define HID_REPORT_TYPE_FEATURE     3U

static QueueHandle_t s_frames;
static uint8_t s_next_tag = 1U;
static uint32_t s_dropped;

static uint8_t next_tag(void)
{
    const uint8_t tag = s_next_tag++;
    if (s_next_tag == 0U) {
        s_next_tag = 1U;
    }
    return tag;
}

esp_err_t s3_proxy_reports_init(void)
{
    if (s_frames != NULL) {
        return ESP_OK;
    }
    s_frames = xQueueCreate(PROXY_FRAME_QUEUE_DEPTH, sizeof(p4_s3_frame_t));
    return s_frames != NULL ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t s3_proxy_send_set_report(
    uint8_t interface_number,
    uint8_t report_id,
    uint8_t report_type,
    const uint8_t *data,
    size_t length)
{
    if (s_frames == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (data == NULL || length == 0U || length > PROXY_MAX_REPORT_LENGTH) {
        return ESP_ERR_INVALID_ARG;
    }
    const uint8_t count = (uint8_t)(
        (length + P4_S3_FRAGMENT_DATA_SIZE - 1U) / P4_S3_FRAGMENT_DATA_SIZE);

    /* Queue all fragments or none, so the P4 never sees a partial report. */
    if (uxQueueSpacesAvailable(s_frames) < count) {
        ++s_dropped;
        return ESP_ERR_NO_MEM;
    }

    const uint8_t message_type = report_type == HID_REPORT_TYPE_FEATURE
        ? (uint8_t)P4_S3_MSG_FEATURE_SET_FRAGMENT
        : (uint8_t)P4_S3_MSG_OUTPUT_FRAGMENT;
    const uint8_t tag = next_tag();
    size_t remaining = length;

    for (uint8_t index = 0U; index < count; ++index) {
        const size_t chunk = remaining > P4_S3_FRAGMENT_DATA_SIZE
            ? P4_S3_FRAGMENT_DATA_SIZE
            : remaining;
        p4_s3_fragment_t fragment = {
            .tag = tag,
            .interface_number = interface_number,
            .report_id = report_id,
            .report_type = report_type,
            .index = index,
            .count = count,
            .data_length = (uint8_t)chunk,
            .reserved = 0U,
        };
        memcpy(fragment.data, data, chunk);
        data += chunk;
        remaining -= chunk;

        p4_s3_frame_t frame;
        p4_s3_frame_build(
            &frame,
            message_type,
            tag,
            (const uint8_t *)&fragment,
            (uint8_t)(P4_S3_FRAGMENT_HEADER_SIZE + chunk));
        if (xQueueSend(s_frames, &frame, 0U) != pdPASS) {
            ++s_dropped;
            return ESP_ERR_NO_MEM;
        }
    }
    return ESP_OK;
}

esp_err_t s3_proxy_send_event(uint8_t type)
{
    if (s_frames == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    p4_s3_frame_t frame;
    p4_s3_frame_build(&frame, type, 0U, NULL, 0U);
    if (xQueueSend(s_frames, &frame, 0U) != pdPASS) {
        ++s_dropped;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

bool s3_proxy_next_frame(p4_s3_frame_t *frame)
{
    return s_frames != NULL && frame != NULL &&
        xQueueReceive(s_frames, frame, 0U) == pdPASS;
}

uint32_t s3_proxy_dropped(void)
{
    return s_dropped;
}
