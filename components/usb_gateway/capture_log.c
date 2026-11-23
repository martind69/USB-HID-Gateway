#include "capture_log.h"

#include "sdkconfig.h"

#if CONFIG_USB_GATEWAY_CAPTURE_LEVEL_SD

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "sd_storage.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define CAPTURE_PAYLOAD_SIZE        64U
#define CAPTURE_MIN_FREE_BYTES      (64ULL * 1024ULL * 1024ULL)
#define CAPTURE_WRITER_STACK        4096U
#define CAPTURE_WRITER_PRIORITY     3U
#define CAPTURE_LOCK_TIMEOUT_MS     100U
#define MIB                         (1024ULL * 1024ULL)

typedef struct {
    uint64_t timestamp_us;
    uint16_t source;
    uint16_t event;
    uint16_t length;
    uint16_t flags;
    uint32_t sequence;
    uint8_t payload[CAPTURE_PAYLOAD_SIZE];
} capture_record_t;

static const char *TAG = "capture_log";

static QueueHandle_t s_queue;
static SemaphoreHandle_t s_lock;
static FILE *s_file;
static volatile bool s_active;
static uint64_t s_stop_us;
static uint64_t s_file_bytes;
static uint64_t s_session_bytes;
static uint32_t s_file_index;
static uint32_t s_dropped;
static char s_session[96];

static esp_err_t make_directory(const char *path)
{
    return (mkdir(path, 0775) == 0 || errno == EEXIST) ? ESP_OK : ESP_FAIL;
}

static void close_file(void)
{
    if (s_file != NULL) {
        (void)fflush(s_file);
        (void)fsync(fileno(s_file));
        (void)fclose(s_file);
        s_file = NULL;
    }
}

static esp_err_t open_next_file(void)
{
    close_file();
    char path[160];
    (void)snprintf(path, sizeof(path), "%s/events-%03lu.bin",
        s_session, (unsigned long)s_file_index++);
    s_file = fopen(path, "wb");
    s_file_bytes = 0U;
    return s_file != NULL ? ESP_OK : ESP_FAIL;
}

static void stop_locked(const char *reason)
{
    if (!s_active) {
        return;
    }
    close_file();
    s_active = false;
    ESP_LOGI(TAG, "Session stopped (%s): %s, %llu bytes, dropped=%lu",
        reason, s_session, (unsigned long long)s_session_bytes,
        (unsigned long)s_dropped);
}

static void write_record_locked(const capture_record_t *record)
{
    if (!s_active || s_file == NULL) {
        return;
    }
    if (s_file_bytes + sizeof(*record) >
        (uint64_t)CONFIG_USB_GATEWAY_CAPTURE_SD_FILE_MIB * MIB) {
        if (open_next_file() != ESP_OK) {
            ++s_dropped;
            stop_locked("file error");
            return;
        }
    }
    if (fwrite(record, 1U, sizeof(*record), s_file) == sizeof(*record)) {
        s_file_bytes += sizeof(*record);
        s_session_bytes += sizeof(*record);
    } else {
        ++s_dropped;
    }
    if (s_session_bytes >=
        (uint64_t)CONFIG_USB_GATEWAY_CAPTURE_SD_MAX_TOTAL_MIB * MIB) {
        stop_locked("size budget");
    }
}

static void writer_task(void *argument)
{
    (void)argument;
    capture_record_t record;
    TickType_t last_flush = xTaskGetTickCount();
    while (true) {
        const bool received =
            xQueueReceive(s_queue, &record, pdMS_TO_TICKS(100U)) == pdPASS;
        if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(CAPTURE_LOCK_TIMEOUT_MS)) != pdTRUE) {
            if (received) {
                ++s_dropped;
            }
            continue;
        }
        if (received) {
            write_record_locked(&record);
        }
        if (s_active && s_stop_us != 0U &&
            (uint64_t)esp_timer_get_time() >= s_stop_us) {
            stop_locked("duration");
        }
        if (s_active && s_file != NULL &&
            (xTaskGetTickCount() - last_flush) >=
                pdMS_TO_TICKS(CONFIG_USB_GATEWAY_CAPTURE_SD_FLUSH_MS)) {
            (void)fflush(s_file);
            last_flush = xTaskGetTickCount();
        }
        xSemaphoreGive(s_lock);
    }
}

esp_err_t capture_log_init(void)
{
    if (s_queue != NULL) {
        return ESP_OK;
    }
    s_queue = xQueueCreate(
        CONFIG_USB_GATEWAY_CAPTURE_SD_QUEUE_DEPTH,
        sizeof(capture_record_t));
    s_lock = xSemaphoreCreateMutex();
    if (s_queue == NULL || s_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(writer_task, "capture_log", CAPTURE_WRITER_STACK,
            NULL, CAPTURE_WRITER_PRIORITY, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    if (!sd_storage_is_mounted()) {
        ESP_LOGW(TAG, "No SD card, event log not started");
        return ESP_OK;
    }
    return capture_log_start(CONFIG_USB_GATEWAY_CAPTURE_SD_DURATION_S);
}

esp_err_t capture_log_start(uint32_t duration_s)
{
    if (s_lock == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!sd_storage_is_mounted()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (sd_storage_free_bytes() < CAPTURE_MIN_FREE_BYTES) {
        ESP_LOGE(TAG, "Less than 64 MiB free on SD card, event log not started");
        return ESP_ERR_NO_MEM;
    }
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(CAPTURE_LOCK_TIMEOUT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    if (s_active) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t error = make_directory(CAPTURE_LOG_ROOT);
    if (error == ESP_OK) {
        (void)snprintf(s_session, sizeof(s_session), "%s/session-%llu",
            CAPTURE_LOG_ROOT, (unsigned long long)esp_timer_get_time());
        error = make_directory(s_session);
    }
    if (error == ESP_OK) {
        s_file_index = 0U;
        s_session_bytes = 0U;
        s_dropped = 0U;
        s_stop_us = duration_s != 0U
            ? (uint64_t)esp_timer_get_time() + (uint64_t)duration_s * 1000000ULL
            : 0U;
        error = open_next_file();
    }
    s_active = error == ESP_OK;
    xSemaphoreGive(s_lock);
    if (error == ESP_OK) {
        ESP_LOGI(TAG, "Session started: %s, duration=%lus", s_session,
            (unsigned long)duration_s);
    } else {
        ESP_LOGE(TAG, "Session start failed: errno %d", errno);
    }
    return error;
}

esp_err_t capture_log_stop(void)
{
    if (s_lock == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(CAPTURE_LOCK_TIMEOUT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    stop_locked("request");
    xSemaphoreGive(s_lock);
    return ESP_OK;
}

bool capture_log_write(
    uint16_t source,
    uint16_t event,
    uint32_t sequence,
    const void *payload,
    size_t length)
{
    if (!s_active || s_queue == NULL) {
        return false;
    }
    capture_record_t record = {
        .timestamp_us = (uint64_t)esp_timer_get_time(),
        .source = source,
        .event = event,
        .sequence = sequence,
        .length = (uint16_t)(length > CAPTURE_PAYLOAD_SIZE ? CAPTURE_PAYLOAD_SIZE : length),
    };
    if (payload != NULL && record.length > 0U) {
        memcpy(record.payload, payload, record.length);
    }
    if (xQueueSend(s_queue, &record, 0U) != pdPASS) {
        ++s_dropped;
        return false;
    }
    return true;
}

uint32_t capture_log_dropped(void)
{
    return s_dropped;
}

#else /* !CONFIG_USB_GATEWAY_CAPTURE_LEVEL_SD */

esp_err_t capture_log_init(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t capture_log_start(uint32_t duration_s)
{
    (void)duration_s;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t capture_log_stop(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

bool capture_log_write(
    uint16_t source,
    uint16_t event,
    uint32_t sequence,
    const void *payload,
    size_t length)
{
    (void)source;
    (void)event;
    (void)sequence;
    (void)payload;
    (void)length;
    return false;
}

uint32_t capture_log_dropped(void)
{
    return 0U;
}

#endif /* CONFIG_USB_GATEWAY_CAPTURE_LEVEL_SD */
