#include "usb_device.h"

#include <string.h>

#include "diagnostics.h"
#include "gateway_config.h"
#include "usb_host_control_bridge.h"
#include "usb_mirror_descriptor.h"
#include "usb_mirror_device.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "hal/usb_serial_jtag_ll.h"
#include "sdkconfig.h"

#define CONTROL_QUEUE_DEPTH         8U
#define CONTROL_TASK_STACK          4096U
#define CONTROL_TASK_PRIORITY       9U
#define CONTROL_REPORT_CAPACITY     65U     /* 64-byte report + restored ID */

typedef struct {
    uint8_t interface_number;
    uint8_t report_id;
    uint8_t report_type;
    uint8_t length;
    uint8_t data[CONTROL_REPORT_CAPACITY];
} control_request_t;

static const char *TAG = "usb_device";

static QueueHandle_t s_control_queue;
static bool s_ready;
static bool s_was_mounted;
static uint32_t s_control_dropped;

#if CONFIG_USB_GATEWAY_CAPTURE_ENABLE
static int64_t s_last_stats_us;
#endif

/* Called in the TinyUSB task: copy and hand over to the control worker. */
static void set_report_sink(
    uint8_t interface_number,
    uint8_t report_id,
    uint8_t report_type,
    const uint8_t *data,
    size_t length)
{
    if (s_control_queue == NULL || length == 0U || length > CONTROL_REPORT_CAPACITY) {
        ++s_control_dropped;
        return;
    }
    control_request_t request = {
        .interface_number = interface_number,
        .report_id = report_id,
        .report_type = report_type,
        .length = (uint8_t)length,
    };
    memcpy(request.data, data, length);
    if (xQueueSend(s_control_queue, &request, 0U) != pdPASS) {
        ++s_control_dropped;
    }
}

static void control_task(void *argument)
{
    (void)argument;
    control_request_t request;
    while (true) {
        if (xQueueReceive(s_control_queue, &request, portMAX_DELAY) != pdPASS) {
            continue;
        }
        const esp_err_t error = usb_host_control_bridge_set_report(
            GATEWAY_PORT_TARGET_P4_FS,
            request.interface_number,
            request.report_id,
            (usb_proxy_report_type_t)request.report_type,
            request.data,
            request.length);
        if (error != ESP_OK) {
            ESP_LOGW(TAG, "SET_REPORT iface=%u id=0x%02X len=%u failed: %s",
                request.interface_number, request.report_id,
                request.length, esp_err_to_name(error));
        }
    }
}

void usb_device_init(void)
{
#if CONFIG_IDF_TARGET_ESP32P4
    /*
     * Route USB Serial/JTAG to PHY1 (GPIO26/GPIO27) and USB OTG to PHY0
     * (GPIO24/GPIO25), where the board's Type-C 6 / USB1P1 connector is wired.
     * This is a runtime PHY selection; it does not program an eFuse.
     */
    usb_serial_jtag_ll_phy_select(1);
    ESP_LOGI(TAG, "USB PHY mapping: USB OTG=GPIO24/25, USB Serial/JTAG=GPIO26/27");
#endif
    s_control_queue = xQueueCreate(CONTROL_QUEUE_DEPTH, sizeof(control_request_t));
    if (s_control_queue == NULL ||
        xTaskCreate(control_task, "usb_mirror_ctl", CONTROL_TASK_STACK,
            NULL, CONTROL_TASK_PRIORITY, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Out of memory");
        diagnostics_set_device_state(DIAG_STATE_ERROR, ESP_ERR_NO_MEM);
        return;
    }
    const usb_mirror_device_config_t config = {
        .set_report = set_report_sink,
    };
    const esp_err_t error = usb_mirror_device_init(&config);
    if (error != ESP_OK) {
        diagnostics_set_device_state(DIAG_STATE_ERROR, error);
        return;
    }
    s_ready = true;
    diagnostics_set_device_state(DIAG_STATE_READY, ESP_OK);
}

void usb_device_process(void)
{
    if (!s_ready) {
        return;
    }
    const bool mounted = usb_mirror_device_mounted();
    if (mounted != s_was_mounted) {
        s_was_mounted = mounted;
        diagnostics_set_device_state(
            mounted ? DIAG_STATE_CONNECTED : DIAG_STATE_READY,
            ESP_OK);
        ESP_LOGI(TAG, "PC mirror %s", mounted ? "enumerated" : "inactive");
    }
#if CONFIG_USB_GATEWAY_CAPTURE_ENABLE
    const int64_t now = esp_timer_get_time();
    if ((now - s_last_stats_us) >=
        (int64_t)CONFIG_USB_GATEWAY_CAPTURE_DIAG_PERIOD_S * 1000000LL) {
        s_last_stats_us = now;
        usb_mirror_device_stats_t stats;
        usb_mirror_device_get_stats(&stats);
        ESP_LOGI(TAG,
            "P4 MIRROR session=%u state=%u mounted=%u %04X:%04X in=%lu drop=%lu "
            "set=%lu setdrop=%lu ctldrop=%lu get_stall=%lu last_get=if%u/id0x%02X/t%u",
            stats.session, stats.state, stats.mounted,
            stats.vendor_id, stats.product_id,
            (unsigned long)stats.reports_sent,
            (unsigned long)stats.reports_dropped,
            (unsigned long)stats.set_report_forwarded,
            (unsigned long)stats.set_report_dropped,
            (unsigned long)s_control_dropped,
            (unsigned long)stats.get_report_stalled,
            stats.last_get_interface, stats.last_get_report_id,
            stats.last_get_report_type);
    }
#endif
}

size_t usb_device_mirror_max_hid(void)
{
    return usb_mirror_device_max_hid();
}

esp_err_t usb_device_mirror_begin(
    uint8_t address,
    const usb_device_desc_t *device_descriptor,
    const usb_config_desc_t *configuration_descriptor,
    const usb_device_info_t *device_info)
{
    if (device_descriptor == NULL || configuration_descriptor == NULL ||
        device_info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    static char text[USB_MIRROR_STRING_COUNT][USB_MIRROR_STRING_CAPACITY];
    const char *strings[USB_MIRROR_STRING_COUNT] = { NULL };
    const struct {
        uint8_t index;
        const usb_str_desc_t *descriptor;
    } sources[] = {
        { device_descriptor->iManufacturer, device_info->str_desc_manufacturer },
        { device_descriptor->iProduct, device_info->str_desc_product },
        { device_descriptor->iSerialNumber, device_info->str_desc_serial_num },
    };
    for (size_t source = 0U; source < sizeof(sources) / sizeof(sources[0]); ++source) {
        const uint8_t index = sources[source].index;
        if (index == 0U || index >= USB_MIRROR_STRING_COUNT ||
            sources[source].descriptor == NULL) {
            continue;
        }
        bool lossy = false;
        (void)usb_mirror_string_to_ascii(
            (const uint8_t *)sources[source].descriptor,
            text[index],
            USB_MIRROR_STRING_CAPACITY,
            &lossy);
        if (lossy) {
            ESP_LOGW(TAG, "String %u shortened or non-ASCII replaced", index);
        }
        strings[index] = text[index];
    }
    return usb_mirror_device_begin(
        address,
        (const uint8_t *)device_descriptor,
        (const uint8_t *)configuration_descriptor,
        configuration_descriptor->wTotalLength,
        strings);
}

void usb_device_mirror_set_report_descriptor(
    uint8_t address,
    uint8_t interface_number,
    const uint8_t *descriptor,
    size_t length)
{
    usb_mirror_device_set_report_descriptor(address, interface_number, descriptor, length);
}

bool usb_device_mirror_input(
    uint8_t address,
    uint8_t interface_number,
    const uint8_t *report,
    size_t length)
{
    return usb_mirror_device_input(address, interface_number, report, length);
}

void usb_device_mirror_end(uint8_t address)
{
    if (address != 0U) {
        usb_mirror_device_end(address);
    }
}

bool usb_device_mirror_connected(void)
{
    return usb_mirror_device_connected();
}
