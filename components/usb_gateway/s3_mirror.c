#include "s3_mirror.h"

#include <string.h>

#include "p4_s3_link.h"
#include "p4_s3_protocol.h"
#include "usb_mirror_descriptor.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#define S3_MIRROR_TASK_STACK        4096U
#define S3_MIRROR_TASK_PRIORITY     7U
#define S3_MIRROR_COMMAND_TIMEOUT   1500U       /* ms */
#define S3_MIRROR_POLL_MS           2000U
#define S3_MIRROR_RETRY_MS          1000U
#define S3_MIRROR_MAX_PACKET        64U
#define S3_MIRROR_CACHE_ENTRIES     4U

#define EVENT_SYNC                  (1UL << 0)
#define EVENT_END                   (1UL << 1)
#define EVENT_REQUEST               (1UL << 2)

typedef struct {
    uint8_t address;                            /* 0 = no device */
    uint32_t generation;
    uint8_t device[USB_MIRROR_DEVICE_DESC_LENGTH];
    uint8_t configuration[USB_MIRROR_CONFIGURATION_CAPACITY];
    uint16_t configuration_length;
    usb_mirror_layout_t layout;
    uint8_t report[USB_MIRROR_MAX_HID_INTERFACES][USB_MIRROR_REPORT_DESC_CAPACITY];
    uint16_t report_length[USB_MIRROR_MAX_HID_INTERFACES];
    char strings[USB_MIRROR_STRING_COUNT][USB_MIRROR_STRING_CAPACITY];
} session_t;

typedef struct {
    bool used;
    uint8_t address;
    uint8_t interface_number;
    uint16_t length;
    uint8_t data[USB_MIRROR_REPORT_DESC_CAPACITY];
} cache_entry_t;

static const char *TAG = "s3_mirror";

static SemaphoreHandle_t s_lock;
static TaskHandle_t s_task;
static session_t s_session;         /* guarded by s_lock */
static session_t s_snapshot;        /* used by the task only */
static cache_entry_t s_cache[S3_MIRROR_CACHE_ENTRIES];

static volatile uint8_t s_synced_address;   /* address confirmed by the S3 */
static volatile bool s_end_pending;
static uint32_t s_sessions_sent;
static uint32_t s_input_dropped;

/* ---------------------------------------------------------------------- */
/* Session data (caller holds s_lock)                                     */
/* ---------------------------------------------------------------------- */

static bool session_complete_locked(void)
{
    if (s_session.address == 0U) {
        return false;
    }
    for (uint8_t index = 0U; index < s_session.layout.hid_count; ++index) {
        if (s_session.report_length[index] == 0U) {
            return false;
        }
    }
    return true;
}

static void apply_report_locked(uint8_t interface_number, const uint8_t *data, size_t length)
{
    const int instance = usb_mirror_hid_instance(&s_session.layout, interface_number);
    if (instance < 0) {
        return;
    }
    memcpy(s_session.report[instance], data, length);
    s_session.report_length[instance] = (uint16_t)length;
    if (s_session.layout.hid_report_length[instance] != length) {
        /* Keep wDescriptorLength consistent with the delivered descriptor. */
        const uint16_t offset = s_session.layout.hid_descriptor_offset[instance];
        s_session.configuration[offset + 7U] = (uint8_t)(length & 0xFFU);
        s_session.configuration[offset + 8U] = (uint8_t)(length >> 8U);
        s_session.layout.hid_report_length[instance] = (uint16_t)length;
    }
    ++s_session.generation;
}

static void cache_store(uint8_t address, uint8_t interface_number, const uint8_t *data, size_t length)
{
    cache_entry_t *target = &s_cache[0];
    for (size_t index = 0U; index < S3_MIRROR_CACHE_ENTRIES; ++index) {
        if (!s_cache[index].used ||
            (s_cache[index].address == address &&
             s_cache[index].interface_number == interface_number)) {
            target = &s_cache[index];
            break;
        }
    }
    target->used = true;
    target->address = address;
    target->interface_number = interface_number;
    target->length = (uint16_t)length;
    memcpy(target->data, data, length);
}

static void cache_adopt_locked(uint8_t address)
{
    for (size_t index = 0U; index < S3_MIRROR_CACHE_ENTRIES; ++index) {
        if (s_cache[index].used && s_cache[index].address == address) {
            apply_report_locked(
                s_cache[index].interface_number,
                s_cache[index].data,
                s_cache[index].length);
            s_cache[index].used = false;
        }
    }
}

static void cache_forget(uint8_t address)
{
    for (size_t index = 0U; index < S3_MIRROR_CACHE_ENTRIES; ++index) {
        if (s_cache[index].address == address) {
            s_cache[index].used = false;
        }
    }
}

/* ---------------------------------------------------------------------- */
/* Transfer to the S3 (task context only)                                 */
/* ---------------------------------------------------------------------- */

static esp_err_t command(uint8_t type, const void *payload, size_t length, p4_s3_ack_t *ack)
{
    const esp_err_t error = p4_s3_link_command(
        type, (const uint8_t *)payload, length, ack, S3_MIRROR_COMMAND_TIMEOUT);
    if (error == ESP_FAIL) {
        ESP_LOGW(TAG, "S3 rejected type 0x%02X: status 0x%02X", type, ack->status);
    }
    return error;
}

static esp_err_t send_descriptor(uint8_t kind, uint8_t index, const uint8_t *data, size_t length)
{
    p4_s3_ack_t ack;
    size_t offset = 0U;
    do {
        const size_t chunk = (length - offset) > P4_S3_DESCRIPTOR_DATA_SIZE
            ? P4_S3_DESCRIPTOR_DATA_SIZE
            : length - offset;
        p4_s3_descriptor_chunk_t packet = {
            .kind = kind,
            .index = index,
            .offset = (uint16_t)offset,
            .total_length = (uint16_t)length,
            .data_length = (uint8_t)chunk,
        };
        memcpy(packet.data, &data[offset], chunk);
        const esp_err_t error = command(
            P4_S3_MSG_MIRROR_DESCRIPTOR,
            &packet,
            P4_S3_DESCRIPTOR_HEADER_SIZE + chunk,
            &ack);
        if (error != ESP_OK) {
            return error;
        }
        offset += chunk;
    } while (offset < length);
    return ESP_OK;
}

static esp_err_t send_session(const session_t *session)
{
    p4_s3_ack_t ack;
    const p4_s3_mirror_begin_t begin = { .session = session->address };
    esp_err_t error = command(P4_S3_MSG_MIRROR_BEGIN, &begin, sizeof(begin), &ack);
    if (error == ESP_OK) {
        error = send_descriptor(P4_S3_DESCRIPTOR_DEVICE, 0U,
            session->device, USB_MIRROR_DEVICE_DESC_LENGTH);
    }
    if (error == ESP_OK) {
        error = send_descriptor(P4_S3_DESCRIPTOR_CONFIGURATION, 0U,
            session->configuration, session->configuration_length);
    }
    for (uint8_t index = 1U; error == ESP_OK && index < USB_MIRROR_STRING_COUNT; ++index) {
        const size_t length = strlen(session->strings[index]);
        if (length > 0U) {
            error = send_descriptor(P4_S3_DESCRIPTOR_STRING, index,
                (const uint8_t *)session->strings[index], length);
        }
    }
    for (uint8_t index = 0U; error == ESP_OK && index < session->layout.hid_count; ++index) {
        error = send_descriptor(P4_S3_DESCRIPTOR_REPORT,
            session->layout.hid_interface[index],
            session->report[index], session->report_length[index]);
    }
    if (error == ESP_OK) {
        error = command(P4_S3_MSG_MIRROR_COMMIT, NULL, 0U, &ack);
    }
    return error;
}

static void send_end(void)
{
    p4_s3_ack_t ack;
    const esp_err_t error = command(P4_S3_MSG_MIRROR_END, NULL, 0U, &ack);
    if (error == ESP_OK) {
        s_end_pending = false;
        ESP_LOGI(TAG, "S3 session ended");
    }
}

/* Returns true and fills the snapshot if a complete session needs sending. */
static bool take_snapshot(void)
{
    bool ready = false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (session_complete_locked()) {
        s_snapshot = s_session;
        ready = true;
    }
    xSemaphoreGive(s_lock);
    return ready;
}

static bool sync_session(void)
{
    if (!take_snapshot()) {
        return true;
    }
    s_synced_address = 0U;
    const esp_err_t error = send_session(&s_snapshot);
    if (error != ESP_OK) {
        ESP_LOGW(TAG, "Session %u transfer failed: %s",
            s_snapshot.address, esp_err_to_name(error));
        return false;
    }
    /* Device removed or changed during the transfer: send again. */
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const bool current = s_session.address == s_snapshot.address &&
        s_session.generation == s_snapshot.generation;
    xSemaphoreGive(s_lock);
    if (!current) {
        return false;
    }
    s_synced_address = s_snapshot.address;
    ++s_sessions_sent;
    ESP_LOGI(TAG, "S3 session %u committed: %02X%02X:%02X%02X, %u HID interface(s), transfer #%lu",
        s_snapshot.address,
        s_snapshot.device[9], s_snapshot.device[8],
        s_snapshot.device[11], s_snapshot.device[10],
        s_snapshot.layout.hid_count, (unsigned long)s_sessions_sent);
    return true;
}

/* Polls the S3 statistics; returns true if the session must be re-sent. */
static bool poll_stats(bool log)
{
    p4_s3_ack_t ack;
    if (command(P4_S3_MSG_MIRROR_STATS, NULL, 0U, &ack) != ESP_OK ||
        ack.payload_length < sizeof(p4_s3_mirror_stats_t)) {
        return false;
    }
    p4_s3_mirror_stats_t stats;
    memcpy(&stats, ack.payload, sizeof(stats));
    if (log) {
        ESP_LOGI(TAG,
            "S3 MIRROR session=%u state=%u mounted=%u %04X:%04X in=%lu drop=%lu "
            "set=%lu setdrop=%lu get_stall=%lu last_get=if%u/id0x%02X/t%u "
            "p4drop=%lu uptime=%lus",
            stats.session, stats.state, stats.mounted,
            stats.vendor_id, stats.product_id,
            (unsigned long)stats.reports_sent,
            (unsigned long)stats.reports_dropped,
            (unsigned long)stats.set_report_forwarded,
            (unsigned long)stats.set_report_dropped,
            (unsigned long)stats.get_report_stalled,
            stats.last_get_interface, stats.last_get_report_id,
            stats.last_get_report_type,
            (unsigned long)s_input_dropped,
            (unsigned long)(stats.uptime_ms / 1000U));
    }
    const uint8_t synced = s_synced_address;
    return synced != 0U &&
        (stats.session != synced || stats.state != P4_S3_MIRROR_STATE_CONNECTED);
}

static void mirror_task(void *argument)
{
    (void)argument;
    uint32_t events = 0U;
    int64_t last_log_us = 0;

    /* A P4 restart may leave an old session on the S3. */
    s_end_pending = true;

    while (true) {
        const bool need_retry = s_end_pending ||
            (s_synced_address == 0U && take_snapshot());
        (void)xTaskNotifyWait(0U, UINT32_MAX, &events,
            pdMS_TO_TICKS(need_retry ? S3_MIRROR_RETRY_MS : S3_MIRROR_POLL_MS));

        if (s_end_pending || (events & EVENT_END) != 0U) {
            send_end();
        }
        if ((events & (EVENT_SYNC | EVENT_REQUEST)) != 0U ||
            (s_synced_address == 0U && !s_end_pending)) {
            if ((events & EVENT_REQUEST) != 0U) {
                ESP_LOGI(TAG, "S3 requested a session");
            }
            (void)sync_session();
        }

        bool log = false;
#if CONFIG_USB_GATEWAY_CAPTURE_ENABLE
        const int64_t now = esp_timer_get_time();
        if ((now - last_log_us) >=
            (int64_t)CONFIG_USB_GATEWAY_CAPTURE_DIAG_PERIOD_S * 1000000LL) {
            last_log_us = now;
            log = true;
        }
#else
        (void)last_log_us;
#endif
        if (poll_stats(log)) {
            ESP_LOGW(TAG, "S3 lost session %u, re-sending", s_synced_address);
            s_synced_address = 0U;
            (void)sync_session();
        }
    }
}

static void link_event_handler(uint8_t type, const uint8_t *payload, size_t length)
{
    (void)payload;
    (void)length;
    if (type == P4_S3_MSG_MIRROR_REQUEST && s_task != NULL) {
        s_synced_address = 0U;
        xTaskNotify(s_task, EVENT_REQUEST, eSetBits);
    }
}

/* ---------------------------------------------------------------------- */
/* Public API                                                             */
/* ---------------------------------------------------------------------- */

esp_err_t s3_mirror_init(void)
{
    if (s_lock != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(mirror_task, "s3_mirror", S3_MIRROR_TASK_STACK,
            NULL, S3_MIRROR_TASK_PRIORITY, &s_task) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    p4_s3_link_set_event_handler(link_event_handler);
    return ESP_OK;
}

esp_err_t s3_mirror_begin(
    uint8_t address,
    const usb_device_desc_t *device_descriptor,
    const usb_config_desc_t *configuration_descriptor,
    const usb_device_info_t *device_info)
{
    if (address == 0U || device_descriptor == NULL ||
        configuration_descriptor == NULL || device_info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_lock == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    const size_t length = configuration_descriptor->wTotalLength;
    if (length > USB_MIRROR_CONFIGURATION_CAPACITY) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    usb_mirror_layout_t layout;
    const esp_err_t error = usb_mirror_parse_configuration(
        (const uint8_t *)configuration_descriptor,
        length,
        USB_MIRROR_MAX_HID_INTERFACES,
        S3_MIRROR_MAX_PACKET,
        &layout);
    if (error != ESP_OK) {
        return error;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    const uint32_t generation = s_session.generation + 1U;
    memset(&s_session, 0, sizeof(s_session));
    s_session.generation = generation;
    s_session.address = address;
    memcpy(s_session.device, device_descriptor, USB_MIRROR_DEVICE_DESC_LENGTH);
    memcpy(s_session.configuration, configuration_descriptor, length);
    s_session.configuration_length = (uint16_t)length;
    s_session.layout = layout;

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
        if (index != 0U && index < USB_MIRROR_STRING_COUNT) {
            (void)usb_mirror_string_to_ascii(
                (const uint8_t *)sources[source].descriptor,
                s_session.strings[index],
                USB_MIRROR_STRING_CAPACITY,
                NULL);
        }
    }
    cache_adopt_locked(address);
    const bool complete = session_complete_locked();
    xSemaphoreGive(s_lock);

    s_synced_address = 0U;
    ESP_LOGI(TAG, "Session %u prepared: %04X:%04X, %u HID interface(s)",
        address, device_descriptor->idVendor, device_descriptor->idProduct,
        layout.hid_count);
    if (complete && s_task != NULL) {
        xTaskNotify(s_task, EVENT_SYNC, eSetBits);
    }
    return ESP_OK;
}

void s3_mirror_set_report_descriptor(
    uint8_t address,
    uint8_t interface_number,
    const uint8_t *descriptor,
    size_t length)
{
    if (s_lock == NULL || address == 0U || descriptor == NULL || length == 0U ||
        length > USB_MIRROR_REPORT_DESC_CAPACITY) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool complete = false;
    if (s_session.address == address) {
        apply_report_locked(interface_number, descriptor, length);
        complete = session_complete_locked();
    } else {
        cache_store(address, interface_number, descriptor, length);
    }
    xSemaphoreGive(s_lock);
    if (complete && s_task != NULL) {
        xTaskNotify(s_task, EVENT_SYNC, eSetBits);
    }
}

bool s3_mirror_input(
    uint8_t address,
    uint8_t interface_number,
    const uint8_t *report,
    size_t length)
{
    if (address == 0U || address != s_synced_address ||
        interface_number > (P4_S3_MSG_INPUT_LAST - P4_S3_MSG_INPUT_BASE) ||
        report == NULL || length == 0U || length > P4_S3_PAYLOAD_SIZE) {
        return false;
    }
    if (!p4_s3_link_enqueue(
            (uint8_t)(P4_S3_MSG_INPUT_BASE + interface_number), report, length)) {
        ++s_input_dropped;
        return false;
    }
    return true;
}

void s3_mirror_end(uint8_t address)
{
    if (s_lock == NULL || address == 0U) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    cache_forget(address);
    const bool active = s_session.address == address;
    if (active) {
        const uint32_t generation = s_session.generation + 1U;
        memset(&s_session, 0, sizeof(s_session));
        s_session.generation = generation;
    }
    xSemaphoreGive(s_lock);
    if (active) {
        s_synced_address = 0U;
        s_end_pending = true;
        if (s_task != NULL) {
            xTaskNotify(s_task, EVENT_END, eSetBits);
        }
    }
}
