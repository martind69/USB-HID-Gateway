#include "usb_host_control_bridge.h"

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define CONTROL_TIMEOUT_MS          1000U
#define CONTROL_BUFFER_CAPACITY     520U
#define CONTROL_SETUP_SIZE          8U
#define BRIDGE_TARGET_COUNT         ((size_t)GATEWAY_PORT_TARGET_TAP + 1U)

typedef struct {
    usb_device_handle_t device;
    uint8_t address;
} bridge_slot_t;

static const char *TAG = "ctrl_bridge";

static usb_host_client_handle_t s_client;
static volatile bridge_slot_t s_slots[BRIDGE_TARGET_COUNT];
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

static usb_device_handle_t device_for(gateway_port_target_t target)
{
    if ((size_t)target >= BRIDGE_TARGET_COUNT) {
        return NULL;
    }
    return s_slots[target].device;
}

static esp_err_t execute_control(
    gateway_port_target_t target,
    const uint8_t setup[CONTROL_SETUP_SIZE],
    const uint8_t *out_data,
    uint8_t *in_data,
    size_t *data_length,
    bool direction_in)
{
    if (setup == NULL || data_length == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_transfer == NULL || s_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (*data_length > (CONTROL_BUFFER_CAPACITY - CONTROL_SETUP_SIZE)) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(CONTROL_TIMEOUT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    usb_device_handle_t device = device_for(target);
    if (device == NULL) {
        xSemaphoreGive(s_mutex);
        return ESP_ERR_INVALID_STATE;
    }
    while (xSemaphoreTake(s_complete, 0U) == pdTRUE) {
    }
    memcpy(s_transfer->data_buffer, setup, CONTROL_SETUP_SIZE);
    if (!direction_in && *data_length > 0U && out_data != NULL) {
        memcpy(s_transfer->data_buffer + CONTROL_SETUP_SIZE, out_data, *data_length);
    }
    s_transfer->device_handle = device;
    s_transfer->bEndpointAddress = 0U;
    s_transfer->num_bytes = (int)(CONTROL_SETUP_SIZE + *data_length);
    s_transfer->callback = transfer_callback;
    s_transfer->context = NULL;
    s_result = ESP_FAIL;
    s_actual_length = 0U;

    esp_err_t error = usb_host_transfer_submit_control(s_client, s_transfer);
    if (error == ESP_OK &&
        xSemaphoreTake(s_complete, pdMS_TO_TICKS(CONTROL_TIMEOUT_MS)) != pdTRUE) {
        error = ESP_ERR_TIMEOUT;
    } else if (error == ESP_OK) {
        error = s_result;
    }
    if (error == ESP_OK && direction_in) {
        const size_t received = s_actual_length > CONTROL_SETUP_SIZE
            ? s_actual_length - CONTROL_SETUP_SIZE
            : 0U;
        const size_t copied = received < *data_length ? received : *data_length;
        if (copied > 0U && in_data != NULL) {
            memcpy(in_data, s_transfer->data_buffer + CONTROL_SETUP_SIZE, copied);
        }
        *data_length = copied;
    }
    xSemaphoreGive(s_mutex);
    return error;
}

esp_err_t usb_host_control_bridge_set_report(
    gateway_port_target_t target,
    uint8_t interface_number,
    uint8_t report_id,
    usb_proxy_report_type_t report_type,
    const uint8_t *data,
    size_t length)
{
    if (data == NULL || length == 0U || length > 512U) {
        return ESP_ERR_INVALID_ARG;
    }
    const uint16_t value = (uint16_t)(((uint16_t)report_type << 8U) | report_id);
    const uint8_t setup[CONTROL_SETUP_SIZE] = {
        0x21U,                              /* class, interface, host-to-device */
        0x09U,                              /* SET_REPORT */
        (uint8_t)(value & 0xFFU),
        (uint8_t)(value >> 8U),
        interface_number,
        0U,
        (uint8_t)(length & 0xFFU),
        (uint8_t)(length >> 8U),
    };
    size_t transfer_length = length;
    const esp_err_t error = execute_control(
        target, setup, data, NULL, &transfer_length, false);
    if (error == ESP_OK) {
        ++s_set_ok;
    } else {
        ++s_set_failed;
    }
    ESP_LOGD(TAG, "SET_REPORT %s iface=%u type=%u id=0x%02X len=%u: %s (ok=%lu failed=%lu)",
        gateway_port_target_name(target), interface_number, (unsigned)report_type,
        report_id, (unsigned)length, esp_err_to_name(error),
        (unsigned long)s_set_ok, (unsigned long)s_set_failed);
    return error;
}

esp_err_t usb_host_control_bridge_get_report(
    gateway_port_target_t target,
    uint8_t interface_number,
    uint8_t report_id,
    usb_proxy_report_type_t report_type,
    uint8_t *data,
    size_t *length)
{
    if (data == NULL || length == NULL || *length == 0U || *length > 512U) {
        return ESP_ERR_INVALID_ARG;
    }
    const uint16_t value = (uint16_t)(((uint16_t)report_type << 8U) | report_id);
    const uint8_t setup[CONTROL_SETUP_SIZE] = {
        0xA1U,                              /* class, interface, device-to-host */
        0x01U,                              /* GET_REPORT */
        (uint8_t)(value & 0xFFU),
        (uint8_t)(value >> 8U),
        interface_number,
        0U,
        (uint8_t)(*length & 0xFFU),
        (uint8_t)(*length >> 8U),
    };
    return execute_control(target, setup, NULL, data, length, true);
}

esp_err_t usb_host_control_bridge_control(
    gateway_port_target_t target,
    const uint8_t setup[8],
    uint8_t *data,
    size_t *length,
    bool direction_in)
{
    return execute_control(
        target,
        setup,
        direction_in ? NULL : data,
        direction_in ? data : NULL,
        length,
        direction_in);
}

/* Adapters for usb_proxy_control: requests from the S3 always target s3. */
static esp_err_t s3_set_report(
    uint8_t interface_number,
    uint8_t report_id,
    usb_proxy_report_type_t report_type,
    const uint8_t *data,
    size_t length)
{
    return usb_host_control_bridge_set_report(
        GATEWAY_PORT_TARGET_S3, interface_number, report_id, report_type, data, length);
}

static esp_err_t s3_get_report(
    uint8_t interface_number,
    uint8_t report_id,
    usb_proxy_report_type_t report_type,
    uint8_t *data,
    size_t *length)
{
    return usb_host_control_bridge_get_report(
        GATEWAY_PORT_TARGET_S3, interface_number, report_id, report_type, data, length);
}

static esp_err_t s3_control(
    const uint8_t setup[8],
    uint8_t *data,
    size_t *length,
    bool direction_in)
{
    return usb_host_control_bridge_control(
        GATEWAY_PORT_TARGET_S3, setup, data, length, direction_in);
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
        CONTROL_BUFFER_CAPACITY, 0, &s_transfer);
    if (error != ESP_OK) {
        return error;
    }
    usb_proxy_control_register_host(s3_set_report, s3_get_report, s3_control);
    ESP_LOGI(TAG, "EP0 bridge initialized (targets p4_fs, s3)");
    return ESP_OK;
}

void usb_host_control_bridge_attach(
    gateway_port_target_t target,
    usb_device_handle_t device_handle,
    uint8_t device_address)
{
    if (target != GATEWAY_PORT_TARGET_P4_FS && target != GATEWAY_PORT_TARGET_S3) {
        return;
    }
    /*
     * No mutex: attach/detach run in the USB host client task, which also
     * delivers control transfer completions. A running transfer on a removed
     * device completes with an error.
     */
    s_slots[target].device = device_handle;
    s_slots[target].address = device_address;
    ESP_LOGI(TAG, "Attached %s -> address %u", gateway_port_target_name(target), device_address);
}

void usb_host_control_bridge_detach(uint8_t device_address)
{
    if (device_address == 0U) {
        return;
    }
    for (size_t index = 0U; index < BRIDGE_TARGET_COUNT; ++index) {
        if (s_slots[index].address == device_address) {
            s_slots[index].device = NULL;
            s_slots[index].address = 0U;
            ESP_LOGI(TAG, "Detached %s (address %u)",
                gateway_port_target_name((gateway_port_target_t)index), device_address);
        }
    }
}
