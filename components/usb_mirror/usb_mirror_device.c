#include "usb_mirror_device.h"

#include <string.h>

#include "p4_s3_protocol.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "tusb.h"

#if !defined(CFG_TUD_HID) || (CFG_TUD_HID < 2)
#error "Mirror requires CONFIG_TINYUSB_HID_COUNT >= 2 (sdkconfig.defaults)"
#endif

#ifndef CFG_TUD_HID_EP_BUFSIZE
#define CFG_TUD_HID_EP_BUFSIZE 64
#endif

#define MIRROR_REPORT_MAX               CFG_TUD_HID_EP_BUFSIZE
#define MIRROR_TX_QUEUE_DEPTH           32U
#define MIRROR_TX_TASK_STACK            3072U
#define MIRROR_TX_TASK_PRIORITY         15U
#define MIRROR_DESC_CACHE_ENTRIES       4U
#define MIRROR_INIT_WAIT_MS             50U
#define MIRROR_GET_REPORT_LOG_LIMIT     16U

#define MIRROR_MAX_HID \
    ((CFG_TUD_HID) < USB_MIRROR_MAX_HID_INTERFACES \
        ? (CFG_TUD_HID) : USB_MIRROR_MAX_HID_INTERFACES)

typedef struct {
    uint8_t length;
    uint8_t data[MIRROR_REPORT_MAX];
} mirror_report_t;

/* Report descriptors that arrive before usb_mirror_device_begin(). */
typedef struct {
    bool used;
    uint8_t session;
    uint8_t interface_number;
    uint16_t length;
    uint8_t data[USB_MIRROR_REPORT_DESC_CAPACITY];
} descriptor_cache_entry_t;

static const char *TAG = "usb_mirror";

static usb_mirror_device_config_t s_config;
static SemaphoreHandle_t s_lock;
static QueueHandle_t s_tx_queue[MIRROR_MAX_HID];
static TaskHandle_t s_tx_task;

static volatile uint8_t s_state = P4_S3_MIRROR_STATE_IDLE;
static volatile uint8_t s_session;

/*
 * Descriptor buffers referenced by esp_tinyusb. They are only modified while
 * the PC port is disconnected. Initial content is a valid placeholder that
 * the PC never sees (port is disconnected right after installation).
 */
static tusb_desc_device_t s_device_descriptor = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = 0x303A,
    .idProduct = 0x4002,
    .bcdDevice = 0x0100,
    .bNumConfigurations = 1,
};
static uint8_t s_configuration[USB_MIRROR_CONFIGURATION_CAPACITY] = {
    9, TUSB_DESC_CONFIGURATION, 9, 0, 0, 1, 0, 0x80, 50,
};
_Static_assert(sizeof(tusb_desc_device_t) == USB_MIRROR_DEVICE_DESC_LENGTH,
    "device descriptor size mismatch");

static usb_mirror_layout_t s_layout;
static uint8_t s_report_descriptor[MIRROR_MAX_HID][USB_MIRROR_REPORT_DESC_CAPACITY];
static uint16_t s_report_length[MIRROR_MAX_HID];
static char s_strings[USB_MIRROR_STRING_COUNT][USB_MIRROR_STRING_CAPACITY];
static const char *s_string_table[USB_MIRROR_STRING_COUNT];

static descriptor_cache_entry_t s_descriptor_cache[MIRROR_DESC_CACHE_ENTRIES];

static uint32_t s_reports_sent;
static uint32_t s_reports_dropped;
static uint32_t s_set_report_forwarded;
static uint32_t s_set_report_dropped;
static uint32_t s_get_report_stalled;
static uint8_t s_last_get_interface;
static uint8_t s_last_get_report_id;
static uint8_t s_last_get_report_type;

/* ---------------------------------------------------------------------- */
/* Descriptor handling                                                    */
/* ---------------------------------------------------------------------- */

static void cache_store(
    uint8_t session,
    uint8_t interface_number,
    const uint8_t *descriptor,
    size_t length)
{
    descriptor_cache_entry_t *target = NULL;
    for (size_t index = 0U; index < MIRROR_DESC_CACHE_ENTRIES; ++index) {
        descriptor_cache_entry_t *entry = &s_descriptor_cache[index];
        if (entry->used &&
            entry->session == session &&
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
    target->session = session;
    target->interface_number = interface_number;
    target->length = (uint16_t)length;
    memcpy(target->data, descriptor, length);
}

static void cache_forget(uint8_t session)
{
    for (size_t index = 0U; index < MIRROR_DESC_CACHE_ENTRIES; ++index) {
        if (s_descriptor_cache[index].used &&
            (session == 0U || s_descriptor_cache[index].session == session)) {
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
    if (s_layout.hid_report_length[instance] != length) {
        const uint16_t offset = s_layout.hid_descriptor_offset[instance];
        ESP_LOGW(TAG, "iface %u: report descriptor %u B, HID descriptor states %u B; using %u B",
            interface_number, (unsigned)length,
            s_layout.hid_report_length[instance], (unsigned)length);
        s_configuration[offset + 7U] = (uint8_t)(length & 0xFFU);
        s_configuration[offset + 8U] = (uint8_t)(length >> 8U);
        s_layout.hid_report_length[instance] = (uint16_t)length;
    }
}

static void adopt_cache_locked(uint8_t session)
{
    for (size_t index = 0U; index < MIRROR_DESC_CACHE_ENTRIES; ++index) {
        descriptor_cache_entry_t *entry = &s_descriptor_cache[index];
        if (entry->used && entry->session == session) {
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
    const char *const strings[USB_MIRROR_STRING_COUNT],
    const char *field)
{
    if (*descriptor_index == 0U) {
        return;
    }
    if (*descriptor_index >= USB_MIRROR_STRING_COUNT) {
        ESP_LOGW(TAG, "%s string index %u not mirrorable, removed",
            field, *descriptor_index);
        *descriptor_index = 0U;
        return;
    }
    const char *text = strings != NULL ? strings[*descriptor_index] : NULL;
    if (text == NULL) {
        ESP_LOGW(TAG, "%s string not available, mirrored empty", field);
        text = "";
    }
    strncpy(s_strings[*descriptor_index], text, USB_MIRROR_STRING_CAPACITY - 1U);
    s_strings[*descriptor_index][USB_MIRROR_STRING_CAPACITY - 1U] = '\0';
}

static const char *string_at(uint8_t index)
{
    return index != 0U && index < USB_MIRROR_STRING_COUNT ? s_strings[index] : "";
}

static void flush_tx_queues(void)
{
    for (size_t index = 0U; index < MIRROR_MAX_HID; ++index) {
        if (s_tx_queue[index] != NULL) {
            (void)xQueueReset(s_tx_queue[index]);
        }
    }
}

static void end_locked(void)
{
    if (s_state == P4_S3_MIRROR_STATE_CONNECTED) {
        (void)tud_disconnect();
    }
    s_state = P4_S3_MIRROR_STATE_IDLE;
    s_session = 0U;
    memset(s_report_length, 0, sizeof(s_report_length));
    flush_tx_queues();
}

static void connect_if_complete_locked(void)
{
    if (s_state != P4_S3_MIRROR_STATE_COLLECTING) {
        return;
    }
    for (uint8_t index = 0U; index < s_layout.hid_count; ++index) {
        if (s_report_length[index] == 0U) {
            return;
        }
    }
    s_state = P4_S3_MIRROR_STATE_CONNECTED;
    (void)tud_connect();
    ESP_LOGI(TAG, "Mirror connected to PC: %04X:%04X, %u HID interface(s)",
        s_device_descriptor.idVendor, s_device_descriptor.idProduct,
        s_layout.hid_count);
}

/* ---------------------------------------------------------------------- */
/* Transmit task                                                          */
/* ---------------------------------------------------------------------- */

/* Single sender for all IN endpoints; no report is lost on a busy endpoint. */
static void mirror_tx_task(void *argument)
{
    (void)argument;
    mirror_report_t report;
    while (true) {
        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(10U));
        if (s_state != P4_S3_MIRROR_STATE_CONNECTED || !tud_mounted()) {
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

/*
 * GET_REPORT must be answered synchronously and is not forwarded.
 * Returning 0 makes TinyUSB STALL the request. Every request is counted so
 * the acceptance test can prove whether a host application depends on it.
 */
uint16_t tud_hid_get_report_cb(
    uint8_t instance,
    uint8_t report_id,
    hid_report_type_t report_type,
    uint8_t *buffer,
    uint16_t requested_length)
{
    (void)buffer;
    ++s_get_report_stalled;
    s_last_get_interface = instance < s_layout.hid_count
        ? s_layout.hid_interface[instance]
        : 0xFFU;
    s_last_get_report_id = report_id;
    s_last_get_report_type = (uint8_t)report_type;
    if (s_get_report_stalled <= MIRROR_GET_REPORT_LOG_LIMIT) {
        ESP_LOGW(TAG, "GET_REPORT stalled #%lu: iface=%u id=0x%02X type=%u length=%u",
            (unsigned long)s_get_report_stalled, s_last_get_interface,
            report_id, (unsigned)report_type, requested_length);
    }
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
    if (instance >= s_layout.hid_count || buffer == NULL || buffer_size == 0U ||
        s_config.set_report == NULL) {
        ++s_set_report_dropped;
        return;
    }
    uint8_t report[USB_MIRROR_REPORT_DESC_CAPACITY + 1U];
    size_t length = buffer_size;
    if (report_id != 0U) {
        if (length + 1U > sizeof(report)) {
            ++s_set_report_dropped;
            return;
        }
        report[0] = report_id;
        memcpy(&report[1], buffer, length);
        ++length;
    } else {
        if (length > sizeof(report)) {
            ++s_set_report_dropped;
            return;
        }
        memcpy(report, buffer, length);
    }
    s_config.set_report(
        s_layout.hid_interface[instance],
        report_id,
        report_type == HID_REPORT_TYPE_FEATURE ? 3U : 2U,
        report,
        length);
    ++s_set_report_forwarded;
}

/* ---------------------------------------------------------------------- */
/* Public API                                                             */
/* ---------------------------------------------------------------------- */

size_t usb_mirror_device_max_hid(void)
{
    return MIRROR_MAX_HID;
}

uint16_t usb_mirror_device_max_packet_size(void)
{
    return (uint16_t)CFG_TUD_HID_EP_BUFSIZE;
}

esp_err_t usb_mirror_device_init(const usb_mirror_device_config_t *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_lock != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    s_config = *config;

    for (size_t index = 0U; index < MIRROR_MAX_HID; ++index) {
        s_tx_queue[index] = xQueueCreate(MIRROR_TX_QUEUE_DEPTH, sizeof(mirror_report_t));
        if (s_tx_queue[index] == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    if (xTaskCreate(mirror_tx_task, "usb_mirror_tx", MIRROR_TX_TASK_STACK,
            NULL, MIRROR_TX_TASK_PRIORITY, &s_tx_task) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    /* Index 0: language ID 0x0409; descriptor strings are filled per session. */
    s_strings[0][0] = 0x09;
    s_strings[0][1] = 0x04;
    s_strings[0][2] = '\0';
    for (size_t index = 0U; index < USB_MIRROR_STRING_COUNT; ++index) {
        s_string_table[index] = s_strings[index];
    }

    tinyusb_config_t tusb_config = TINYUSB_DEFAULT_CONFIG();
    tusb_config.port = TINYUSB_PORT_FULL_SPEED_0;
    tusb_config.descriptor.device = &s_device_descriptor;
    tusb_config.descriptor.qualifier = NULL;
    tusb_config.descriptor.string = s_string_table;
    tusb_config.descriptor.string_count = (int)USB_MIRROR_STRING_COUNT;
    tusb_config.descriptor.full_speed_config = s_configuration;
    tusb_config.descriptor.high_speed_config = NULL;
    const esp_err_t error = tinyusb_driver_install(&tusb_config);
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "TinyUSB install failed: %s", esp_err_to_name(error));
        return error;
    }

    /* Disconnect before the PC can start enumeration (debounce >= 100 ms). */
    for (uint32_t waited = 0U; !tud_inited() && waited < MIRROR_INIT_WAIT_MS; ++waited) {
        vTaskDelay(pdMS_TO_TICKS(1U));
    }
    (void)tud_disconnect();

    s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }
    s_state = P4_S3_MIRROR_STATE_IDLE;
    ESP_LOGI(TAG, "Mirror ready (max %u HID interfaces, EP <= %u B); PC port disconnected",
        (unsigned)MIRROR_MAX_HID, (unsigned)CFG_TUD_HID_EP_BUFSIZE);
    return ESP_OK;
}

esp_err_t usb_mirror_device_begin(
    uint8_t session,
    const uint8_t device_descriptor[USB_MIRROR_DEVICE_DESC_LENGTH],
    const uint8_t *configuration,
    size_t configuration_length,
    const char *const strings[USB_MIRROR_STRING_COUNT])
{
    if (session == 0U || device_descriptor == NULL || configuration == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_lock == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (configuration_length > sizeof(s_configuration)) {
        ESP_LOGE(TAG, "Configuration descriptor %u B exceeds %u B",
            (unsigned)configuration_length, (unsigned)sizeof(s_configuration));
        return ESP_ERR_NOT_SUPPORTED;
    }
    usb_mirror_layout_t layout;
    const esp_err_t error = usb_mirror_parse_configuration(
        configuration,
        configuration_length,
        MIRROR_MAX_HID,
        (uint16_t)CFG_TUD_HID_EP_BUFSIZE,
        &layout);
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "Session %u not mirrorable: %s", session, esp_err_to_name(error));
        return error;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_state != P4_S3_MIRROR_STATE_IDLE) {
        ESP_LOGW(TAG, "Session %u replaces session %u", session, s_session);
        end_locked();
    }
    s_layout = layout;
    memcpy(s_configuration, configuration, configuration_length);
    memcpy(&s_device_descriptor, device_descriptor, sizeof(s_device_descriptor));
    if (s_device_descriptor.bMaxPacketSize0 != CFG_TUD_ENDPOINT0_SIZE) {
        ESP_LOGW(TAG, "bMaxPacketSize0 %u replaced by %u",
            s_device_descriptor.bMaxPacketSize0, (unsigned)CFG_TUD_ENDPOINT0_SIZE);
        s_device_descriptor.bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE;
    }
    s_device_descriptor.bNumConfigurations = 1U;

    for (size_t index = 1U; index < USB_MIRROR_STRING_COUNT; ++index) {
        s_strings[index][0] = '\0';
    }
    assign_string(&s_device_descriptor.iManufacturer, strings, "Manufacturer");
    assign_string(&s_device_descriptor.iProduct, strings, "Product");
    assign_string(&s_device_descriptor.iSerialNumber, strings, "Serial");

    memset(s_report_length, 0, sizeof(s_report_length));
    s_session = session;
    s_state = P4_S3_MIRROR_STATE_COLLECTING;
    adopt_cache_locked(session);

    ESP_LOGI(TAG, "Session %u %04X:%04X: config %u B, HID interfaces %u, "
        "manufacturer=\"%s\" product=\"%s\" serial=\"%s\"",
        session, s_device_descriptor.idVendor, s_device_descriptor.idProduct,
        (unsigned)configuration_length, s_layout.hid_count,
        string_at(s_device_descriptor.iManufacturer),
        string_at(s_device_descriptor.iProduct),
        string_at(s_device_descriptor.iSerialNumber));

    connect_if_complete_locked();
    xSemaphoreGive(s_lock);
    return ESP_OK;
}

void usb_mirror_device_set_report_descriptor(
    uint8_t session,
    uint8_t interface_number,
    const uint8_t *descriptor,
    size_t length)
{
    if (s_lock == NULL || session == 0U || descriptor == NULL || length == 0U ||
        length > USB_MIRROR_REPORT_DESC_CAPACITY) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_state == P4_S3_MIRROR_STATE_COLLECTING && s_session == session) {
        apply_report_descriptor_locked(interface_number, descriptor, length);
        connect_if_complete_locked();
    } else if (s_session != session) {
        cache_store(session, interface_number, descriptor, length);
    }
    xSemaphoreGive(s_lock);
}

bool usb_mirror_device_input(
    uint8_t session,
    uint8_t interface_number,
    const uint8_t *report,
    size_t length)
{
    if (s_state != P4_S3_MIRROR_STATE_CONNECTED || s_session != session ||
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

void usb_mirror_device_end(uint8_t session)
{
    if (s_lock == NULL) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    cache_forget(session);
    if (s_state != P4_S3_MIRROR_STATE_IDLE && (session == 0U || s_session == session)) {
        end_locked();
        ESP_LOGI(TAG, "Session ended, PC port disconnected");
    }
    xSemaphoreGive(s_lock);
}

bool usb_mirror_device_connected(void)
{
    return s_state == P4_S3_MIRROR_STATE_CONNECTED;
}

bool usb_mirror_device_mounted(void)
{
    return s_state == P4_S3_MIRROR_STATE_CONNECTED && tud_mounted();
}

void usb_mirror_device_get_stats(usb_mirror_device_stats_t *stats)
{
    if (stats == NULL) {
        return;
    }
    *stats = (usb_mirror_device_stats_t) {
        .session = s_session,
        .state = s_state,
        .mounted = usb_mirror_device_mounted(),
        .vendor_id = s_state != P4_S3_MIRROR_STATE_IDLE ? s_device_descriptor.idVendor : 0U,
        .product_id = s_state != P4_S3_MIRROR_STATE_IDLE ? s_device_descriptor.idProduct : 0U,
        .hid_count = s_layout.hid_count,
        .reports_sent = s_reports_sent,
        .reports_dropped = s_reports_dropped,
        .set_report_forwarded = s_set_report_forwarded,
        .set_report_dropped = s_set_report_dropped,
        .get_report_stalled = s_get_report_stalled,
        .last_get_interface = s_last_get_interface,
        .last_get_report_id = s_last_get_report_id,
        .last_get_report_type = s_last_get_report_type,
    };
}
