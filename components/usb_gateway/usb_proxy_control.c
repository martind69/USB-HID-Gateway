#include "usb_proxy_control.h"

#include <string.h>

#include "p4_s3_link.h"
#include "p4_s3_protocol.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define FRAGMENT_DATA_SIZE 56U
#define REASSEMBLY_CAPACITY 512U

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

static const char *TAG = "usb_proxy_control";
static usb_proxy_host_set_report_fn s_set_report;
static usb_proxy_host_get_report_fn s_get_report;
static usb_proxy_host_control_fn s_control;
static SemaphoreHandle_t s_lock;
static uint8_t s_buffer[REASSEMBLY_CAPACITY];
static uint8_t s_active_tag;
static uint8_t s_expected_count;
static uint8_t s_next_index;
static size_t s_used;

void usb_proxy_control_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    s_used = 0U;
    s_expected_count = 0U;
    s_next_index = 0U;
    s_active_tag = 0U;
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

static esp_err_t send_fragments(
    uint8_t type,
    uint8_t tag,
    uint8_t interface_number,
    uint8_t report_id,
    uint8_t report_type,
    const uint8_t *data,
    size_t length)
{
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
        if (chunk > 0U && data != NULL) {
            memcpy(fragment.data, data, chunk);
            data += chunk;
            length -= chunk;
        }
        if (!p4_s3_link_enqueue(
                type,
                (const uint8_t *)&fragment,
                8U + chunk)) {
            return ESP_ERR_NO_MEM;
        }
    }
    return ESP_OK;
}

static esp_err_t collect_fragment(
    const proxy_fragment_t *fragment,
    size_t payload_length,
    bool *complete)
{
    if (fragment == NULL || complete == NULL ||
        payload_length < 8U ||
        fragment->count == 0U ||
        fragment->index >= fragment->count ||
        fragment->data_length > FRAGMENT_DATA_SIZE ||
        8U + fragment->data_length > payload_length) {
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
        return ESP_ERR_INVALID_STATE;
    }

    memcpy(
        s_buffer + s_used,
        fragment->data,
        fragment->data_length);
    s_used += fragment->data_length;
    ++s_next_index;
    *complete = s_next_index == s_expected_count;
    return ESP_OK;
}

esp_err_t usb_proxy_control_from_s3(
    uint8_t type,
    const uint8_t *payload,
    size_t length)
{
    if (payload == NULL || length < 8U || s_lock == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const proxy_fragment_t *fragment =
        (const proxy_fragment_t *)payload;

    if (type == P4_S3_MSG_FEATURE_GET) {
        if (s_get_report == NULL) {
            return ESP_ERR_NOT_SUPPORTED;
        }
        size_t response_length = sizeof(s_buffer);
        const esp_err_t error = s_get_report(
            fragment->interface_number,
            fragment->report_id,
            USB_PROXY_REPORT_FEATURE,
            s_buffer,
            &response_length);
        if (error != ESP_OK) {
            return error;
        }
        return send_fragments(
            P4_S3_MSG_CONTROL_RESPONSE_FRAGMENT,
            fragment->tag,
            fragment->interface_number,
            fragment->report_id,
            USB_PROXY_REPORT_FEATURE,
            s_buffer,
            response_length);
    }

    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000U)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    bool complete = false;
    esp_err_t error = collect_fragment(fragment, length, &complete);
    if (error == ESP_OK && complete) {
        if (type == P4_S3_MSG_OUTPUT_FRAGMENT && s_set_report != NULL) {
            error = s_set_report(
                fragment->interface_number,
                fragment->report_id,
                USB_PROXY_REPORT_OUTPUT,
                s_buffer,
                s_used);
        } else if (type == P4_S3_MSG_FEATURE_SET_FRAGMENT &&
                   s_set_report != NULL) {
            error = s_set_report(
                fragment->interface_number,
                fragment->report_id,
                USB_PROXY_REPORT_FEATURE,
                s_buffer,
                s_used);
        } else if (type == P4_S3_MSG_EP0_FRAGMENT && s_control != NULL) {
            if (s_used < 8U) {
                error = ESP_ERR_INVALID_SIZE;
            } else {
                size_t control_length = s_used - 8U;
                error = s_control(
                    s_buffer,
                    s_buffer + 8U,
                    &control_length,
                    (s_buffer[0] & 0x80U) != 0U);
            }
        } else {
            error = ESP_ERR_NOT_SUPPORTED;
        }
    }

    xSemaphoreGive(s_lock);

    if (complete) {
        ESP_LOGD(
            TAG,
            "Control request type=0x%02X tag=%u iface=%u id=0x%02X length=%u result=%s",
            type,
            fragment->tag,
            fragment->interface_number,
            fragment->report_id,
            (unsigned)s_used,
            esp_err_to_name(error));
    }
    return error;
}
