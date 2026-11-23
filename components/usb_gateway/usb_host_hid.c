#include "usb_host_hid.h"

#include <string.h>

#include "diagnostics.h"
#include "report_tap.h"
#include "s3_mirror.h"
#include "usb_capture.h"
#include "usb_device.h"
#include "usb_host.h"

#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "usb/hid_host.h"

#define USB_HOST_HID_REPORT_BUFFER_SIZE     64U
#define USB_HOST_HID_OPEN_QUEUE_DEPTH       8U
#define USB_HOST_HID_OPEN_SETTLE_MS         250U
#define USB_HOST_HID_OPEN_RETRY_MS          250U
#define USB_HOST_HID_OPEN_ATTEMPTS          3U
#define USB_HOST_HID_OPEN_TASK_STACK        6144U
#define USB_HOST_HID_OPEN_TASK_PRIORITY     6U
#define USB_HOST_HID_DRIVER_TASK_PRIORITY   5U
#define USB_HOST_HID_DRIVER_TASK_STACK      4096U

typedef struct {
    bool active;
    bool open_queued;
    bool opened;
    uint8_t interface_id;
    hid_host_device_handle_t handle;
    hid_host_dev_params_t params;
    hid_host_dev_info_t device_info;
    uint8_t report_descriptor[USB_HOST_HID_MAX_REPORT_DESCRIPTOR_SIZE];
    size_t report_descriptor_length;
    bool descriptor_parsed;
    hid_decoder_layout_t layout;
} usb_host_hid_interface_t;

typedef struct {
    usb_host_hid_interface_t *interface;
} hid_open_request_t;

static const char *TAG = "usb_host_hid";

static usb_host_hid_interface_t s_interfaces[USB_HOST_HID_MAX_INTERFACES];
static QueueHandle_t s_open_queue;
static uint8_t s_next_interface_id = 1U;
static size_t s_interface_count;

static usb_host_hid_interface_t *find_interface(hid_host_device_handle_t handle)
{
    for (size_t index = 0U; index < USB_HOST_HID_MAX_INTERFACES; ++index) {
        if (s_interfaces[index].active && s_interfaces[index].handle == handle) {
            return &s_interfaces[index];
        }
    }
    return NULL;
}

static usb_host_hid_interface_t *allocate_interface(hid_host_device_handle_t handle)
{
    for (size_t index = 0U; index < USB_HOST_HID_MAX_INTERFACES; ++index) {
        if (!s_interfaces[index].active) {
            usb_host_hid_interface_t *interface = &s_interfaces[index];
            memset(interface, 0, sizeof(*interface));
            interface->active = true;
            interface->handle = handle;
            interface->interface_id = s_next_interface_id++;
            if (s_next_interface_id == 0U) {
                s_next_interface_id = 1U;
            }
            ++s_interface_count;
            return interface;
        }
    }
    return NULL;
}

static void release_interface(usb_host_hid_interface_t *interface)
{
    if (interface == NULL || !interface->active) {
        return;
    }
    report_tap_interface_removed(interface->interface_id);
    if (interface->opened) {
        const esp_err_t error = hid_host_device_close(interface->handle);
        if (error != ESP_OK) {
            ESP_LOGW(TAG, "HID interface close failed: %s", esp_err_to_name(error));
        }
    }
    memset(interface, 0, sizeof(*interface));
    if (s_interface_count > 0U) {
        --s_interface_count;
    }
}

static void route_report(
    usb_host_hid_interface_t *interface,
    const uint8_t *report,
    size_t report_length)
{
    const uint8_t address = interface->params.addr;
    const uint8_t interface_number = interface->params.iface_num;

    switch (usb_host_route(address)) {
        case GATEWAY_PORT_TARGET_P4_FS:
            if (usb_device_mirror_input(address, interface_number, report, report_length)) {
                diagnostics_report_proxy_forwarded();
            } else if (usb_device_mirror_connected()) {
                diagnostics_report_proxy_error(ESP_ERR_NO_MEM);
            }
            break;

        case GATEWAY_PORT_TARGET_S3:
            if (usb_capture_is_active()) {
                (void)usb_capture_event(
                    USB_CAPTURE_EVENT_INPUT_REPORT,
                    interface_number,
                    0U,
                    0U,
                    report,
                    report_length,
                    ESP_OK);
            }
            /* false while the session is being (re-)sent: report dropped. */
            if (s3_mirror_input(address, interface_number, report, report_length)) {
                diagnostics_report_proxy_forwarded();
            }
            break;

        case GATEWAY_PORT_TARGET_TAP:
            break;

        case GATEWAY_PORT_TARGET_NONE:
        default:
            return;
    }
    /* Task 3: read-only copy, decoded in the report_tap task. */
    report_tap_submit(interface->interface_id, report, report_length);
}

static void handle_input_report(usb_host_hid_interface_t *interface)
{
    uint8_t report[USB_HOST_HID_REPORT_BUFFER_SIZE];
    size_t report_length = 0U;
    const esp_err_t error = hid_host_device_get_raw_input_report_data(
        interface->handle,
        report,
        sizeof(report),
        &report_length);
    if (error != ESP_OK) {
        diagnostics_report_hid_error(error);
        ESP_LOGW(TAG, "Cannot copy HID input report: %s", esp_err_to_name(error));
        return;
    }
    if (report_length == 0U) {
        return;
    }
    route_report(interface, report, report_length);
}

static void hid_host_interface_event_callback(
    hid_host_device_handle_t handle,
    const hid_host_interface_event_t event,
    void *argument)
{
    usb_host_hid_interface_t *interface = argument;
    if (interface == NULL || !interface->active || interface->handle != handle) {
        return;
    }
    switch (event) {
        case HID_HOST_INTERFACE_EVENT_INPUT_REPORT:
            handle_input_report(interface);
            break;
        case HID_HOST_INTERFACE_EVENT_TRANSFER_ERROR:
            diagnostics_report_hid_error(ESP_FAIL);
            ESP_LOGW(TAG, "HID input transfer error: interface=%u",
                interface->interface_id);
            break;
        case HID_HOST_INTERFACE_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "HID interface disconnected: interface=%u",
                interface->interface_id);
            release_interface(interface);
            break;
        default:
            break;
    }
}

static esp_err_t open_interface_once(usb_host_hid_interface_t *interface)
{
    const hid_host_device_config_t config = {
        .callback = hid_host_interface_event_callback,
        .callback_arg = interface,
    };
    esp_err_t error = hid_host_device_open(interface->handle, &config);
    if (error != ESP_OK) {
        return error;
    }
    interface->opened = true;
    error = hid_host_get_device_info(interface->handle, &interface->device_info);
    if (error != ESP_OK) {
        return error;
    }
    size_t descriptor_length = 0U;
    uint8_t *descriptor = hid_host_get_report_descriptor(
        interface->handle,
        &descriptor_length);
    if (descriptor == NULL || descriptor_length == 0U) {
        return ESP_ERR_NOT_FOUND;
    }
    if (descriptor_length > sizeof(interface->report_descriptor)) {
        return ESP_ERR_INVALID_SIZE;
    }
    memcpy(interface->report_descriptor, descriptor, descriptor_length);
    interface->report_descriptor_length = descriptor_length;
    interface->descriptor_parsed = hid_decoder_parse(
        descriptor,
        descriptor_length,
        &interface->layout) == HID_DECODER_OK;
    if (interface->layout.truncated) {
        ESP_LOGW(TAG, "addr=%u iface=%u: more than %u input fields, rest ignored",
            interface->params.addr, interface->params.iface_num,
            (unsigned)HID_DECODER_MAX_FIELDS);
    }
    diagnostics_report_hid_enum(
        interface->params.addr,
        interface->params.iface_num,
        0U,
        (uint16_t)descriptor_length);

    /* Both mirrors receive every report descriptor; each keeps its own session. */
    usb_device_mirror_set_report_descriptor(
        interface->params.addr,
        interface->params.iface_num,
        descriptor,
        descriptor_length);
    s3_mirror_set_report_descriptor(
        interface->params.addr,
        interface->params.iface_num,
        descriptor,
        descriptor_length);

    if (usb_capture_is_active() &&
        usb_host_route(interface->params.addr) == GATEWAY_PORT_TARGET_S3) {
        const esp_err_t capture_error = usb_capture_write_report_descriptor(
            interface->params.iface_num,
            descriptor,
            descriptor_length);
        if (capture_error != ESP_OK) {
            ESP_LOGW(TAG, "Cannot store report descriptor iface=%u: %s",
                interface->params.iface_num, esp_err_to_name(capture_error));
        }
    }
    return hid_host_device_start(interface->handle);
}

static void hid_open_worker(void *argument)
{
    (void)argument;
    hid_open_request_t request;
    while (true) {
        if (xQueueReceive(s_open_queue, &request, portMAX_DELAY) != pdPASS) {
            continue;
        }
        usb_host_hid_interface_t *interface = request.interface;
        if (interface == NULL || !interface->active || !interface->open_queued) {
            continue;
        }
        interface->open_queued = false;
        vTaskDelay(pdMS_TO_TICKS(USB_HOST_HID_OPEN_SETTLE_MS));
        esp_err_t error = ESP_FAIL;
        for (unsigned attempt = 1U;
             attempt <= USB_HOST_HID_OPEN_ATTEMPTS && interface->active;
             ++attempt) {
            error = open_interface_once(interface);
            if (error == ESP_OK) {
                break;
            }
            ESP_LOGW(TAG, "HID open retry: addr=%u iface=%u attempt=%u error=%s",
                interface->params.addr, interface->params.iface_num,
                attempt, esp_err_to_name(error));
            if (interface->opened) {
                (void)hid_host_device_close(interface->handle);
                interface->opened = false;
            }
            vTaskDelay(pdMS_TO_TICKS(USB_HOST_HID_OPEN_RETRY_MS));
        }
        if (error != ESP_OK) {
            diagnostics_report_hid_error(error);
            release_interface(interface);
            continue;
        }
        ESP_LOGI(TAG, "HID open: id=%u addr=%u iface=%u VID=%04X PID=%04X desc=%u "
            "fields=%u decoded=%u",
            interface->interface_id, interface->params.addr,
            interface->params.iface_num, interface->device_info.VID,
            interface->device_info.PID,
            (unsigned)interface->report_descriptor_length,
            interface->layout.field_count,
            interface->descriptor_parsed);
    }
}

static void hid_host_driver_event_callback(
    hid_host_device_handle_t handle,
    const hid_host_driver_event_t event,
    void *argument)
{
    (void)argument;
    if (event != HID_HOST_DRIVER_EVENT_CONNECTED || find_interface(handle) != NULL) {
        return;
    }
    usb_host_hid_interface_t *interface = allocate_interface(handle);
    if (interface == NULL) {
        ESP_LOGE(TAG, "HID interface capacity exhausted");
        return;
    }
    const esp_err_t error = hid_host_device_get_params(handle, &interface->params);
    if (error != ESP_OK) {
        release_interface(interface);
        return;
    }
    const hid_open_request_t request = { .interface = interface };
    interface->open_queued = true;
    if (xQueueSend(s_open_queue, &request, 0U) != pdPASS) {
        interface->open_queued = false;
        release_interface(interface);
    }
}

esp_err_t usb_host_hid_init(void)
{
    memset(s_interfaces, 0, sizeof(s_interfaces));
    s_next_interface_id = 1U;
    s_interface_count = 0U;
    s_open_queue = xQueueCreate(USB_HOST_HID_OPEN_QUEUE_DEPTH, sizeof(hid_open_request_t));
    if (s_open_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(
            hid_open_worker,
            "hid_desc_worker",
            USB_HOST_HID_OPEN_TASK_STACK,
            NULL,
            USB_HOST_HID_OPEN_TASK_PRIORITY,
            NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    const hid_host_driver_config_t driver_config = {
        .create_background_task = true,
        .task_priority = USB_HOST_HID_DRIVER_TASK_PRIORITY,
        .stack_size = USB_HOST_HID_DRIVER_TASK_STACK,
        .core_id = tskNO_AFFINITY,
        .callback = hid_host_driver_event_callback,
        .callback_arg = NULL,
    };
    return hid_host_install(&driver_config);
}

size_t usb_host_hid_get_interface_count(void)
{
    return s_interface_count;
}

bool usb_host_hid_get_interface_info(
    uint8_t interface_id,
    usb_host_hid_interface_info_t *info)
{
    if (info == NULL) {
        return false;
    }
    for (size_t index = 0U; index < USB_HOST_HID_MAX_INTERFACES; ++index) {
        const usb_host_hid_interface_t *interface = &s_interfaces[index];
        if (interface->active && interface->interface_id == interface_id) {
            *info = (usb_host_hid_interface_info_t) {
                .interface_id = interface->interface_id,
                .device_address = interface->params.addr,
                .interface_number = interface->params.iface_num,
                .interface_subclass = interface->params.sub_class,
                .interface_protocol = interface->params.proto,
                .vendor_id = interface->device_info.VID,
                .product_id = interface->device_info.PID,
                .descriptor_parsed = interface->descriptor_parsed,
                .report_descriptor_length = interface->report_descriptor_length,
                .report_descriptor = interface->report_descriptor,
                .layout = &interface->layout,
            };
            return true;
        }
    }
    return false;
}
