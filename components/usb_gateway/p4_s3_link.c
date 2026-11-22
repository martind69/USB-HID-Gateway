#include "p4_s3_link.h"
#include <string.h>
#include "gateway_test_metrics.h"
#include "shared_spi_bus.h"
#include "usb_proxy_control.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"

typedef struct {
    p4_s3_frame_t frame;
} queued_frame_t;

#define LINK_NORMAL_RESPONSE_TIMEOUT_US 100000LL
#define LINK_OTA_POLL_DELAY_MS 2U

static const char *TAG = "p4_s3_link";
static QueueHandle_t s_tx_queue;
static QueueHandle_t s_ack_queue;
static SemaphoreHandle_t s_command_lock;
static SemaphoreHandle_t s_exclusive_lock;
static spi_device_handle_t s_device;
static uint8_t s_sequence;
static uint32_t s_dropped;
static volatile bool s_exclusive;

static bool is_ota_type(uint8_t type)
{
    return type >= P4_S3_MSG_OTA_BEGIN && type <= P4_S3_MSG_OTA_CONFIRM;
}

static bool command_uses_unlimited_wait(uint8_t type)
{
    return is_ota_type(type);
}

static esp_err_t transfer(
    const p4_s3_frame_t *transmit_frame,
    p4_s3_frame_t *receive_frame)
{
    spi_transaction_t transaction = {
        .length = P4_S3_FRAME_SIZE * 8U,
        .tx_buffer = transmit_frame->raw,
        .rx_buffer = receive_frame->raw,
    };
    return spi_device_transmit(s_device, &transaction);
}

static void publish_acknowledgement(const p4_s3_ack_t *acknowledgement)
{
    if (xQueueSend(s_ack_queue, acknowledgement, 0U) == pdPASS) {
        return;
    }
    p4_s3_ack_t discarded;
    (void)xQueueReceive(s_ack_queue, &discarded, 0U);
    (void)xQueueSend(s_ack_queue, acknowledgement, 0U);
}

static void handle_inbound(
    const p4_s3_frame_t *frame,
    uint8_t expected_sequence,
    uint64_t started_us,
    bool *acknowledged)
{
    if (frame == NULL || acknowledged == NULL) {
        return;
    }
    if (frame->raw[0] == 0U && frame->raw[1] == 0U) {
        return;
    }
    if (!p4_s3_frame_validate(frame)) {
        gateway_link_metrics_on_crc_error();
        return;
    }
    if (frame->raw[3] == P4_S3_MSG_NOP) {
        return;
    }
    if (frame->raw[3] == P4_S3_MSG_STATUS) {
        if (frame->raw[4] != expected_sequence) {
            gateway_link_metrics_on_sequence_error();
            return;
        }
        p4_s3_ack_t acknowledgement = {
            .sequence = frame->raw[4],
            .status = frame->raw[5] > 0U ? frame->raw[6] : 0xFFU,
            .round_trip_us = (uint32_t)(esp_timer_get_time() - started_us),
            .payload_length = frame->raw[5],
        };
        if (acknowledgement.payload_length > P4_S3_PAYLOAD_SIZE) {
            acknowledgement.payload_length = P4_S3_PAYLOAD_SIZE;
        }
        memcpy(
            acknowledgement.payload,
            frame->raw + 6U,
            acknowledgement.payload_length);
        gateway_link_metrics_on_rx(acknowledgement.round_trip_us);
        publish_acknowledgement(&acknowledgement);
        *acknowledged = true;
        return;
    }
    const esp_err_t error = usb_proxy_control_from_s3(
        frame->raw[3],
        frame->raw + 6U,
        frame->raw[5]);
    if (error != ESP_OK && error != ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGW(TAG, "Inbound type 0x%02X failed: %s",
            frame->raw[3], esp_err_to_name(error));
    }
}

esp_err_t p4_s3_link_init(void)
{
    if (s_tx_queue != NULL || s_ack_queue != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    s_tx_queue = xQueueCreate(32U, sizeof(queued_frame_t));
    s_ack_queue = xQueueCreate(8U, sizeof(p4_s3_ack_t));
    s_command_lock = xSemaphoreCreateMutex();
    s_exclusive_lock = xSemaphoreCreateMutex();
    if (s_tx_queue == NULL || s_ack_queue == NULL ||
        s_command_lock == NULL || s_exclusive_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }
    return shared_spi_bus_add_s3(&s_device);
}

bool p4_s3_link_enqueue(uint8_t type, const uint8_t *payload, size_t length)
{
    if (s_tx_queue == NULL || length > P4_S3_PAYLOAD_SIZE) {
        ++s_dropped;
        return false;
    }
    if (s_exclusive && !is_ota_type(type)) {
        ++s_dropped;
        return false;
    }
    queued_frame_t queued = { 0 };
    p4_s3_frame_build(&queued.frame, type, s_sequence++, payload, (uint8_t)length);
    if (xQueueSend(s_tx_queue, &queued, 0U) != pdPASS) {
        ++s_dropped;
        gateway_link_metrics_on_queue_overflow();
        return false;
    }
    return true;
}

static void synchronize_link(const p4_s3_frame_t *nop_frame)
{
    for (unsigned attempt = 0U; attempt < 3U; ++attempt) {
        p4_s3_frame_t received = { 0 };
        (void)transfer(nop_frame, &received);
        esp_rom_delay_us(CONFIG_USB_GATEWAY_S3_REARM_GUARD_US);
    }
    p4_s3_ack_t stale;
    while (xQueueReceive(s_ack_queue, &stale, 0U) == pdPASS) {
    }
    ESP_LOGI(TAG, "P4/S3 startup synchronization complete");
}

static void link_task(void *argument)
{
    (void)argument;
    queued_frame_t queued;
    p4_s3_frame_t nop_frame;
    p4_s3_frame_build(&nop_frame, P4_S3_MSG_NOP, 0U, NULL, 0U);
    synchronize_link(&nop_frame);

    while (true) {
        if (xQueueReceive(s_tx_queue, &queued, portMAX_DELAY) != pdPASS) {
            continue;
        }
        p4_s3_frame_t received = { 0 };
        const uint64_t started_us = (uint64_t)esp_timer_get_time();
        esp_err_t error = transfer(&queued.frame, &received);
        if (error != ESP_OK) {
            gateway_link_metrics_on_timeout();
            continue;
        }
        gateway_link_metrics_on_tx();
        bool acknowledged = false;
        handle_inbound(&received, queued.frame.raw[4], started_us, &acknowledged);

        const uint8_t message_type = queued.frame.raw[3];
        const bool unlimited_wait = command_uses_unlimited_wait(message_type);
        const int64_t normal_deadline_us = esp_timer_get_time() +
            LINK_NORMAL_RESPONSE_TIMEOUT_US;

        while (!acknowledged &&
            (unlimited_wait || esp_timer_get_time() < normal_deadline_us)) {
            memset(&received, 0, sizeof(received));
            vTaskDelay(pdMS_TO_TICKS(
                unlimited_wait ? LINK_OTA_POLL_DELAY_MS
                               : CONFIG_USB_GATEWAY_S3_STATUS_POLL_MS));
            error = transfer(&nop_frame, &received);
            if (error == ESP_OK) {
                handle_inbound(
                    &received,
                    queued.frame.raw[4],
                    started_us,
                    &acknowledged);
            }
        }
        if (!acknowledged) {
            gateway_link_metrics_on_timeout();
            ESP_LOGE(TAG, "Response timeout type=0x%02X sequence=%u",
                message_type, queued.frame.raw[4]);
        }
        esp_rom_delay_us(CONFIG_USB_GATEWAY_S3_REARM_GUARD_US);
    }
}

void p4_s3_link_start(void)
{
    if (xTaskCreate(link_task, "p4_s3_link", 4096U, NULL, 18U, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Cannot create P4/S3 link task");
    }
}

bool p4_s3_link_wait_status(p4_s3_ack_t *acknowledgement, uint32_t timeout_ms)
{
    return s_ack_queue != NULL && acknowledgement != NULL &&
        xQueueReceive(s_ack_queue, acknowledgement,
            pdMS_TO_TICKS(timeout_ms)) == pdPASS;
}

esp_err_t p4_s3_link_command(
    uint8_t type,
    const uint8_t *payload,
    size_t length,
    p4_s3_ack_t *acknowledgement,
    uint32_t timeout_ms)
{
    if (acknowledgement == NULL || s_command_lock == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    const TickType_t wait_ticks = timeout_ms == UINT32_MAX
        ? portMAX_DELAY
        : pdMS_TO_TICKS(timeout_ms);
    if (xSemaphoreTake(s_command_lock, wait_ticks) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    p4_s3_ack_t stale;
    while (xQueueReceive(s_ack_queue, &stale, 0U) == pdPASS) {
    }

    esp_err_t error = ESP_OK;
    if (!p4_s3_link_enqueue(type, payload, length)) {
        error = ESP_ERR_NO_MEM;
    } else if (xQueueReceive(s_ack_queue, acknowledgement, wait_ticks) != pdPASS) {
        error = ESP_ERR_TIMEOUT;
    } else if (acknowledgement->status != 0U) {
        error = ESP_FAIL;
    }
    xSemaphoreGive(s_command_lock);
    return error;
}

esp_err_t p4_s3_link_begin_exclusive(void)
{
    if (s_exclusive_lock == NULL ||
        xSemaphoreTake(s_exclusive_lock, pdMS_TO_TICKS(1000U)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    s_exclusive = true;
    xQueueReset(s_tx_queue);
    return ESP_OK;
}

void p4_s3_link_end_exclusive(void)
{
    s_exclusive = false;
    if (s_exclusive_lock != NULL) {
        xSemaphoreGive(s_exclusive_lock);
    }
}

uint32_t p4_s3_link_dropped(void)
{
    return s_dropped;
}
