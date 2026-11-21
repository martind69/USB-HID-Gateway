#include "usb_host_control_bridge.h"

#include <string.h>

#include "usb_proxy_control.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define G815_USB_VID 0x046DU
#define G815_USB_PID 0xC33FU
#define CONTROL_TIMEOUT_MS 1000U
#define CONTROL_BUFFER_CAPACITY 520U

static const char *TAG = "g815_control";
static usb_host_client_handle_t s_client;
static usb_device_handle_t s_device;
static uint8_t s_address;
static SemaphoreHandle_t s_mutex;
static SemaphoreHandle_t s_complete;
static usb_transfer_t *s_transfer;
static esp_err_t s_result;
static size_t s_actual_length;
static uint32_t s_set_ok;
static uint32_t s_set_failed;

static void transfer_callback(usb_transfer_t *transfer)
{
    s_result = transfer->status == USB_TRANSFER_STATUS_COMPLETED
        ? ESP_OK
        : ESP_FAIL;
    s_actual_length = transfer->actual_num_bytes;
    xSemaphoreGive(s_complete);
}

static esp_err_t execute_control(
    const uint8_t setup[8],
    const uint8_t *out_data,
    uint8_t *in_data,
    size_t *data_length,
    bool direction_in)
{
    if (setup == NULL || data_length == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_device == NULL || s_transfer == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (*data_length > (CONTROL_BUFFER_CAPACITY - 8U)) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(CONTROL_TIMEOUT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    while (xSemaphoreTake(s_complete, 0U) == pdTRUE) {
    }

    memcpy(s_transfer->data_buffer, setup, 8U);
    if (!direction_in && *data_length > 0U && out_data != NULL) {
        memcpy(s_transfer->data_buffer + 8U, out_data, *data_length);
    }

    s_transfer->device_handle = s_device;
    s_transfer->bEndpointAddress = 0U;
    s_transfer->num_bytes = 8U + *data_length;
    s_transfer->callback = transfer_callback;
    s_transfer->context = NULL;
    s_result = ESP_FAIL;
    s_actual_length = 0U;

    esp_err_t error = usb_host_transfer_submit_control(s_client, s_transfer);
    if (error == ESP_OK &&
        xSemaphoreTake(
            s_complete,
            pdMS_TO_TICKS(CONTROL_TIMEOUT_MS)) != pdTRUE) {
        error = ESP_ERR_TIMEOUT;
    } else if (error == ESP_OK) {
        error = s_result;
    }

    if (error == ESP_OK && direction_in) {
        const size_t received = s_actual_length > 8U
            ? s_actual_length - 8U
            : 0U;
        const size_t copied = received < *data_length ? received : *data_length;
        if (copied > 0U && in_data != NULL) {
            memcpy(in_data, s_transfer->data_buffer + 8U, copied);
        }
        *data_length = copied;
    }

    xSemaphoreGive(s_mutex);
    return error;
}

static esp_err_t set_report(
    uint8_t interface_number,
    uint8_t report_id,
    usb_proxy_report_type_t report_type,
    const uint8_t *data,
    size_t length)
{
    if (data == NULL || length == 0U || length > 512U) {
        return ESP_ERR_INVALID_ARG;
    }

    const uint16_t value =
        (uint16_t)(((uint16_t)report_type << 8U) | report_id);
    const uint8_t setup[8] = {
        0x21U,
        0x09U,
        (uint8_t)(value & 0xFFU),
        (uint8_t)(value >> 8U),
        interface_number,
        0U,
        (uint8_t)(length & 0xFFU),
        (uint8_t)(length >> 8U),
    };

    size_t transfer_length = length;
    const esp_err_t error = execute_control(
        setup,
        data,
        NULL,
        &transfer_length,
        false);

    if (error == ESP_OK) {
        ++s_set_ok;
    } else {
        ++s_set_failed;
    }

    ESP_LOGI(
        TAG,
        "SET_REPORT iface=%u type=%u id=0x%02X length=%u result=%s ok=%lu failed=%lu",
        interface_number,
        (unsigned)report_type,
        report_id,
        (unsigned)length,
        esp_err_to_name(error),
        (unsigned long)s_set_ok,
        (unsigned long)s_set_failed);
    return error;
}

static esp_err_t get_report(
    uint8_t interface_number,
    uint8_t report_id,
    usb_proxy_report_type_t report_type,
    uint8_t *data,
    size_t *length)
{
    if (data == NULL || length == NULL || *length == 0U || *length > 512U) {
        return ESP_ERR_INVALID_ARG;
    }

    const uint16_t value =
        (uint16_t)(((uint16_t)report_type << 8U) | report_id);
    const uint8_t setup[8] = {
        0xA1U,
        0x01U,
        (uint8_t)(value & 0xFFU),
        (uint8_t)(value >> 8U),
        interface_number,
        0U,
        (uint8_t)(*length & 0xFFU),
        (uint8_t)(*length >> 8U),
    };

    return execute_control(setup, NULL, data, length, true);
}

static esp_err_t raw_control(
    const uint8_t setup[8],
    uint8_t *data,
    size_t *length,
    bool direction_in)
{
    return execute_control(
        setup,
        direction_in ? NULL : data,
        direction_in ? data : NULL,
        length,
        direction_in);
}

esp_err_t usb_host_control_bridge_init(usb_host_client_handle_t client_handle)
{
    if (client_handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    s_client = client_handle;
    s_mutex = xSemaphoreCreateMutex();
    s_complete = xSemaphoreCreateBinary();
    if (s_mutex == NULL || s_complete == NULL) {
        return ESP_ERR_NO_MEM;
    }

    const esp_err_t error = usb_host_transfer_alloc(
        CONTROL_BUFFER_CAPACITY,
        0U,
        &s_transfer);
    if (error != ESP_OK) {
        return error;
    }

    usb_proxy_control_register_host(set_report, get_report, raw_control);
    ESP_LOGI(TAG, "Transparent HID++ control bridge initialized");
    return ESP_OK;
}

void usb_host_control_bridge_attach(
    usb_device_handle_t device_handle,
    uint8_t device_address,
    uint16_t vendor_id,
    uint16_t product_id)
{
    if (vendor_id != G815_USB_VID || product_id != G815_USB_PID) {
        return;
    }

    s_device = device_handle;
    s_address = device_address;
    ESP_LOGI(TAG, "G815 control bridge attached: address=%u", s_address);
}

void usb_host_control_bridge_detach(uint8_t device_address)
{
    if (device_address == s_address) {
        s_device = NULL;
        s_address = 0U;
        ESP_LOGI(TAG, "G815 control bridge detached");
    }
}
