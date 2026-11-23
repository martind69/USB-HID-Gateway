#include "s3_mirror_session.h"

#include <stdbool.h>
#include <string.h>

#include "s3_proxy_reports.h"
#include "usb_mirror_descriptor.h"
#include "usb_mirror_device.h"

#include "esp_log.h"
#include "esp_timer.h"

typedef struct {
    uint8_t *data;
    size_t capacity;
    uint16_t total;
    uint16_t received;
} stage_buffer_t;

typedef struct {
    bool used;
    uint8_t interface_number;
    uint8_t data[USB_MIRROR_REPORT_DESC_CAPACITY];
    stage_buffer_t buffer;
} stage_report_t;

static const char *TAG = "s3_mirror";

/* Staging area, written by the SPI link task only. */
static uint8_t s_session;
static uint8_t s_device[USB_MIRROR_DEVICE_DESC_LENGTH];
static uint8_t s_configuration[USB_MIRROR_CONFIGURATION_CAPACITY];
static stage_buffer_t s_device_stage;
static stage_buffer_t s_configuration_stage;
static stage_report_t s_reports[USB_MIRROR_MAX_HID_INTERFACES];
static char s_strings[USB_MIRROR_STRING_COUNT][USB_MIRROR_STRING_CAPACITY];

static void set_report_sink(
    uint8_t interface_number,
    uint8_t report_id,
    uint8_t report_type,
    const uint8_t *data,
    size_t length)
{
    const esp_err_t error = s3_proxy_send_set_report(
        interface_number, report_id, report_type, data, length);
    if (error != ESP_OK) {
        ESP_LOGW(TAG, "SET_REPORT iface=%u id=0x%02X not queued: %s",
            interface_number, report_id, esp_err_to_name(error));
    }
}

static void stage_reset(void)
{
    s_session = 0U;
    s_device_stage = (stage_buffer_t) { .data = s_device, .capacity = sizeof(s_device) };
    s_configuration_stage = (stage_buffer_t) {
        .data = s_configuration,
        .capacity = sizeof(s_configuration),
    };
    for (size_t index = 0U; index < USB_MIRROR_MAX_HID_INTERFACES; ++index) {
        s_reports[index].used = false;
        s_reports[index].buffer = (stage_buffer_t) {
            .data = s_reports[index].data,
            .capacity = sizeof(s_reports[index].data),
        };
    }
    memset(s_strings, 0, sizeof(s_strings));
}

static bool stage_complete(const stage_buffer_t *buffer)
{
    return buffer->total > 0U && buffer->received == buffer->total;
}

static uint8_t stage_append(stage_buffer_t *buffer, const p4_s3_descriptor_chunk_t *chunk)
{
    if (chunk->offset == 0U) {
        if (chunk->total_length == 0U || chunk->total_length > buffer->capacity) {
            return P4_S3_STATUS_INVALID_LENGTH;
        }
        buffer->total = chunk->total_length;
        buffer->received = 0U;
    }
    if (chunk->total_length != buffer->total ||
        chunk->offset != buffer->received ||
        (size_t)chunk->offset + chunk->data_length > buffer->total) {
        return P4_S3_STATUS_INVALID_LENGTH;
    }
    memcpy(&buffer->data[chunk->offset], chunk->data, chunk->data_length);
    buffer->received = (uint16_t)(buffer->received + chunk->data_length);
    return P4_S3_STATUS_OK;
}

static stage_report_t *report_stage(uint8_t interface_number)
{
    stage_report_t *free_slot = NULL;
    for (size_t index = 0U; index < USB_MIRROR_MAX_HID_INTERFACES; ++index) {
        if (s_reports[index].used && s_reports[index].interface_number == interface_number) {
            return &s_reports[index];
        }
        if (!s_reports[index].used && free_slot == NULL) {
            free_slot = &s_reports[index];
        }
    }
    if (free_slot != NULL) {
        free_slot->used = true;
        free_slot->interface_number = interface_number;
    }
    return free_slot;
}

esp_err_t s3_mirror_session_init(void)
{
    stage_reset();
    const usb_mirror_device_config_t config = {
        .set_report = set_report_sink,
    };
    return usb_mirror_device_init(&config);
}

uint8_t s3_mirror_session_begin(const uint8_t *payload, size_t length)
{
    if (payload == NULL || length != sizeof(p4_s3_mirror_begin_t) || payload[0] == 0U) {
        return P4_S3_STATUS_INVALID_LENGTH;
    }
    stage_reset();
    s_session = payload[0];
    ESP_LOGI(TAG, "Session %u: receiving descriptors", s_session);
    return P4_S3_STATUS_OK;
}

uint8_t s3_mirror_session_descriptor(const uint8_t *payload, size_t length)
{
    if (s_session == 0U) {
        return P4_S3_STATUS_NO_SESSION;
    }
    if (payload == NULL || length < P4_S3_DESCRIPTOR_HEADER_SIZE) {
        return P4_S3_STATUS_INVALID_LENGTH;
    }
    p4_s3_descriptor_chunk_t chunk = { 0 };
    memcpy(&chunk, payload, length > sizeof(chunk) ? sizeof(chunk) : length);
    if (chunk.data_length > P4_S3_DESCRIPTOR_DATA_SIZE ||
        P4_S3_DESCRIPTOR_HEADER_SIZE + chunk.data_length != length) {
        return P4_S3_STATUS_INVALID_LENGTH;
    }

    switch (chunk.kind) {
        case P4_S3_DESCRIPTOR_DEVICE:
            if (chunk.total_length != USB_MIRROR_DEVICE_DESC_LENGTH) {
                return P4_S3_STATUS_INVALID_LENGTH;
            }
            return stage_append(&s_device_stage, &chunk);

        case P4_S3_DESCRIPTOR_CONFIGURATION:
            return stage_append(&s_configuration_stage, &chunk);

        case P4_S3_DESCRIPTOR_REPORT: {
            stage_report_t *report = report_stage(chunk.index);
            return report != NULL
                ? stage_append(&report->buffer, &chunk)
                : P4_S3_STATUS_NOT_MIRRORABLE;
        }

        case P4_S3_DESCRIPTOR_STRING:
            if (chunk.index == 0U || chunk.index >= USB_MIRROR_STRING_COUNT ||
                chunk.offset != 0U || chunk.data_length >= USB_MIRROR_STRING_CAPACITY) {
                return P4_S3_STATUS_INVALID_LENGTH;
            }
            memcpy(s_strings[chunk.index], chunk.data, chunk.data_length);
            s_strings[chunk.index][chunk.data_length] = '\0';
            return P4_S3_STATUS_OK;

        default:
            return P4_S3_STATUS_UNSUPPORTED;
    }
}

uint8_t s3_mirror_session_commit(void)
{
    if (s_session == 0U) {
        return P4_S3_STATUS_NO_SESSION;
    }
    if (!stage_complete(&s_device_stage) || !stage_complete(&s_configuration_stage)) {
        return P4_S3_STATUS_INVALID_STATE;
    }
    const char *strings[USB_MIRROR_STRING_COUNT] = { NULL };
    for (size_t index = 1U; index < USB_MIRROR_STRING_COUNT; ++index) {
        strings[index] = s_strings[index];
    }
    const esp_err_t error = usb_mirror_device_begin(
        s_session,
        s_device,
        s_configuration,
        s_configuration_stage.total,
        strings);
    if (error == ESP_ERR_NOT_SUPPORTED) {
        return P4_S3_STATUS_NOT_MIRRORABLE;
    }
    if (error != ESP_OK) {
        return P4_S3_STATUS_INVALID_STATE;
    }
    for (size_t index = 0U; index < USB_MIRROR_MAX_HID_INTERFACES; ++index) {
        if (s_reports[index].used && stage_complete(&s_reports[index].buffer)) {
            usb_mirror_device_set_report_descriptor(
                s_session,
                s_reports[index].interface_number,
                s_reports[index].data,
                s_reports[index].buffer.total);
        }
    }
    if (!usb_mirror_device_connected()) {
        ESP_LOGW(TAG, "Session %u incomplete: report descriptor missing", s_session);
        return P4_S3_STATUS_INVALID_STATE;
    }
    return P4_S3_STATUS_OK;
}

uint8_t s3_mirror_session_end(void)
{
    usb_mirror_device_end(0U);
    stage_reset();
    return P4_S3_STATUS_OK;
}

uint8_t s3_mirror_session_input(
    uint8_t interface_number,
    const uint8_t *report,
    size_t length)
{
    if (!usb_mirror_device_connected()) {
        return P4_S3_STATUS_NO_SESSION;
    }
    usb_mirror_device_stats_t stats;
    usb_mirror_device_get_stats(&stats);
    return usb_mirror_device_input(stats.session, interface_number, report, length)
        ? P4_S3_STATUS_OK
        : P4_S3_STATUS_QUEUE_FULL;
}

void s3_mirror_session_stats(p4_s3_mirror_stats_t *stats)
{
    if (stats == NULL) {
        return;
    }
    usb_mirror_device_stats_t device;
    usb_mirror_device_get_stats(&device);
    *stats = (p4_s3_mirror_stats_t) {
        .status = P4_S3_STATUS_OK,
        .session = device.session,
        .state = device.state,
        .mounted = device.mounted ? 1U : 0U,
        .vendor_id = device.vendor_id,
        .product_id = device.product_id,
        .reports_sent = device.reports_sent,
        .reports_dropped = device.reports_dropped,
        .set_report_forwarded = device.set_report_forwarded,
        .set_report_dropped = device.set_report_dropped,
        .get_report_stalled = device.get_report_stalled,
        .last_get_interface = device.last_get_interface,
        .last_get_report_id = device.last_get_report_id,
        .last_get_report_type = device.last_get_report_type,
        .uptime_ms = (uint32_t)(esp_timer_get_time() / 1000LL),
    };
}
