#include "usb_device.h"

#include <string.h>

#include "diagnostics.h"
#include "gateway_config.h"
#include "hid_proxy.h"
#include "usb_host_control_bridge.h"
#include "usb_mirror_descriptor.h"
#include "usb_proxy_control.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "hal/usb_serial_jtag_ll.h"
#include "sdkconfig.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "tusb.h"

#if !defined(CFG_TUD_HID) || (CFG_TUD_HID < 2)
#error "Mirror requires CONFIG_TINYUSB_HID_COUNT >= 2 (sdkconfig.defaults)"
#endif

#ifndef CFG_TUD_HID_EP_BUFSIZE
#define CFG_TUD_HID_EP_BUFSIZE 64
#endif

#define MIRROR_CONFIGURATION_CAPACITY   512U
#define MIRROR_REPORT_DESC_CAPACITY     512U
#define MIRROR_STRING_COUNT             8U      /* index 0 = language ID */
#define MIRROR_STRING_CAPACITY          32U     /* 31 characters + NUL */
#define MIRROR_REPORT_MAX               CFG_TUD_HID_EP_BUFSIZE
#define MIRROR_TX_QUEUE_DEPTH           32U
#define MIRROR_TX_TASK_STACK            3072U
#define MIRROR_TX_TASK_PRIORITY         15U
#define MIRROR_CONTROL_QUEUE_DEPTH      8U
#define MIRROR_CONTROL_TASK_STACK       4096U
#define MIRROR_CONTROL_TASK_PRIORITY    9U
#define MIRROR_DESC_CACHE_ENTRIES       6U

#define MIRROR_MAX_HID \
    ((CFG_TUD_HID) < USB_MIRROR_MAX_HID_INTERFACES \
        ? (CFG_TUD_HID) : USB_MIRROR_MAX_HID_INTERFACES)

typedef enum {
    MIRROR_IDLE = 0,
    MIRROR_COLLECTING,
    MIRROR_CONNECTED,
} mirror_state_t;

typedef struct {
    uint8_t length;
    uint8_t data[MIRROR_REPORT_MAX];
} mirror_report_t;

typedef struct {
    uint8_t interface_number;
    uint8_t report_id;
    uint8_t report_type;
    uint8_t length;
    uint8_t data[MIRROR_REPORT_MAX + 1U];
} mirror_control_request_t;

/* Report descriptors that arrive before usb_device_mirror_begin(). */
typedef struct {
    bool used;
    uint8_t address;
    uint8_t interface_number;
    uint16_t length;
    uint8_t data[MIRROR_REPORT_DESC_CAPACITY];
} descriptor_cache_entry_t;

static const char *TAG = "usb_mirror";

static SemaphoreHandle_t s_lock;
static QueueHandle_t s_tx_queue[MIRROR_MAX_HID];
static QueueHandle_t s_control_queue;
static TaskHandle_t s_tx_task;

static bool s_driver_installed;
static bool s_was_mounted;
static volatile mirror_state_t s_state;
static volatile uint8_t s_address;

/* Descriptor buffers referenced by esp_tinyusb; modified only while disconnected. */
static tusb_desc_device_t s_device_descriptor;
static uint8_t s_configuration[MIRROR_CONFIGURATION_CAPACITY];
static usb_mirror_layout_t s_layout;
static uint8_t s_report_descriptor[MIRROR_MAX_HID][MIRROR_REPORT_DESC_CAPACITY];
static uint16_t s_report_length[MIRROR_MAX_HID];
static char s_strings[MIRROR_STRING_COUNT][MIRROR_STRING_CAPACITY];
static const char *s_string_table[MIRROR_STRING_COUNT];

static descriptor_cache_entry_t s_descriptor_cache[MIRROR_DESC_CACHE_ENTRIES];

static uint32_t s_reports_sent;
static uint32_t s_reports_dropped;
static uint32_t s_control_dropped;

/* ---------------------------------------------------------------------- */
/* Descriptor handling                                                    */
/* ---------------------------------------------------------------------- */

static void cache_store(
    uint8_t address,
    uint8_t interface_number,
    const uint8_t *descriptor,
    size_t length)
{
    descriptor_cache_entry_t *target = NULL;
    for (size_t index = 0U; index < MIRROR_DESC_CACHE_ENTRIES; ++index) {
        descriptor_cache_entry_t *entry = &s_descriptor_cache[index];
        if (entry->used &&
            entry->address == address &&
            entry->interface_number == interface_number) {
            target = entry;
            break;
        }
        if (!entry->used && target == NULL) {
            target = entry;
        }
    }
    if (target == NULL) {
        /* Cache full: replace the first entry. */
        target = &s_descriptor_cache[0];
    }
    target->used = true;
    target->address = address;
    target->interface_number = interface_number;
    target->length = (uint16_t)length;
    memcpy(target->data, descriptor, length);
}

static void cache_forget(uint8_t address)
{
    for (size_t index = 0U; index < MIRROR_DESC_CACHE_ENTRIES; ++index) {
        if (s_descriptor_cache[index].used &&
            s_descriptor_cache[index].address == address) {
            s_descriptor_cache[index].used = false;
        }
    }
}

static void apply_report_descriptor_locked(
    uint8_t interface_number,
    const uint8_t *descriptor,
    size_t length)
{
    const int instance = usb_mirror_hid_instance(&s_layout, interface_number);
    if (instance < 0) {
        return;
    }
    memcpy(s_report_descriptor[instance], descriptor, length);
    s_report_length[instance] = (uint16_t)length;

    /* Keep wDescriptorLength of the HID descriptor consistent. */
    const uint16_t offset = s_layout.hid_descriptor_offset[instance];
    if (s_layout.hid_report_length[instance] != length) {
        ESP_LOGW(TAG, "iface %u: report descriptor %u B, HID descriptor states %u B; using %u B",
            interface_number, (unsigned)length,
            s_layout.hid_report_length[instance], (unsigned)length);
        s_configuration[offset + 7U] = (uint8_t)(length & 0xFFU);
        s_configuration[offset + 8U] = (uint8_t)(length >> 8U);
        s_layout.hid_report_length[instance] = (uint16_t)length;
    }
}

static void adopt_cache_locked(uint8_t address)
{
    for (size_t index = 0U; index < MIRROR_DESC_CACHE_ENTRIES; ++index) {
        descriptor_cache_entry_t *entry = &s_descriptor_cache[index];
        if (entry->used && entry->address == address) {
            apply_report_descriptor_locked(
                entry->interface_number,
                entry->data,
                entry->length);
            entry->used = false;
        }
    }
}

static void assign_string(
    uint8_t *descriptor_index,
    const usb_str_desc_t *string_descriptor,
    const char *field)
{
    if (*descriptor_index == 0U) {
        return;
    }
    if (*descriptor_index >= MIRROR_STRING_COUNT) {
        ESP_LOGW(TAG, "%s string index %u not mirrorable, removed",
            field, *descriptor_index);
        *descriptor_index = 0U;
        return;
    }
    bool lossy = false;
    (void)usb_mirror_string_to_ascii(
        (const uint8_t *)string_descriptor,
        s_strings[*descriptor_index],
        MIRROR_STRING_CAPACITY,
        &lossy);
    if (string_descriptor == NULL) {
        ESP_LOGW(TAG, "%s string not read by host stack, mirrored empty", field);
    } else if (lossy) {
        ESP_LOGW(TAG, "%s string shortened or non-ASCII replaced: \"%s\"",
            field, s_strings[*descriptor_index]);
    }
}

static const char *mirrored_string(uint8_t index)
{
    return index != 0U && index < MIRROR_STRING_COUNT ? s_strings[index] : "";
}

/* ---------------------------------------------------------------------- */
/* PC connection                                                          */
/* ---------------------------------------------------------------------- */

static void usb_device_event_handler(tinyusb_event_t *event, void *context)
{
    (void)context;
    if (event == NULL) {
        return;
    }
    switch (event->id) {
        case TINYUSB_EVENT_ATTACHED:
            ESP_LOGI(TAG, "PC attached");
            break;
        case TINYUSB_EVENT_DETACHED:
            ESP_LOGI(TAG, "PC detached");
            break;
        default:
            break;
    }
}

static esp_err_t connect_locked(void)
{
    if (!s_driver_installed) {
        tinyusb_config_t config = TINYUSB_DEFAULT_CONFIG(usb_device_event_handler);
        config.port = TINYUSB_PORT_FULL_SPEED_0;
        config.descriptor.device = &s_device_descriptor;
        config.descriptor.qualifier = NULL;
        config.descriptor.string = s_string_table;
        config.descriptor.string_count = (int)MIRROR_STRING_COUNT;
        config.descriptor.full_speed_config = s_configuration;
        config.descriptor.high_speed_config = NULL;
        const esp_err_t error = tinyusb_driver_install(&config);
        if (error != ESP_OK) {
            ESP_LOGE(TAG, "TinyUSB install failed: %s", esp_err_to_name(error));
            diagnostics_set_device_state(DIAG_STATE_ERROR, error);
            return error;
        }
        s_driver_installed = true;
    } else {
        (void)tud_connect();
    }
    s_state = MIRROR_CONNECTED;
    diagnostics_set_device_state(DIAG_STATE_READY, ESP_OK);
    ESP_LOGI(TAG, "Mirror connected to PC: %04X:%04X, %u HID interface(s)",
        s_device_descriptor.idVendor, s_device_descriptor.idProduct,
        s_layout.hid_count);
    return ESP_OK;
}

static void connect_if_complete_locked(void)
{
    if (s_state != MIRROR_COLLECTING) {
        return;
    }
    for (uint8_t index = 0U; index < s_layout.hid_count; ++index) {
        if (s_report_length[index] == 0U) {
            return;
        }
    }
    (void)connect_locked();
}

static void flush_tx_queues(void)
{
    for (size_t index = 0U; index < MIRROR_MAX_HID; ++index) {
        if (s_tx_queue[index] != NULL) {
            (void)xQueueReset(s_tx_queue[index]);
        }
    }
}

/* ---------------------------------------------------------------------- */
/* Tasks                                                                  */
/* ---------------------------------------------------------------------- */

/* Single sender for all IN endpoints; no report is lost on a busy endpoint. */
static void mirror_tx_task(void *argument)
{
    (void)argument;
    mirror_report_t report;
    while (true) {
        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(10U));
        if (s_state != MIRROR_CONNECTED || !tud_mounted()) {
            continue;
        }
        for (uint8_t instance = 0U; instance < s_layout.hid_count; ++instance) {
            while (tud_hid_n_ready(instance) &&
                xQueuePeek(s_tx_queue[instance], &report, 0U) == pdPASS) {
                /* Report ID 0: data is sent byte-identical, ID included. */
                if (!tud_hid_n_report(instance, 0U, report.data, report.length)) {
                    break;
                }
                (void)xQueueReceive(s_tx_queue[instance], &report, 0U);
                ++s_reports_sent;
            }
        }
    }
}

static void mirror_control_task(void *argument)
{
    (void)argument;
    mirror_control_request_t request;
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

/* ---------------------------------------------------------------------- */
/* TinyUSB callbacks                                                      */
/* ---------------------------------------------------------------------- */

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance)
{
    if (instance >= s_layout.hid_count || instance >= MIRROR_MAX_HID) {
        return NULL;
    }
    return s_report_descriptor[instance];
}

void tud_hid_report_complete_cb(uint8_t instance, uint8_t const *report, uint16_t length)
{
    (void)instance;
    (void)report;
    (void)length;
    if (s_tx_task != NULL) {
        xTaskNotifyGive(s_tx_task);
    }
}

/* GET_REPORT is answered synchronously by TinyUSB; not forwarded (STALL). */
uint16_t tud_hid_get_report_cb(
    uint8_t instance,
    uint8_t report_id,
    hid_report_type_t report_type,
    uint8_t *buffer,
    uint16_t requested_length)
{
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)requested_length;
    return 0U;
}

/*
 * TinyUSB removes a leading Report ID from a control SET_REPORT before this
 * callback. The complete report (ID + payload) is restored and forwarded.
 * Interrupt OUT data arrives with report_id 0 and is forwarded unchanged as
 * an output report.
 */
void tud_hid_set_report_cb(
    uint8_t instance,
    uint8_t report_id,
    hid_report_type_t report_type,
    uint8_t const *buffer,
    uint16_t buffer_size)
{
    if (instance >= s_layout.hid_count || buffer == NULL || buffer_size == 0U) {
        return;
    }
    mirror_control_request_t request = {
        .interface_number = s_layout.hid_interface[instance],
        .report_id = report_id,
        .report_type = report_type == HID_REPORT_TYPE_FEATURE
            ? (uint8_t)USB_PROXY_REPORT_FEATURE
            : (uint8_t)USB_PROXY_REPORT_OUTPUT,
    };
    size_t length = buffer_size;
    if (report_id != 0U) {
        if (length + 1U > sizeof(request.data)) {
            ++s_control_dropped;
            return;
        }
        request.data[0] = report_id;
        memcpy(&request.data[1], buffer, length);
        ++length;
    } else {
        if (length > sizeof(request.data)) {
            ++s_control_dropped;
            return;
        }
        memcpy(request.data, buffer, length);
    }
    request.length = (uint8_t)length;
    if (xQueueSend(s_control_queue, &request, 0U) != pdPASS) {
        ++s_control_dropped;
    }
}

/* ---------------------------------------------------------------------- */
/* Public API                                                             */
/* ---------------------------------------------------------------------- */

size_t usb_device_mirror_max_hid(void)
{
    return MIRROR_MAX_HID;
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
    bool allocated = true;
    s_control_queue = xQueueCreate(
        MIRROR_CONTROL_QUEUE_DEPTH,
        sizeof(mirror_control_request_t));
    allocated = allocated && s_control_queue != NULL;
    for (size_t index = 0U; index < MIRROR_MAX_HID; ++index) {
        s_tx_queue[index] = xQueueCreate(MIRROR_TX_QUEUE_DEPTH, sizeof(mirror_report_t));
        allocated = allocated && s_tx_queue[index] != NULL;
    }
    if (allocated) {
        /* s_lock is created last; a non-NULL s_lock marks a usable module. */
        s_lock = xSemaphoreCreateMutex();
    }
    if (!allocated || s_lock == NULL) {
        ESP_LOGE(TAG, "Out of memory");
        diagnostics_set_device_state(DIAG_STATE_ERROR, ESP_ERR_NO_MEM);
        return;
    }
    if (xTaskCreate(mirror_tx_task, "usb_mirror_tx", MIRROR_TX_TASK_STACK,
            NULL, MIRROR_TX_TASK_PRIORITY, &s_tx_task) != pdPASS ||
        xTaskCreate(mirror_control_task, "usb_mirror_ctl", MIRROR_CONTROL_TASK_STACK,
            NULL, MIRROR_CONTROL_TASK_PRIORITY, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Cannot create mirror tasks");
        diagnostics_set_device_state(DIAG_STATE_ERROR, ESP_ERR_NO_MEM);
        return;
    }

    /* Language ID string; descriptor strings are filled per device. */
    s_strings[0][0] = 0x09;
    s_strings[0][1] = 0x04;
    s_strings[0][2] = '\0';
    for (size_t index = 0U; index < MIRROR_STRING_COUNT; ++index) {
        s_string_table[index] = s_strings[index];
    }

    /* The semantic composite output is replaced by the mirror. */
    hid_proxy_set_output_enabled(false);

    s_state = MIRROR_IDLE;
    diagnostics_set_device_state(DIAG_STATE_READY, ESP_OK);
    ESP_LOGI(TAG, "Mirror ready, PC port stays disconnected until a p4_fs device is present");
}

void usb_device_process(void)
{
    if (!s_driver_installed) {
        return;
    }
    const bool mounted = tud_mounted() && s_state == MIRROR_CONNECTED;
    if (mounted == s_was_mounted) {
        return;
    }
    s_was_mounted = mounted;
    if (mounted) {
        ESP_LOGI(TAG, "PC enumerated mirror %04X:%04X",
            s_device_descriptor.idVendor, s_device_descriptor.idProduct);
        diagnostics_set_device_state(DIAG_STATE_CONNECTED, ESP_OK);
    } else {
        ESP_LOGI(TAG, "PC mirror connection inactive, sent=%lu dropped=%lu ctl_dropped=%lu",
            (unsigned long)s_reports_sent, (unsigned long)s_reports_dropped,
            (unsigned long)s_control_dropped);
        diagnostics_set_device_state(DIAG_STATE_READY, ESP_OK);
    }
}

esp_err_t usb_device_mirror_begin(
    uint8_t address,
    const usb_device_desc_t *device_descriptor,
    const usb_config_desc_t *configuration_descriptor,
    const usb_device_info_t *device_info)
{
    if (device_descriptor == NULL || configuration_descriptor == NULL ||
        device_info == NULL || address == 0U) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_lock == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    const size_t configuration_length = configuration_descriptor->wTotalLength;
    if (configuration_length > sizeof(s_configuration)) {
        ESP_LOGE(TAG, "Configuration descriptor %u B exceeds %u B",
            (unsigned)configuration_length, (unsigned)sizeof(s_configuration));
        return ESP_ERR_NOT_SUPPORTED;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_state != MIRROR_IDLE) {
        xSemaphoreGive(s_lock);
        ESP_LOGE(TAG, "Mirror busy with address %u", s_address);
        return ESP_ERR_INVALID_STATE;
    }

    usb_mirror_layout_t layout;
    const esp_err_t error = usb_mirror_parse_configuration(
        (const uint8_t *)configuration_descriptor,
        configuration_length,
        MIRROR_MAX_HID,
        (uint16_t)CFG_TUD_HID_EP_BUFSIZE,
        &layout);
    if (error != ESP_OK) {
        xSemaphoreGive(s_lock);
        ESP_LOGE(TAG, "Device at address %u not mirrorable: %s",
            address, esp_err_to_name(error));
        return error;
    }

    s_layout = layout;
    memcpy(s_configuration, configuration_descriptor, configuration_length);
    memcpy(&s_device_descriptor, device_descriptor, sizeof(s_device_descriptor));
    if (s_device_descriptor.bMaxPacketSize0 != CFG_TUD_ENDPOINT0_SIZE) {
        ESP_LOGW(TAG, "bMaxPacketSize0 %u replaced by %u",
            s_device_descriptor.bMaxPacketSize0, (unsigned)CFG_TUD_ENDPOINT0_SIZE);
        s_device_descriptor.bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE;
    }
    s_device_descriptor.bNumConfigurations = 1U;

    for (size_t index = 1U; index < MIRROR_STRING_COUNT; ++index) {
        s_strings[index][0] = '\0';
    }
    assign_string(&s_device_descriptor.iManufacturer,
        device_info->str_desc_manufacturer, "Manufacturer");
    assign_string(&s_device_descriptor.iProduct,
        device_info->str_desc_product, "Product");
    assign_string(&s_device_descriptor.iSerialNumber,
        device_info->str_desc_serial_num, "Serial");

    memset(s_report_length, 0, sizeof(s_report_length));
    s_address = address;
    s_state = MIRROR_COLLECTING;
    adopt_cache_locked(address);

    ESP_LOGI(TAG, "Mirror session addr=%u %04X:%04X: config %u B, HID interfaces %u, "
        "manufacturer=\"%s\" product=\"%s\" serial=\"%s\"",
        address, s_device_descriptor.idVendor, s_device_descriptor.idProduct,
        (unsigned)configuration_length, s_layout.hid_count,
        mirrored_string(s_device_descriptor.iManufacturer),
        mirrored_string(s_device_descriptor.iProduct),
        mirrored_string(s_device_descriptor.iSerialNumber));

    connect_if_complete_locked();
    xSemaphoreGive(s_lock);
    return ESP_OK;
}

void usb_device_mirror_set_report_descriptor(
    uint8_t address,
    uint8_t interface_number,
    const uint8_t *descriptor,
    size_t length)
{
    if (s_lock == NULL || descriptor == NULL || length == 0U ||
        length > MIRROR_REPORT_DESC_CAPACITY) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_state == MIRROR_COLLECTING && s_address == address) {
        apply_report_descriptor_locked(interface_number, descriptor, length);
        connect_if_complete_locked();
    } else if (s_state == MIRROR_IDLE || s_address != address) {
        cache_store(address, interface_number, descriptor, length);
    }
    xSemaphoreGive(s_lock);
}

bool usb_device_mirror_input(
    uint8_t address,
    uint8_t interface_number,
    const uint8_t *report,
    size_t length)
{
    if (s_state != MIRROR_CONNECTED || s_address != address ||
        report == NULL || length == 0U || length > MIRROR_REPORT_MAX) {
        return false;
    }
    const int instance = usb_mirror_hid_instance(&s_layout, interface_number);
    if (instance < 0) {
        return false;
    }
    mirror_report_t item = { .length = (uint8_t)length };
    memcpy(item.data, report, length);
    if (xQueueSend(s_tx_queue[instance], &item, 0U) != pdPASS) {
        ++s_reports_dropped;
        return false;
    }
    xTaskNotifyGive(s_tx_task);
    return true;
}

void usb_device_mirror_end(uint8_t address)
{
    if (s_lock == NULL) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    cache_forget(address);
    if (s_state != MIRROR_IDLE && s_address == address) {
        if (s_state == MIRROR_CONNECTED) {
            (void)tud_disconnect();
        }
        s_state = MIRROR_IDLE;
        s_address = 0U;
        memset(s_report_length, 0, sizeof(s_report_length));
        flush_tx_queues();
        ESP_LOGI(TAG, "Mirror session ended, PC port disconnected");
    }
    xSemaphoreGive(s_lock);
}

bool usb_device_mirror_connected(void)
{
    return s_state == MIRROR_CONNECTED;
}
