#include "usb_proxy_control.h"

#include <string.h>

#include "p4_s3_protocol.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#define CONTROL_QUEUE_DEPTH     16U
#define CONTROL_TASK_STACK_SIZE 4096U
#define CONTROL_TASK_PRIORITY   10U
#define REASSEMBLY_CAPACITY     512U

typedef struct {
    uint8_t message_type;
    uint8_t length;
    uint8_t payload[P4_S3_PAYLOAD_SIZE];
} control_item_t;

static const char *TAG = "usb_proxy_control";

static usb_proxy_host_set_report_fn s_set_report;
static usb_proxy_host_get_report_fn s_get_report;
static usb_proxy_host_control_fn s_control;

static QueueHandle_t s_queue;
static uint8_t s_buffer[REASSEMBLY_CAPACITY];
static uint8_t s_active_tag;
static uint8_t s_expected_count;
static uint8_t s_next_index;
static size_t s_used;
static uint32_t s_dropped;

static esp_err_t collect_fragment(
    const p4_s3_fragment_t *fragment,
    size_t payload_length,
    bool *complete)
{
    *complete = false;
    if (payload_length < P4_S3_FRAGMENT_HEADER_SIZE ||
        fragment->count == 0U ||
        fragment->index >= fragment->count ||
        fragment->data_length > P4_S3_FRAGMENT_DATA_SIZE ||
        P4_S3_FRAGMENT_HEADER_SIZE + fragment->data_length > payload_length) {
        return ESP_ERR_INVALID_ARG;
    }
    if (fragment->index == 0U) {
        s_active_tag = fragment->tag;
        s_expected_count = fragment->count;
        s_next_index = 0U;
        s_used = 0U;
    }
    if (fragment->tag != s_active_tag ||
        fragment->count != s_expected_count ||
        fragment->index != s_next_index ||
        s_used + fragment->data_length > sizeof(s_buffer)) {
        s_expected_count = 0U;
        s_next_index = 0U;
        s_used = 0U;
        return ESP_ERR_INVALID_STATE;
    }
    memcpy(&s_buffer[s_used], fragment->data, fragment->data_length);
    s_used += fragment->data_length;
    ++s_next_index;
    *complete = s_next_index == s_expected_count;
    return ESP_OK;
}

static esp_err_t dispatch_complete(
    uint8_t message_type,
    const p4_s3_fragment_t *fragment)
{
    switch (message_type) {
        case P4_S3_MSG_OUTPUT_FRAGMENT:
            return s_set_report != NULL
                ? s_set_report(
                    fragment->interface_number,
                    fragment->report_id,
                    USB_PROXY_REPORT_OUTPUT,
                    s_buffer,
                    s_used)
                : ESP_ERR_INVALID_STATE;

        case P4_S3_MSG_FEATURE_SET_FRAGMENT:
            return s_set_report != NULL
                ? s_set_report(
                    fragment->interface_number,
                    fragment->report_id,
                    USB_PROXY_REPORT_FEATURE,
                    s_buffer,
                    s_used)
                : ESP_ERR_INVALID_STATE;

        case P4_S3_MSG_EP0_FRAGMENT:
            if (s_control == NULL) {
                return ESP_ERR_INVALID_STATE;
            }
            if (s_used < 8U) {
                return ESP_ERR_INVALID_SIZE;
            } else {
                size_t data_length = s_used - 8U;
                return s_control(
                    s_buffer,
                    &s_buffer[8],
                    &data_length,
                    (s_buffer[0] & 0x80U) != 0U);
            }

        default:
            return ESP_ERR_NOT_SUPPORTED;
    }
}

static void control_task(void *argument)
{
    (void)argument;
    control_item_t item;
    while (true) {
        if (xQueueReceive(s_queue, &item, portMAX_DELAY) != pdPASS) {
            continue;
        }
        const p4_s3_fragment_t *fragment =
            (const p4_s3_fragment_t *)item.payload;
        bool complete = false;
        esp_err_t error = collect_fragment(fragment, item.length, &complete);
        if (error != ESP_OK) {
            ESP_LOGW(TAG, "Fragment rejected type=0x%02X tag=%u index=%u/%u: %s",
                item.message_type, fragment->tag, fragment->index,
                fragment->count, esp_err_to_name(error));
            continue;
        }
        if (!complete) {
            continue;
        }
        error = dispatch_complete(item.message_type, fragment);
        ESP_LOGD(TAG, "Request type=0x%02X iface=%u id=0x%02X length=%u result=%s",
            item.message_type, fragment->interface_number, fragment->report_id,
            (unsigned)s_used, esp_err_to_name(error));
        s_expected_count = 0U;
        s_next_index = 0U;
        s_used = 0U;
    }
}

esp_err_t usb_proxy_control_init(void)
{
    if (s_queue != NULL) {
        return ESP_OK;
    }
    s_queue = xQueueCreate(CONTROL_QUEUE_DEPTH, sizeof(control_item_t));
    if (s_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(
            control_task,
            "usb_proxy_ctrl",
            CONTROL_TASK_STACK_SIZE,
            NULL,
            CONTROL_TASK_PRIORITY,
            NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void usb_proxy_control_register_host(
    usb_proxy_host_set_report_fn set_fn,
    usb_proxy_host_get_report_fn get_fn,
    usb_proxy_host_control_fn control_fn)
{
    s_set_report = set_fn;
    s_get_report = get_fn;
    s_control = control_fn;
}

esp_err_t usb_proxy_control_from_s3(
    uint8_t message_type,
    const uint8_t *payload,
    size_t length)
{
    if (message_type != P4_S3_MSG_OUTPUT_FRAGMENT &&
        message_type != P4_S3_MSG_FEATURE_SET_FRAGMENT &&
        message_type != P4_S3_MSG_EP0_FRAGMENT) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (payload == NULL ||
        length < P4_S3_FRAGMENT_HEADER_SIZE ||
        length > P4_S3_PAYLOAD_SIZE) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    control_item_t item = {
        .message_type = message_type,
        .length = (uint8_t)length,
    };
    memcpy(item.payload, payload, length);
    if (xQueueSend(s_queue, &item, 0U) != pdPASS) {
        ++s_dropped;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
