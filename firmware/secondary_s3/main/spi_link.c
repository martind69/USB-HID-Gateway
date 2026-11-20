#include "spi_link.h"

#include "p4_s3_protocol.h"
#include "keyboard_report_queue.h"
#include "s3_proxy_reports.h"
#include "usb_device.h"

#include "driver/spi_slave.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include <string.h>

#define STATUS_OK 0x00U
#define STATUS_INVALID_FRAME 0x01U
#define STATUS_UNSUPPORTED 0x02U
#define STATUS_INVALID_LENGTH 0x03U
#define STATUS_USB_NOT_MOUNTED 0x04U
#define STATUS_QUEUE_FULL 0x06U

static const char *TAG = "spi_link";
static p4_s3_frame_t s_next_tx;
static uint32_t s_received;
static uint32_t s_invalid;
static uint32_t s_transactions_failed;
static uint32_t s_keyboard_accepted;
static uint32_t s_keyboard_rejected;

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
        .queue_size = 4,
    };

    p4_s3_frame_build(&s_next_tx, P4_S3_MSG_NOP, 0U, NULL, 0U);

    esp_err_t error = spi_slave_initialize(
        SPI2_HOST,
        &bus_config,
        &device_config,
        SPI_DMA_CH_AUTO);
    if (error != ESP_OK) {
        return error;
    }

    ESP_LOGI(
        TAG,
        "SPI slave ready: SCLK=%d MOSI=%d MISO=%d CS=%d",
        CONFIG_SECONDARY_S3_SPI_SCLK_GPIO,
        CONFIG_SECONDARY_S3_SPI_MOSI_GPIO,
        CONFIG_SECONDARY_S3_SPI_MISO_GPIO,
        CONFIG_SECONDARY_S3_SPI_CS_GPIO);
    return ESP_OK;
}

static void prepare_status(uint8_t sequence, uint8_t status)
{
    p4_s3_frame_build(
        &s_next_tx,
        P4_S3_MSG_STATUS,
        sequence,
        &status,
        1U);
}

static void handle_keyboard_report(const p4_s3_frame_t *received)
{
    uint8_t status;

    if (received->raw[5] != S3_KEYBOARD_REPORT_SIZE) {
        status = STATUS_INVALID_LENGTH;
        ++s_keyboard_rejected;
    } else if (!usb_device_mounted()) {
        status = STATUS_USB_NOT_MOUNTED;
        ++s_keyboard_rejected;
    } else if (!usb_device_keyboard_enqueue(
                   received->raw + 6U,
                   received->raw[5])) {
        status = STATUS_QUEUE_FULL;
        ++s_keyboard_rejected;
    } else {
        status = STATUS_OK;
        ++s_keyboard_accepted;
    }

    prepare_status(received->raw[4], status);
}

static void prepare_response(const p4_s3_frame_t *received)
{
    if (s3_proxy_next_frame(&s_next_tx)) {
        return;
    }

    if (!p4_s3_frame_validate(received)) {
        ++s_invalid;
        prepare_status(received->raw[4], STATUS_INVALID_FRAME);
        return;
    }

    switch (received->raw[3]) {
        case P4_S3_MSG_NOP:
            p4_s3_frame_build(
                &s_next_tx,
                P4_S3_MSG_NOP,
                0U,
                NULL,
                0U);
            break;

        case P4_S3_MSG_G815_IF0_INPUT:
            handle_keyboard_report(received);
            break;

        case P4_S3_MSG_G815_IF1_INPUT:
        case P4_S3_MSG_G815_ATTACH:
        case P4_S3_MSG_G815_DETACH:
            prepare_status(received->raw[4], STATUS_OK);
            break;

        case 0x41U:
            prepare_status(
                received->raw[4],
                s3_proxy_accept_response(received) == ESP_OK
                    ? STATUS_OK
                    : STATUS_INVALID_LENGTH);
            break;

        default:
            prepare_status(received->raw[4], STATUS_UNSUPPORTED);
            break;
    }
}

void spi_link_task(void *argument)
{
    (void)argument;
    p4_s3_frame_t received;

    for (;;) {
        memset(&received, 0, sizeof(received));

        spi_slave_transaction_t transaction = {
            .length = P4_S3_FRAME_SIZE * 8U,
            .tx_buffer = s_next_tx.raw,
            .rx_buffer = received.raw,
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
        prepare_response(&received);

        if ((s_received % 1000U) == 0U) {
            ESP_LOGI(
                TAG,
                "RX=%lu invalid=%lu failed=%lu keyboard_ok=%lu keyboard_reject=%lu usb_sent=%lu usb_failed=%lu queue_drop=%lu",
                (unsigned long)s_received,
                (unsigned long)s_invalid,
                (unsigned long)s_transactions_failed,
                (unsigned long)s_keyboard_accepted,
                (unsigned long)s_keyboard_rejected,
                (unsigned long)usb_device_reports_sent(),
                (unsigned long)usb_device_reports_failed(),
                (unsigned long)keyboard_report_queue_dropped());
        }
    }
}
