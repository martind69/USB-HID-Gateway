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
#include "freertos/task.h"
#include "sdkconfig.h"

typedef struct {
    p4_s3_frame_t frame;
} queued_frame_t;

static const char *TAG = "p4_s3_link";
static QueueHandle_t s_tx_queue;
static QueueHandle_t s_ack_queue;
static spi_device_handle_t s_device;
static uint8_t s_sequence;
static uint32_t s_dropped;
static uint32_t s_status_count;
static p4_s3_status_payload_t s_last_telemetry;

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

static void report_s3_telemetry(const p4_s3_status_payload_t *telemetry)
{
    ++s_status_count;
    const bool state_changed =
        telemetry->flags != s_last_telemetry.flags ||
        telemetry->usb_reports_failed != s_last_telemetry.usb_reports_failed ||
        telemetry->queue_dropped != s_last_telemetry.queue_dropped ||
        telemetry->reports_rejected != s_last_telemetry.reports_rejected ||
        telemetry->output_reports_received != s_last_telemetry.output_reports_received;

    if (state_changed || (s_status_count % 1000U) == 0U) {
        ESP_LOGI(
            TAG,
            "S3 USB mounted=%u IF0=%u IF1=%u rx=%lu accepted=%lu rejected=%lu "
            "usb_sent=%lu usb_failed=%lu queue_drop=%lu queue_hwm=%lu "
            "pc_output=%lu last_if=%u last_id=0x%02X",
            (telemetry->flags & P4_S3_S3_FLAG_USB_MOUNTED) != 0U,
            (telemetry->flags & P4_S3_S3_FLAG_IF0_READY) != 0U,
            (telemetry->flags & P4_S3_S3_FLAG_IF1_READY) != 0U,
            (unsigned long)telemetry->spi_received,
            (unsigned long)telemetry->reports_accepted,
            (unsigned long)telemetry->reports_rejected,
            (unsigned long)telemetry->usb_reports_sent,
            (unsigned long)telemetry->usb_reports_failed,
            (unsigned long)telemetry->queue_dropped,
            (unsigned long)telemetry->queue_high_watermark,
            (unsigned long)telemetry->output_reports_received,
            telemetry->last_interface,
            telemetry->last_report_id);
    }
    s_last_telemetry = *telemetry;
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
            .status = frame->raw[6],
            .round_trip_us =
                (uint32_t)(esp_timer_get_time() - started_us),
        };

        if (frame->raw[5] >= sizeof(p4_s3_status_payload_t)) {
            memcpy(
                &acknowledgement.telemetry,
                frame->raw + 6U,
                sizeof(acknowledgement.telemetry));
            acknowledgement.telemetry_valid =
                acknowledgement.telemetry.telemetry_version ==
                P4_S3_STATUS_TELEMETRY_VERSION;
            if (acknowledgement.telemetry_valid) {
                report_s3_telemetry(&acknowledgement.telemetry);
            }
        }

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
        ESP_LOGW(
            TAG,
            "Inbound type 0x%02X failed: %s",
            frame->raw[3],
            esp_err_to_name(error));
    }
}

esp_err_t p4_s3_link_init(void)
{
    if (s_tx_queue != NULL || s_ack_queue != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    s_tx_queue = xQueueCreate(32U, sizeof(queued_frame_t));
    s_ack_queue = xQueueCreate(8U, sizeof(p4_s3_ack_t));
    if (s_tx_queue == NULL || s_ack_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }
    return shared_spi_bus_add_s3(&s_device);
}

bool p4_s3_link_enqueue(
    uint8_t type,
    const uint8_t *payload,
    size_t length)
{
    if (s_tx_queue == NULL || length > P4_S3_PAYLOAD_SIZE) {
        ++s_dropped;
        return false;
    }
    queued_frame_t queued = { 0 };
    p4_s3_frame_build(
        &queued.frame,
        type,
        s_sequence++,
        payload,
        (uint8_t)length);
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
        const esp_err_t error = transfer(nop_frame, &received);
        if (error != ESP_OK) {
            ESP_LOGW(
                TAG,
                "Startup synchronization transfer %u failed: %s",
                attempt + 1U,
                esp_err_to_name(error));
        }
        esp_rom_delay_us(CONFIG_USB_GATEWAY_S3_REARM_GUARD_US);
    }

    p4_s3_ack_t stale_acknowledgement;
    while (xQueueReceive(
               s_ack_queue,
               &stale_acknowledgement,
               0U) == pdPASS) {
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
            ESP_LOGW(TAG, "Command transfer failed: %s", esp_err_to_name(error));
            continue;
        }

        gateway_link_metrics_on_tx();
        bool acknowledged = false;
        handle_inbound(
            &received,
            queued.frame.raw[4],
            started_us,
            &acknowledged);

        for (unsigned poll = 0U;
             !acknowledged &&
             poll < CONFIG_USB_GATEWAY_S3_STATUS_POLL_LIMIT;
             ++poll) {
            memset(&received, 0, sizeof(received));
            vTaskDelay(pdMS_TO_TICKS(CONFIG_USB_GATEWAY_S3_STATUS_POLL_MS));
            error = transfer(&nop_frame, &received);
            if (error != ESP_OK) {
                gateway_link_metrics_on_timeout();
                break;
            }
            handle_inbound(
                &received,
                queued.frame.raw[4],
                started_us,
                &acknowledged);
        }

        if (!acknowledged) {
            gateway_link_metrics_on_timeout();
        }
        esp_rom_delay_us(CONFIG_USB_GATEWAY_S3_REARM_GUARD_US);
    }
}

void p4_s3_link_start(void)
{
    if (xTaskCreate(
            link_task,
            "p4_s3_link",
            4096U,
            NULL,
            18U,
            NULL) != pdPASS) {
        ESP_LOGE(TAG, "Cannot create P4/S3 link task");
    }
}

bool p4_s3_link_wait_status(
    p4_s3_ack_t *acknowledgement,
    uint32_t timeout_ms)
{
    return s_ack_queue != NULL && acknowledgement != NULL &&
        xQueueReceive(
            s_ack_queue,
            acknowledgement,
            pdMS_TO_TICKS(timeout_ms)) == pdPASS;
}

uint32_t p4_s3_link_dropped(void)
{
    return s_dropped;
}
