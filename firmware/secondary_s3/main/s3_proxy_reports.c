#include "s3_proxy_reports.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#define FRAGMENT_DATA_SIZE 56U
#define RESPONSE_CAPACITY 512U
#define RESPONSE_TIMEOUT_MS 1000U

typedef struct __attribute__((packed)) {
    uint8_t tag;
    uint8_t interface_number;
    uint8_t report_id;
    uint8_t report_type;
    uint8_t index;
    uint8_t count;
    uint8_t data_length;
    uint8_t reserved;
    uint8_t data[FRAGMENT_DATA_SIZE];
} proxy_fragment_t;

static QueueHandle_t s_frames;
static SemaphoreHandle_t s_response_ready;
static uint8_t s_next_tag = 1U;
static uint8_t s_response_tag;
static uint8_t s_expected_response_index;
static uint8_t s_response[RESPONSE_CAPACITY];
static size_t s_response_length;

esp_err_t s3_proxy_reports_init(void)
{
    if (s_frames != NULL) {
        return ESP_OK;
    }
    s_frames = xQueueCreate(16U, sizeof(p4_s3_frame_t));
    s_response_ready = xSemaphoreCreateBinary();
    return s_frames != NULL && s_response_ready != NULL
        ? ESP_OK
        : ESP_ERR_NO_MEM;
}

static uint8_t next_tag(void)
{
    const uint8_t tag = s_next_tag++;
    if (s_next_tag == 0U) {
        s_next_tag = 1U;
    }
    return tag;
}

static esp_err_t queue_fragments(
    uint8_t message_type,
    uint8_t interface_number,
    uint8_t report_id,
    uint8_t report_type,
    const uint8_t *data,
    size_t length,
    uint8_t *tag_out)
{
    if (s_frames == NULL || (length > 0U && data == NULL) ||
        length > RESPONSE_CAPACITY) {
        return ESP_ERR_INVALID_ARG;
    }

    const uint8_t tag = next_tag();
    uint8_t count = (uint8_t)(
        (length + FRAGMENT_DATA_SIZE - 1U) / FRAGMENT_DATA_SIZE);
    if (count == 0U) {
        count = 1U;
    }

    for (uint8_t index = 0U; index < count; ++index) {
        const size_t chunk = length > FRAGMENT_DATA_SIZE
            ? FRAGMENT_DATA_SIZE
            : length;
        proxy_fragment_t fragment = {
            .tag = tag,
            .interface_number = interface_number,
            .report_id = report_id,
            .report_type = report_type,
            .index = index,
            .count = count,
            .data_length = (uint8_t)chunk,
        };
        if (chunk > 0U) {
            memcpy(fragment.data, data, chunk);
            data += chunk;
            length -= chunk;
        }

        p4_s3_frame_t frame;
        p4_s3_frame_build(
            &frame,
            message_type,
            tag,
            (const uint8_t *)&fragment,
            (uint8_t)(8U + chunk));
        if (xQueueSend(s_frames, &frame, 0U) != pdPASS) {
            return ESP_ERR_NO_MEM;
        }
    }

    if (tag_out != NULL) {
        *tag_out = tag;
    }
    return ESP_OK;
}

esp_err_t s3_proxy_send_set_report(
    uint8_t instance,
    uint8_t report_id,
    uint8_t report_type,
    const uint8_t *data,
    size_t length)
{
    const uint8_t message_type = report_type == 3U
        ? P4_S3_MSG_FEATURE_SET_FRAGMENT
        : P4_S3_MSG_OUTPUT_FRAGMENT;
    return queue_fragments(
        message_type,
        instance,
        report_id,
        report_type,
        data,
        length,
        NULL);
}

esp_err_t s3_proxy_send_get_report(
    uint8_t instance,
    uint8_t report_id,
    uint8_t report_type,
    uint16_t requested_length)
{
    proxy_fragment_t request = {
        .tag = next_tag(),
        .interface_number = instance,
        .report_id = report_id,
        .report_type = report_type,
        .index = 0U,
        .count = 1U,
        .data_length = 2U,
        .data = {
            (uint8_t)(requested_length & 0xFFU),
            (uint8_t)(requested_length >> 8U),
        },
    };

    p4_s3_frame_t frame;
    p4_s3_frame_build(
        &frame,
        P4_S3_MSG_FEATURE_GET,
        request.tag,
        (const uint8_t *)&request,
        10U);
    return xQueueSend(s_frames, &frame, 0U) == pdPASS
        ? ESP_OK
        : ESP_ERR_NO_MEM;
}

bool s3_proxy_next_frame(p4_s3_frame_t *frame)
{
    return s_frames != NULL && frame != NULL &&
        xQueueReceive(s_frames, frame, 0U) == pdPASS;
}

esp_err_t s3_proxy_accept_response(const p4_s3_frame_t *frame)
{
    if (frame == NULL ||
        !p4_s3_frame_validate(frame) ||
        frame->raw[3] != P4_S3_MSG_CONTROL_RESPONSE_FRAGMENT ||
        frame->raw[5] < 8U) {
        return ESP_ERR_INVALID_ARG;
    }

    const proxy_fragment_t *fragment =
        (const proxy_fragment_t *)(frame->raw + 6U);
    if (fragment->index == 0U) {
        s_response_tag = fragment->tag;
        s_expected_response_index = 0U;
        s_response_length = 0U;
    }

    if (fragment->tag != s_response_tag ||
        fragment->index != s_expected_response_index ||
        s_response_length + fragment->data_length > sizeof(s_response)) {
        return ESP_ERR_INVALID_STATE;
    }

    memcpy(
        s_response + s_response_length,
        fragment->data,
        fragment->data_length);
    s_response_length += fragment->data_length;
    ++s_expected_response_index;

    if (fragment->index + 1U == fragment->count) {
        xSemaphoreGive(s_response_ready);
    }
    return ESP_OK;
}

size_t s3_proxy_copy_get_response(
    uint8_t tag,
    uint8_t *buffer,
    size_t capacity,
    uint32_t timeout_ms)
{
    if (buffer == NULL || tag != s_response_tag ||
        xSemaphoreTake(
            s_response_ready,
            pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        return 0U;
    }

    const size_t copied = s_response_length < capacity
        ? s_response_length
        : capacity;
    memcpy(buffer, s_response, copied);
    return copied;
}
