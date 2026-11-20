#include "g815_capture.h"

#include "sd_storage.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CAPTURE_MAGIC 0x47383135U
#define CAPTURE_VERSION 1U
#define CAPTURE_PAYLOAD_MAX 512U
#define CAPTURE_PATH_MAX 256U

static const char *TAG = "g815_capture";

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t version;
    uint16_t header_size;
    uint64_t timestamp_us;
    uint32_t sequence;
    uint16_t event_type;
    uint8_t interface_number;
    uint8_t report_id;
    uint8_t endpoint;
    uint8_t reserved;
    int32_t result;
    uint16_t payload_length;
    uint16_t original_length;
} capture_record_header_t;

typedef struct __attribute__((packed)) {
    g815_capture_setup_packet_t setup;
    uint16_t data_length;
    uint16_t original_length;
} capture_control_payload_t;

static SemaphoreHandle_t s_lock;
static FILE *s_events;
static bool s_active;
static uint32_t s_sequence;
static uint32_t s_dropped;
static char s_session_path[CAPTURE_PATH_MAX];

static esp_err_t make_directory(const char *path)
{
    if (mkdir(path, 0775) == 0 || errno == EEXIST) {
        return ESP_OK;
    }
    return ESP_FAIL;
}

static esp_err_t write_atomic(
    const char *name,
    const void *data,
    size_t length)
{
    char path[CAPTURE_PATH_MAX];
    char temporary[CAPTURE_PATH_MAX];
    if (snprintf(path, sizeof(path), "%s/%s", s_session_path, name) >=
            (int)sizeof(path) ||
        snprintf(temporary, sizeof(temporary), "%s.tmp", path) >=
            (int)sizeof(temporary)) {
        return ESP_ERR_INVALID_SIZE;
    }

    FILE *file = fopen(temporary, "wb");
    if (file == NULL) {
        return ESP_FAIL;
    }

    bool success = fwrite(data, 1U, length, file) == length &&
        fflush(file) == 0 && fsync(fileno(file)) == 0;
    success = fclose(file) == 0 && success;
    if (!success) {
        (void)remove(temporary);
        return ESP_FAIL;
    }

    if (rename(temporary, path) != 0) {
        (void)remove(temporary);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t append_manifest(const char *line)
{
    char path[CAPTURE_PATH_MAX];
    if (snprintf(path, sizeof(path), "%s/manifest.txt", s_session_path) >=
        (int)sizeof(path)) {
        return ESP_ERR_INVALID_SIZE;
    }

    FILE *file = fopen(path, "ab");
    if (file == NULL) {
        return ESP_FAIL;
    }
    const size_t length = strlen(line);
    const bool success = fwrite(line, 1U, length, file) == length &&
        fflush(file) == 0;
    fclose(file);
    return success ? ESP_OK : ESP_FAIL;
}

esp_err_t g815_capture_init(void)
{
    if (s_lock != NULL) {
        return ESP_OK;
    }
    s_lock = xSemaphoreCreateMutex();
    return s_lock != NULL ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t g815_capture_start(const g815_capture_device_info_t *device)
{
    if (device == NULL || !sd_storage_is_mounted()) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t error = g815_capture_init();
    if (error != ESP_OK) {
        return error;
    }
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(100U)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    if (s_active) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_STATE;
    }

    char root[CAPTURE_PATH_MAX];
    (void)snprintf(root, sizeof(root), "/sdcard/captures");
    error = make_directory(root);
    if (error == ESP_OK) {
        const unsigned long long now =
            (unsigned long long)esp_timer_get_time();
        if (snprintf(
                s_session_path,
                sizeof(s_session_path),
                "%s/g815-%04x-%04x-%llu",
                root,
                device->vendor_id,
                device->product_id,
                now) >= (int)sizeof(s_session_path)) {
            error = ESP_ERR_INVALID_SIZE;
        }
    }
    if (error == ESP_OK) {
        error = make_directory(s_session_path);
    }

    char event_path[CAPTURE_PATH_MAX];
    if (error == ESP_OK &&
        snprintf(event_path, sizeof(event_path), "%s/events.bin", s_session_path) <
            (int)sizeof(event_path)) {
        s_events = fopen(event_path, "wb");
        if (s_events == NULL) {
            error = ESP_FAIL;
        }
    }

    if (error == ESP_OK) {
        s_sequence = 0U;
        s_dropped = 0U;
        s_active = true;
        char line[192];
        (void)snprintf(
            line,
            sizeof(line),
            "format=%u\nvid=0x%04X\npid=0x%04X\nbcdDevice=0x%04X\naddress=%u\nconfiguration=%u\ninterfaces=%u\n",
            CAPTURE_VERSION,
            device->vendor_id,
            device->product_id,
            device->bcd_device,
            device->address,
            device->configuration_value,
            device->interface_count);
        error = append_manifest(line);
    }

    if (error != ESP_OK) {
        if (s_events != NULL) {
            fclose(s_events);
            s_events = NULL;
        }
        s_active = false;
    }
    xSemaphoreGive(s_lock);

    if (error == ESP_OK) {
        ESP_LOGI(TAG, "Capture started: %s", s_session_path);
        (void)g815_capture_event(
            G815_CAPTURE_EVENT_ATTACH,
            0xFFU,
            0U,
            0U,
            device,
            sizeof(*device),
            ESP_OK);
    }
    return error;
}

esp_err_t g815_capture_stop(void)
{
    if (s_lock == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(100U)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    if (!s_active) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_STATE;
    }

    (void)fflush(s_events);
    (void)fsync(fileno(s_events));
    (void)fclose(s_events);
    s_events = NULL;
    s_active = false;

    char line[96];
    (void)snprintf(
        line,
        sizeof(line),
        "records=%lu\ndropped=%lu\n",
        (unsigned long)s_sequence,
        (unsigned long)s_dropped);
    (void)append_manifest(line);
    xSemaphoreGive(s_lock);
    ESP_LOGI(TAG, "Capture stopped: %s", s_session_path);
    return ESP_OK;
}

bool g815_capture_is_active(void)
{
    return s_active;
}

static esp_err_t write_descriptor(
    const char *name,
    const uint8_t *descriptor,
    size_t length)
{
    if (!s_active || descriptor == NULL || length == 0U) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(100U)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    const esp_err_t error = write_atomic(name, descriptor, length);
    xSemaphoreGive(s_lock);
    return error;
}

esp_err_t g815_capture_write_device_descriptor(
    const uint8_t *descriptor,
    size_t length)
{
    return write_descriptor("device_descriptor.bin", descriptor, length);
}

esp_err_t g815_capture_write_configuration_descriptor(
    const uint8_t *descriptor,
    size_t length)
{
    return write_descriptor("configuration_descriptor.bin", descriptor, length);
}

esp_err_t g815_capture_write_report_descriptor(
    uint8_t interface_number,
    const uint8_t *descriptor,
    size_t length)
{
    char name[48];
    (void)snprintf(
        name,
        sizeof(name),
        "hid_report_interface_%02u.bin",
        interface_number);
    return write_descriptor(name, descriptor, length);
}

esp_err_t g815_capture_write_string_descriptor(
    uint8_t string_index,
    uint16_t language_id,
    const uint8_t *descriptor,
    size_t length)
{
    char name[64];
    (void)snprintf(
        name,
        sizeof(name),
        "string_%03u_lang_%04X.bin",
        string_index,
        language_id);
    return write_descriptor(name, descriptor, length);
}

esp_err_t g815_capture_event(
    g815_capture_event_type_t type,
    uint8_t interface_number,
    uint8_t report_id,
    uint8_t endpoint,
    const void *data,
    size_t length,
    esp_err_t result)
{
    if (!s_active || s_events == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(20U)) != pdTRUE) {
        ++s_dropped;
        return ESP_ERR_TIMEOUT;
    }

    const size_t stored = length > CAPTURE_PAYLOAD_MAX
        ? CAPTURE_PAYLOAD_MAX
        : length;
    capture_record_header_t header = {
        .magic = CAPTURE_MAGIC,
        .version = CAPTURE_VERSION,
        .header_size = sizeof(capture_record_header_t),
        .timestamp_us = (uint64_t)esp_timer_get_time(),
        .sequence = s_sequence++,
        .event_type = (uint16_t)type,
        .interface_number = interface_number,
        .report_id = report_id,
        .endpoint = endpoint,
        .result = result,
        .payload_length = (uint16_t)stored,
        .original_length = (uint16_t)(length > UINT16_MAX ? UINT16_MAX : length),
    };

    bool success = fwrite(&header, 1U, sizeof(header), s_events) == sizeof(header);
    if (success && stored > 0U) {
        success = data != NULL &&
            fwrite(data, 1U, stored, s_events) == stored;
    }
    if (!success) {
        ++s_dropped;
    }
    xSemaphoreGive(s_lock);
    return success ? ESP_OK : ESP_FAIL;
}

esp_err_t g815_capture_control(
    g815_capture_event_type_t type,
    const g815_capture_setup_packet_t *setup,
    const void *data,
    size_t length,
    esp_err_t result)
{
    if (setup == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t payload[sizeof(capture_control_payload_t) + CAPTURE_PAYLOAD_MAX];
    capture_control_payload_t header = {
        .setup = *setup,
        .data_length = (uint16_t)(
            length > CAPTURE_PAYLOAD_MAX ? CAPTURE_PAYLOAD_MAX : length),
        .original_length = (uint16_t)(length > UINT16_MAX ? UINT16_MAX : length),
    };
    memcpy(payload, &header, sizeof(header));
    if (header.data_length > 0U && data != NULL) {
        memcpy(payload + sizeof(header), data, header.data_length);
    }

    return g815_capture_event(
        type,
        (uint8_t)(setup->w_index & 0xFFU),
        (uint8_t)(setup->w_value & 0xFFU),
        0U,
        payload,
        sizeof(header) + header.data_length,
        result);
}

uint32_t g815_capture_dropped(void)
{
    return s_dropped;
}

const char *g815_capture_session_path(void)
{
    return s_session_path;
}
