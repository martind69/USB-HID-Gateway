#include "s3_ota_manager.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "p4_s3_link.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/md.h"
#include "sdkconfig.h"

#define OTA_PROGRESS_STEP_BYTES (64U * 1024U)
#define OTA_WAIT_FOREVER UINT32_MAX
#define OTA_DATA_RETRY_COUNT 3U
#define OTA_DATA_RETRY_DELAY_MS 20U
#define OTA_STATUS_POLL_MS 500U
#define OTA_DISPLAY_MAX_INTERVAL_US 1000000LL

static const char *TAG = "s3_ota";
static TaskHandle_t s_task;
static char s_image_path[256];
static s3_ota_progress_callback_t s_progress_callback;
static uint8_t s_last_display_percent = 0xFFU;
static p4_s3_ota_phase_t s_last_display_phase = 0xFF;
static int64_t s_last_display_us;

static bool file_exists(const char *path)
{
    struct stat status;
    return path != NULL && stat(path, &status) == 0 && S_ISREG(status.st_mode);
}

static void notify_progress(p4_s3_ota_phase_t phase, uint32_t bytes_written,
    uint32_t image_size, esp_err_t error, const char *version, bool force)
{
    if (s_progress_callback == NULL) {
        return;
    }
    const uint8_t percent = image_size > 0U
        ? (uint8_t)(((uint64_t)bytes_written * 100ULL) / image_size)
        : 0U;
    const int64_t now = esp_timer_get_time();
    const bool changed = phase != s_last_display_phase ||
        percent != s_last_display_percent;
    const bool interval_elapsed =
        now - s_last_display_us >= OTA_DISPLAY_MAX_INTERVAL_US;
    if (!force && !changed && !interval_elapsed) {
        return;
    }
    s3_ota_progress_t progress = {
        .phase = phase,
        .bytes_written = bytes_written,
        .image_size = image_size,
        .progress_percent = percent > 100U ? 100U : percent,
        .error = error,
    };
    if (version != NULL) {
        (void)snprintf(progress.version, sizeof(progress.version), "%.*s",
            (int)sizeof(progress.version) - 1, version);
    }
    s_last_display_phase = phase;
    s_last_display_percent = progress.progress_percent;
    s_last_display_us = now;
    s_progress_callback(&progress);
}

static esp_err_t file_sha256(FILE *file, uint8_t digest[32], uint32_t *image_size)
{
    if (file == NULL || digest == NULL || image_size == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    const mbedtls_md_info_t *info =
        mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (info == NULL) {
        return ESP_FAIL;
    }
    mbedtls_md_context_t context;
    mbedtls_md_init(&context);
    if (mbedtls_md_setup(&context, info, 0) != 0 ||
        mbedtls_md_starts(&context) != 0) {
        mbedtls_md_free(&context);
        return ESP_FAIL;
    }
    uint8_t buffer[4096];
    uint64_t total = 0U;
    rewind(file);
    while (!feof(file)) {
        const size_t count = fread(buffer, 1U, sizeof(buffer), file);
        if (count > 0U) {
            if (mbedtls_md_update(&context, buffer, count) != 0) {
                mbedtls_md_free(&context);
                return ESP_FAIL;
            }
            total += count;
        }
        if (ferror(file)) {
            mbedtls_md_free(&context);
            return ESP_FAIL;
        }
    }
    if (total == 0U || total > UINT32_MAX ||
        mbedtls_md_finish(&context, digest) != 0) {
        mbedtls_md_free(&context);
        return ESP_ERR_INVALID_SIZE;
    }
    mbedtls_md_free(&context);
    *image_size = (uint32_t)total;
    rewind(file);
    return ESP_OK;
}

static void read_version(char output[P4_S3_OTA_VERSION_LENGTH])
{
    memset(output, 0, P4_S3_OTA_VERSION_LENGTH);
    FILE *file = fopen(CONFIG_USB_GATEWAY_S3_OTA_VERSION_PATH, "rb");
    if (file == NULL) {
        (void)snprintf(output, P4_S3_OTA_VERSION_LENGTH, "spi-ota");
        return;
    }
    (void)fgets(output, P4_S3_OTA_VERSION_LENGTH, file);
    fclose(file);
    output[strcspn(output, "\r\n")] = '\0';
}

static bool decode_status(const p4_s3_ack_t *acknowledgement,
    p4_s3_ota_status_t *status)
{
    if (acknowledgement == NULL || status == NULL ||
        acknowledgement->payload_length < sizeof(*status)) {
        return false;
    }
    memcpy(status, acknowledgement->payload, sizeof(*status));
    return true;
}

static esp_err_t command(uint8_t type, const void *payload, size_t payload_length,
    p4_s3_ota_status_t *status, uint32_t timeout_ms)
{
    p4_s3_ack_t acknowledgement = { 0 };
    const esp_err_t error = p4_s3_link_command(type, payload, payload_length,
        &acknowledgement, timeout_ms);
    if (!decode_status(&acknowledgement, status)) {
        return error == ESP_OK ? ESP_ERR_INVALID_RESPONSE : error;
    }
    if (error != ESP_OK || status->status != 0U) {
        ESP_LOGE(TAG,
            "S3 rejected command=0x%02X phase=%u status=%u error=%ld offset=%lu",
            type, status->phase, status->status, (long)status->error_code,
            (unsigned long)status->next_offset);
        return error != ESP_OK ? error : ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t send_data_with_retry(
    const p4_s3_ota_data_t *packet,
    size_t payload_length,
    p4_s3_ota_status_t *status)
{
    esp_err_t last_error = ESP_FAIL;
    for (uint32_t attempt = 1U; attempt <= OTA_DATA_RETRY_COUNT; ++attempt) {
        last_error = command(P4_S3_MSG_OTA_DATA, packet, payload_length,
            status, OTA_WAIT_FOREVER);
        if (last_error == ESP_OK) {
            return ESP_OK;
        }

        p4_s3_ota_status_t current = { 0 };
        const esp_err_t status_error = command(P4_S3_MSG_OTA_STATUS, NULL, 0U,
            &current, OTA_WAIT_FOREVER);
        if (status_error == ESP_OK) {
            *status = current;
            const uint32_t packet_end = packet->offset + packet->data_length;
            if (current.next_offset == packet_end) {
                ESP_LOGW(TAG,
                    "DATA ACK lost but chunk committed: offset=%lu next=%lu",
                    (unsigned long)packet->offset,
                    (unsigned long)current.next_offset);
                return ESP_OK;
            }
            if (current.next_offset != packet->offset) {
                ESP_LOGE(TAG,
                    "DATA offset diverged: sent=%lu length=%u S3-next=%lu",
                    (unsigned long)packet->offset,
                    packet->data_length,
                    (unsigned long)current.next_offset);
                return ESP_ERR_INVALID_STATE;
            }
        }
        ESP_LOGW(TAG, "DATA retry %lu/%u offset=%lu error=%s",
            (unsigned long)attempt, OTA_DATA_RETRY_COUNT,
            (unsigned long)packet->offset, esp_err_to_name(last_error));
        vTaskDelay(pdMS_TO_TICKS(OTA_DATA_RETRY_DELAY_MS));
    }
    return last_error;
}

static void log_progress(uint32_t complete, uint32_t total, int64_t started_us)
{
    const int64_t elapsed_us = esp_timer_get_time() - started_us;
    const uint32_t rate = elapsed_us > 0
        ? (uint32_t)(((uint64_t)complete * 1000000ULL) / (uint64_t)elapsed_us)
        : 0U;
    const uint32_t remaining = total > complete ? total - complete : 0U;
    const uint32_t eta = rate > 0U ? remaining / rate : 0U;
    const uint32_t permille = total > 0U
        ? (uint32_t)(((uint64_t)complete * 1000ULL) / total)
        : 0U;
    ESP_LOGI(TAG, "WRITE %lu/%lu bytes %lu.%lu%% rate=%lu B/s eta=%lu s",
        (unsigned long)complete, (unsigned long)total,
        (unsigned long)(permille / 10U), (unsigned long)(permille % 10U),
        (unsigned long)rate, (unsigned long)eta);
}

static void finalize_marker(void)
{
    if (!file_exists(CONFIG_USB_GATEWAY_S3_OTA_TRIGGER_PATH)) {
        return;
    }
    (void)remove(CONFIG_USB_GATEWAY_S3_OTA_DONE_PATH);
    if (rename(CONFIG_USB_GATEWAY_S3_OTA_TRIGGER_PATH,
            CONFIG_USB_GATEWAY_S3_OTA_DONE_PATH) != 0) {
        ESP_LOGW(TAG, "Cannot rename OTA trigger: errno=%d", errno);
    }
}

static esp_err_t wait_for_new_image(
    const char expected_version[P4_S3_OTA_VERSION_LENGTH], uint32_t image_size)
{
    p4_s3_ota_status_t status = { 0 };
    notify_progress(P4_S3_OTA_PHASE_REBOOT_PENDING, image_size, image_size,
        ESP_OK, expected_version, true);
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(OTA_STATUS_POLL_MS));
        const esp_err_t error = command(P4_S3_MSG_OTA_STATUS, NULL, 0U,
            &status, OTA_WAIT_FOREVER);
        if (error != ESP_OK) {
            continue;
        }
        ESP_LOGI(TAG, "S3 online version='%s' phase=%u written=%lu/%lu",
            status.running_version, status.phase,
            (unsigned long)status.bytes_written,
            (unsigned long)status.image_size);
        if (status.phase != P4_S3_OTA_PHASE_PENDING_CONFIRM &&
            status.phase != P4_S3_OTA_PHASE_CONFIRMED) {
            continue;
        }
        notify_progress((p4_s3_ota_phase_t)status.phase, image_size, image_size,
            ESP_OK, status.running_version, true);
        if (status.phase == P4_S3_OTA_PHASE_CONFIRMED) {
            return ESP_OK;
        }
        if (status.phase == P4_S3_OTA_PHASE_PENDING_CONFIRM) {
            ESP_LOGI(TAG, "S3 pending confirmation; trigger remains unchanged");
            const esp_err_t confirm_error = command(
                P4_S3_MSG_OTA_CONFIRM, NULL, 0U, &status, OTA_WAIT_FOREVER);
            if (confirm_error != ESP_OK) {
                return confirm_error;
            }
            continue;
        }
    }
}

static esp_err_t perform_update(const char *path)
{
    notify_progress(P4_S3_OTA_PHASE_IDLE, 0U, 0U, ESP_OK, NULL, true);
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return ESP_ERR_NOT_FOUND;
    }
    p4_s3_ota_begin_t begin = { 0 };
    uint32_t image_size = 0U;
    esp_err_t error = file_sha256(file, begin.sha256, &image_size);
    if (error != ESP_OK) {
        fclose(file);
        return error;
    }
    begin.image_size = image_size;
    read_version(begin.version);
    error = p4_s3_link_begin_exclusive();
    if (error != ESP_OK) {
        fclose(file);
        return error;
    }

    p4_s3_ota_status_t status = { 0 };
    notify_progress(P4_S3_OTA_PHASE_ABORTED, 0U, image_size, ESP_OK,
        begin.version, true);
    (void)command(P4_S3_MSG_OTA_ABORT, NULL, 0U, &status,
        OTA_WAIT_FOREVER);
    notify_progress(P4_S3_OTA_PHASE_ERASING, 0U, image_size, ESP_OK,
        begin.version, true);
    error = command(P4_S3_MSG_OTA_BEGIN, &begin, sizeof(begin), &status,
        OTA_WAIT_FOREVER);
    if (error != ESP_OK) {
        goto cleanup;
    }

    const int64_t started_us = esp_timer_get_time();
    uint32_t offset = 0U;
    uint32_t next_log = OTA_PROGRESS_STEP_BYTES;
    while (offset < image_size) {
        p4_s3_ota_data_t packet = { .offset = offset };
        const size_t count = fread(packet.data, 1U, sizeof(packet.data), file);
        if (count == 0U) {
            error = ESP_FAIL;
            goto abort_update;
        }
        packet.data_length = (uint8_t)count;
        packet.chunk_crc16 = p4_s3_crc16(packet.data, count);
        error = send_data_with_retry(&packet, 8U + count, &status);
        if (error != ESP_OK) {
            goto abort_update;
        }
        offset += (uint32_t)count;
        notify_progress(P4_S3_OTA_PHASE_WRITING, offset, image_size, ESP_OK,
            begin.version, false);
        if (offset >= next_log || offset == image_size) {
            log_progress(offset, image_size, started_us);
            next_log += OTA_PROGRESS_STEP_BYTES;
        }
    }

    notify_progress(P4_S3_OTA_PHASE_VERIFYING, image_size, image_size, ESP_OK,
        begin.version, true);
    error = command(P4_S3_MSG_OTA_END, NULL, 0U, &status,
        OTA_WAIT_FOREVER);
    if (error != ESP_OK) {
        goto abort_update;
    }
    error = wait_for_new_image(begin.version, image_size);
    goto cleanup;

abort_update:
    (void)command(P4_S3_MSG_OTA_ABORT, NULL, 0U, &status,
        OTA_WAIT_FOREVER);
cleanup:
    p4_s3_link_end_exclusive();
    fclose(file);
    return error;
}

static esp_err_t recover_completed_update(void)
{
    p4_s3_ota_status_t status = { 0 };
    esp_err_t error = command(
        P4_S3_MSG_OTA_STATUS, NULL, 0U, &status, OTA_WAIT_FOREVER);
    if (error != ESP_OK) {
        return error;
    }

    if (status.phase == P4_S3_OTA_PHASE_PENDING_CONFIRM) {
        ESP_LOGI(TAG, "Recovered PENDING_CONFIRM; trigger remains unchanged");
        error = command(
            P4_S3_MSG_OTA_CONFIRM, NULL, 0U, &status, OTA_WAIT_FOREVER);
        if (error != ESP_OK) {
            return error;
        }
        do {
            vTaskDelay(pdMS_TO_TICKS(OTA_STATUS_POLL_MS));
            error = command(
                P4_S3_MSG_OTA_STATUS, NULL, 0U, &status, OTA_WAIT_FOREVER);
            if (error != ESP_OK) {
                return error;
            }
        } while (status.phase != P4_S3_OTA_PHASE_CONFIRMED);
        finalize_marker();
        return ESP_OK;
    }

    if (status.phase == P4_S3_OTA_PHASE_CONFIRMED) {
        finalize_marker();
        return ESP_OK;
    }
    return ESP_ERR_NOT_FOUND;
}

static void update_task(void *argument)
{
    (void)argument;
    esp_err_t error = recover_completed_update();
    if (error == ESP_ERR_NOT_FOUND) {
        error = perform_update(s_image_path);
    }
    if (error == ESP_OK) {
        ESP_LOGI(TAG, "SPI OTA completed successfully");
        notify_progress(P4_S3_OTA_PHASE_CONFIRMED, 1U, 1U, ESP_OK, NULL, true);
        finalize_marker();
    } else {
        ESP_LOGE(TAG, "SPI OTA failed: %s", esp_err_to_name(error));
        notify_progress(P4_S3_OTA_PHASE_FAILED, 0U, 0U, error, NULL, true);
    }
    s_task = NULL;
    vTaskDelete(NULL);
}

esp_err_t s3_ota_manager_init(void)
{
    s_task = NULL;
    s_progress_callback = NULL;
    s_last_display_percent = 0xFFU;
    s_last_display_phase = 0xFF;
    s_last_display_us = 0;
    return ESP_OK;
}

void s3_ota_manager_set_progress_callback(s3_ota_progress_callback_t callback)
{
    s_progress_callback = callback;
}

esp_err_t s3_ota_manager_start(const char *image_path)
{
    if (image_path == NULL || !file_exists(image_path)) {
        return ESP_ERR_NOT_FOUND;
    }
    if (s_task != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (snprintf(s_image_path, sizeof(s_image_path), "%s", image_path) >=
        (int)sizeof(s_image_path)) {
        return ESP_ERR_INVALID_SIZE;
    }
    return xTaskCreate(update_task, "s3_spi_ota", 8192U, NULL, 8U, &s_task)
        == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t s3_ota_manager_start_if_requested(void)
{
#if CONFIG_USB_GATEWAY_S3_OTA_ENABLE
    if (!file_exists(CONFIG_USB_GATEWAY_S3_OTA_TRIGGER_PATH)) {
        return ESP_ERR_NOT_FOUND;
    }
    return s3_ota_manager_start(CONFIG_USB_GATEWAY_S3_OTA_IMAGE_PATH);
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

bool s3_ota_manager_running(void)
{
    return s_task != NULL;
}
