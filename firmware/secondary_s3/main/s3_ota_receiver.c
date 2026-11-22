#include "s3_ota_receiver.h"

#include <stdio.h>
#include <string.h>

#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define OTA_REBOOT_DELAY_MS 500U

static const char *TAG = "s3_ota";
static esp_ota_handle_t s_handle;
static const esp_partition_t *s_partition;
static bool s_active;
static uint32_t s_expected_size;
static uint32_t s_written;
static uint8_t s_expected_sha256[32];
static p4_s3_ota_phase_t s_phase = P4_S3_OTA_PHASE_IDLE;
static esp_err_t s_last_error;

static void copy_running_version(
    char output[P4_S3_OTA_VERSION_LENGTH])
{
    memset(output, 0, P4_S3_OTA_VERSION_LENGTH);

    const esp_app_desc_t *description = esp_app_get_description();
    if (description == NULL) {
        return;
    }

    const size_t source_length = strnlen(
        description->version,
        sizeof(description->version));
    const size_t copy_length = source_length <
            (P4_S3_OTA_VERSION_LENGTH - 1U)
        ? source_length
        : (P4_S3_OTA_VERSION_LENGTH - 1U);

    memcpy(output, description->version, copy_length);
    output[copy_length] = '\0';
}

void s3_ota_receiver_get_status(p4_s3_ota_status_t *status)
{
    if (status == NULL) {
        return;
    }

    memset(status, 0, sizeof(*status));
    status->status = s_last_error == ESP_OK ? 0U : 1U;
    status->phase = (uint8_t)s_phase;
    status->error_code = s_last_error;
    status->bytes_written = s_written;
    status->image_size = s_expected_size;
    status->next_offset = s_written;
    copy_running_version(status->running_version);
}

static void reboot_task(void *argument)
{
    (void)argument;
    vTaskDelay(pdMS_TO_TICKS(OTA_REBOOT_DELAY_MS));
    esp_restart();
}

static esp_err_t ota_begin(
    const uint8_t *payload,
    size_t payload_length)
{
    if (payload == NULL ||
        payload_length != sizeof(p4_s3_ota_begin_t)) {
        return ESP_ERR_INVALID_SIZE;
    }

    if (s_active) {
        return ESP_ERR_INVALID_STATE;
    }

    p4_s3_ota_begin_t begin;
    memcpy(&begin, payload, sizeof(begin));

    s_partition = esp_ota_get_next_update_partition(NULL);
    if (s_partition == NULL ||
        begin.image_size == 0U ||
        begin.image_size > s_partition->size) {
        return ESP_ERR_INVALID_SIZE;
    }

    s_phase = P4_S3_OTA_PHASE_ERASING;
    s_last_error = ESP_OK;
    s_expected_size = begin.image_size;
    s_written = 0U;
    memcpy(
        s_expected_sha256,
        begin.sha256,
        sizeof(s_expected_sha256));

    const esp_err_t error = esp_ota_begin(
        s_partition,
        begin.image_size,
        &s_handle);
    if (error != ESP_OK) {
        s_phase = P4_S3_OTA_PHASE_FAILED;
        s_last_error = error;
        return error;
    }

    s_active = true;
    s_phase = P4_S3_OTA_PHASE_WRITING;

    ESP_LOGI(
        TAG,
        "BEGIN target=%s size=%lu version='%s'",
        s_partition->label,
        (unsigned long)begin.image_size,
        begin.version);

    return ESP_OK;
}

static esp_err_t ota_data(
    const uint8_t *payload,
    size_t payload_length)
{
    if (!s_active || payload == NULL || payload_length < 8U) {
        return ESP_ERR_INVALID_STATE;
    }

    p4_s3_ota_data_t packet = { 0 };
    memcpy(&packet, payload, payload_length);

    if (packet.data_length == 0U ||
        packet.data_length > P4_S3_OTA_DATA_SIZE ||
        payload_length != 8U + packet.data_length ||
        packet.offset != s_written ||
        s_written + packet.data_length > s_expected_size) {
        return ESP_ERR_INVALID_SIZE;
    }

    if (p4_s3_crc16(packet.data, packet.data_length) !=
        packet.chunk_crc16) {
        return ESP_ERR_INVALID_CRC;
    }

    const esp_err_t error = esp_ota_write(
        s_handle,
        packet.data,
        packet.data_length);
    if (error != ESP_OK) {
        s_last_error = error;
        s_phase = P4_S3_OTA_PHASE_FAILED;
        return error;
    }

    s_written += packet.data_length;
    return ESP_OK;
}

static esp_err_t ota_end(void)
{
    if (!s_active || s_written != s_expected_size) {
        return ESP_ERR_INVALID_STATE;
    }

    s_phase = P4_S3_OTA_PHASE_VERIFYING;

    esp_err_t error = esp_ota_end(s_handle);
    s_active = false;
    if (error != ESP_OK) {
        s_last_error = error;
        s_phase = P4_S3_OTA_PHASE_FAILED;
        return error;
    }

    uint8_t actual_sha256[32];
    error = esp_partition_get_sha256(
        s_partition,
        actual_sha256);
    if (error != ESP_OK ||
        memcmp(
            actual_sha256,
            s_expected_sha256,
            sizeof(actual_sha256)) != 0) {
        s_last_error = error != ESP_OK
            ? error
            : ESP_ERR_INVALID_CRC;
        s_phase = P4_S3_OTA_PHASE_FAILED;
        return s_last_error;
    }

    error = esp_ota_set_boot_partition(s_partition);
    if (error != ESP_OK) {
        s_last_error = error;
        s_phase = P4_S3_OTA_PHASE_FAILED;
        return error;
    }

    s_last_error = ESP_OK;
    s_phase = P4_S3_OTA_PHASE_REBOOT_PENDING;

    ESP_LOGI(
        TAG,
        "VERIFY OK; boot partition=%s",
        s_partition->label);

    if (xTaskCreate(
            reboot_task,
            "s3_ota_reboot",
            2048U,
            NULL,
            20U,
            NULL) != pdPASS) {
        s_last_error = ESP_ERR_NO_MEM;
        s_phase = P4_S3_OTA_PHASE_FAILED;
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

static esp_err_t ota_abort(void)
{
    if (s_active) {
        (void)esp_ota_abort(s_handle);
    }

    s_active = false;
    s_partition = NULL;
    s_last_error = ESP_OK;
    s_phase = P4_S3_OTA_PHASE_ABORTED;
    return ESP_OK;
}

static esp_err_t ota_confirm(void)
{
    const esp_err_t error =
        esp_ota_mark_app_valid_cancel_rollback();

    if (error == ESP_OK) {
        s_last_error = ESP_OK;
        s_phase = P4_S3_OTA_PHASE_CONFIRMED;
        ESP_LOGI(TAG, "Running image confirmed");
    } else {
        s_last_error = error;
        s_phase = P4_S3_OTA_PHASE_FAILED;
    }

    return error;
}

esp_err_t s3_ota_receiver_init(void)
{
    s_active = false;
    s_expected_size = 0U;
    s_written = 0U;
    s_last_error = ESP_OK;

    const esp_partition_t *running =
        esp_ota_get_running_partition();
    esp_ota_img_states_t state;

    if (running != NULL &&
        esp_ota_get_state_partition(running, &state) == ESP_OK &&
        state == ESP_OTA_IMG_PENDING_VERIFY) {
        s_phase = P4_S3_OTA_PHASE_PENDING_CONFIRM;
        ESP_LOGW(TAG, "Running image pending P4 confirmation");
    } else {
        s_phase = P4_S3_OTA_PHASE_CONFIRMED;
    }

    return ESP_OK;
}

esp_err_t s3_ota_receiver_handle(
    uint8_t message_type,
    const uint8_t *payload,
    size_t payload_length,
    p4_s3_ota_status_t *status)
{
    esp_err_t error;

    switch (message_type) {
        case P4_S3_MSG_OTA_BEGIN:
            error = ota_begin(payload, payload_length);
            break;

        case P4_S3_MSG_OTA_DATA:
            error = ota_data(payload, payload_length);
            break;

        case P4_S3_MSG_OTA_END:
            error = ota_end();
            break;

        case P4_S3_MSG_OTA_ABORT:
            error = ota_abort();
            break;

        case P4_S3_MSG_OTA_STATUS:
            error = ESP_OK;
            break;

        case P4_S3_MSG_OTA_CONFIRM:
            error = ota_confirm();
            break;

        default:
            error = ESP_ERR_NOT_SUPPORTED;
            break;
    }

    if (error != ESP_OK) {
        s_last_error = error;

        if (message_type != P4_S3_MSG_OTA_STATUS) {
            ESP_LOGE(
                TAG,
                "Command 0x%02X failed: %s",
                message_type,
                esp_err_to_name(error));
        }
    } else if (message_type == P4_S3_MSG_OTA_DATA) {
        s_last_error = ESP_OK;
    }

    s3_ota_receiver_get_status(status);
    status->status = error == ESP_OK ? 0U : 1U;
    status->error_code = error;

    return error;
}
