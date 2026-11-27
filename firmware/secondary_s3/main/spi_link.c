#include "spi_link.h"

#include <string.h>

#include "p4_s3_protocol.h"
#include "s3_mirror_session.h"
#include "s3_ota_receiver.h"
#include "s3_proxy_reports.h"

#include "driver/spi_slave.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static const char *TAG = "spi_link";

/* Static, word-aligned DMA buffers (alignment from p4_s3_frame_t). */
static p4_s3_frame_t s_next_tx;
static p4_s3_frame_t s_rx;

static uint32_t s_received;
static uint32_t s_invalid;
static uint32_t s_transactions_failed;

static void prepare_status(
    uint8_t sequence,
    const uint8_t *payload,
    uint8_t length)
{
    p4_s3_frame_build(&s_next_tx, P4_S3_MSG_STATUS, sequence, payload, length);
}

static void prepare_simple_status(uint8_t sequence, uint8_t status)
{
    prepare_status(sequence, &status, 1U);
}

static void prepare_idle(void)
{
    /* Deliver pending S3 -> P4 traffic on the next poll, otherwise NOP. */
    if (!s3_proxy_next_frame(&s_next_tx)) {
        p4_s3_frame_build(&s_next_tx, P4_S3_MSG_NOP, 0U, NULL, 0U);
    }
}

static void handle_ota(const p4_s3_frame_t *received)
{
    p4_s3_ota_status_t status;
    (void)s3_ota_receiver_handle(
        received->raw[P4_S3_OFFSET_TYPE],
        &received->raw[P4_S3_OFFSET_PAYLOAD],
        received->raw[P4_S3_OFFSET_LENGTH],
        &status);
    prepare_status(
        received->raw[P4_S3_OFFSET_SEQUENCE],
        (const uint8_t *)&status,
        (uint8_t)sizeof(status));
}

static void handle_stats(uint8_t sequence)
{
    p4_s3_mirror_stats_t stats;
    s3_mirror_session_stats(&stats);
    prepare_status(sequence, (const uint8_t *)&stats, (uint8_t)sizeof(stats));
}

static void prepare_response(const p4_s3_frame_t *received)
{
    if (p4_s3_frame_is_empty(received)) {
        /* Clock without valid data (P4 reset or bus noise). */
        prepare_idle();
        return;
    }
    if (!p4_s3_frame_validate(received)) {
        ++s_invalid;
        prepare_simple_status(
            received->raw[P4_S3_OFFSET_SEQUENCE],
            P4_S3_STATUS_INVALID_FRAME);
        return;
    }

    const uint8_t type = received->raw[P4_S3_OFFSET_TYPE];
    const uint8_t sequence = received->raw[P4_S3_OFFSET_SEQUENCE];
    const uint8_t *payload = &received->raw[P4_S3_OFFSET_PAYLOAD];
    const uint8_t length = received->raw[P4_S3_OFFSET_LENGTH];

    if (p4_s3_is_ota_message(type)) {
        handle_ota(received);
        return;
    }
    if (p4_s3_is_input_message(type)) {
        prepare_simple_status(
            sequence,
            s3_mirror_session_input(
                (uint8_t)(type - P4_S3_MSG_INPUT_BASE), payload, length));
        return;
    }
    switch (type) {
        case P4_S3_MSG_NOP:
            prepare_idle();
            break;
        case P4_S3_MSG_MIRROR_BEGIN:
            prepare_simple_status(sequence, s3_mirror_session_begin(payload, length));
            break;
        case P4_S3_MSG_MIRROR_DESCRIPTOR:
            prepare_simple_status(sequence, s3_mirror_session_descriptor(payload, length));
            break;
        case P4_S3_MSG_MIRROR_COMMIT:
            prepare_simple_status(sequence, s3_mirror_session_commit());
            break;
        case P4_S3_MSG_MIRROR_END:
            prepare_simple_status(sequence, s3_mirror_session_end());
            break;
        case P4_S3_MSG_MIRROR_STATS:
            handle_stats(sequence);
            break;
        default:
            prepare_simple_status(sequence, P4_S3_STATUS_UNSUPPORTED);
            break;
    }
}

esp_err_t spi_link_init(void)
{
    const spi_bus_config_t bus_config = {
        .sclk_io_num = CONFIG_SECONDARY_S3_SPI_SCLK_GPIO,
        .mosi_io_num = CONFIG_SECONDARY_S3_SPI_MOSI_GPIO,
        .miso_io_num = CONFIG_SECONDARY_S3_SPI_MISO_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = P4_S3_FRAME_SIZE,
    };
    const spi_slave_interface_config_t device_config = {
        .spics_io_num = CONFIG_SECONDARY_S3_SPI_CS_GPIO,
        .mode = 0,
        .queue_size = 1,
    };
    p4_s3_frame_build(&s_next_tx, P4_S3_MSG_NOP, 0U, NULL, 0U);
    const esp_err_t error = spi_slave_initialize(
        SPI2_HOST,
        &bus_config,
        &device_config,
        SPI_DMA_CH_AUTO);
    if (error != ESP_OK) {
        return error;
    }
    ESP_LOGI(TAG, "SPI slave ready: SCLK=%d MOSI=%d MISO=%d CS=%d",
        CONFIG_SECONDARY_S3_SPI_SCLK_GPIO,
        CONFIG_SECONDARY_S3_SPI_MOSI_GPIO,
        CONFIG_SECONDARY_S3_SPI_MISO_GPIO,
        CONFIG_SECONDARY_S3_SPI_CS_GPIO);
    return ESP_OK;
}

void spi_link_task(void *argument)
{
    (void)argument;
    while (true) {
        memset(&s_rx, 0, sizeof(s_rx));
        spi_slave_transaction_t transaction = {
            .length = P4_S3_FRAME_SIZE * 8U,
            .tx_buffer = s_next_tx.raw,
            .rx_buffer = s_rx.raw,
        };
        const esp_err_t error = spi_slave_transmit(
            SPI2_HOST,
            &transaction,
            portMAX_DELAY);
        if (error != ESP_OK) {
            ++s_transactions_failed;
            continue;
        }
        ++s_received;
        prepare_response(&s_rx);
    }
}
