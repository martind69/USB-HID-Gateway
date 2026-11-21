#include "spi_link.h"

#include <string.h>

#include "g815_report_router.h"
#include "g815_usb_descriptors.h"
#include "keyboard_report_queue.h"
#include "p4_s3_protocol.h"
#include "s3_proxy_reports.h"
#include "usb_device.h"
#include "driver/spi_slave.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#define STATUS_OK 0x00U
#define STATUS_INVALID_FRAME 0x01U
#define STATUS_UNSUPPORTED 0x02U
#define STATUS_INVALID_LENGTH 0x03U
#define STATUS_USB_NOT_MOUNTED 0x04U
#define STATUS_ENDPOINT_NOT_READY 0x05U
#define STATUS_QUEUE_FULL 0x06U
#define STATUS_INVALID_INTERFACE 0x07U
#define STATUS_UNSUPPORTED_REPORT_ID 0x08U

static const char *TAG = "spi_link";
static p4_s3_frame_t s_next_tx;
static uint32_t s_received;
static uint32_t s_invalid;
static uint32_t s_transactions_failed;
static uint32_t s_reports_accepted;
static uint32_t s_reports_rejected;
static uint8_t s_last_interface = 0xFFU;
static uint8_t s_last_report_id;

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
    const esp_err_t error = spi_slave_initialize(
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

static uint8_t telemetry_flags(void)
{
    uint8_t flags = 0U;
    if (usb_device_mounted()) {
        flags |= P4_S3_S3_FLAG_USB_MOUNTED;
    }
    if (usb_device_interface_ready(G815_INTERFACE_BOOT_KEYBOARD)) {
        flags |= P4_S3_S3_FLAG_IF0_READY;
    }
    if (usb_device_interface_ready(G815_INTERFACE_EXTENDED_HID)) {
        flags |= P4_S3_S3_FLAG_IF1_READY;
    }
    return flags;
}

static void prepare_status(uint8_t sequence, uint8_t status)
{
    const p4_s3_status_payload_t payload = {
        .status = status,
        .telemetry_version = P4_S3_STATUS_TELEMETRY_VERSION,
        .flags = telemetry_flags(),
        .last_interface = s_last_interface,
        .last_report_id = s_last_report_id,
        .spi_received = s_received,
        .spi_invalid = s_invalid,
        .spi_failed = s_transactions_failed,
        .reports_accepted = s_reports_accepted,
        .reports_rejected = s_reports_rejected,
        .usb_reports_sent = usb_device_reports_sent(),
        .usb_reports_failed = usb_device_reports_failed(),
        .queue_dropped = keyboard_report_queue_dropped(),
        .queue_high_watermark = keyboard_report_queue_high_watermark(),
        .output_reports_received = usb_device_output_reports_received(),
    };

    p4_s3_frame_build(
        &s_next_tx,
        P4_S3_MSG_STATUS,
        sequence,
        (const uint8_t *)&payload,
        sizeof(payload));
}

static uint8_t map_validation_error(esp_err_t error)
{
    if (error == ESP_ERR_INVALID_SIZE) {
        return STATUS_INVALID_LENGTH;
    }
    if (error == ESP_ERR_NOT_SUPPORTED) {
        return STATUS_UNSUPPORTED_REPORT_ID;
    }
    return STATUS_INVALID_INTERFACE;
}

static void handle_hid_report(
    const p4_s3_frame_t *received,
    uint8_t interface_number)
{
    const uint8_t *report = received->raw + 6U;
    const size_t length = received->raw[5];
    s_last_interface = interface_number;
    s_last_report_id = interface_number == G815_INTERFACE_EXTENDED_HID && length > 0U
        ? report[0]
        : 0U;

    uint8_t status = STATUS_OK;
    const esp_err_t validation = g815_report_validate(
        interface_number,
        report,
        length);
    if (validation != ESP_OK) {
        status = map_validation_error(validation);
    } else if (!usb_device_mounted()) {
        status = STATUS_USB_NOT_MOUNTED;
    } else {
        const esp_err_t enqueue_error = usb_device_report_enqueue(
            interface_number,
            report,
            length);
        if (enqueue_error == ESP_ERR_NO_MEM) {
            status = STATUS_QUEUE_FULL;
        } else if (enqueue_error != ESP_OK) {
            status = map_validation_error(enqueue_error);
        }
    }

    if (status == STATUS_OK) {
        ++s_reports_accepted;
    } else {
        ++s_reports_rejected;
    }
    prepare_status(received->raw[4], status);
}

static void prepare_response(const p4_s3_frame_t *received)
{
    if (!p4_s3_frame_validate(received)) {
        ++s_invalid;
        prepare_status(received->raw[4], STATUS_INVALID_FRAME);
        return;
    }

    switch (received->raw[3]) {
        case P4_S3_MSG_NOP:
            if (!s3_proxy_next_frame(&s_next_tx)) {
                p4_s3_frame_build(
                    &s_next_tx,
                    P4_S3_MSG_NOP,
                    0U,
                    NULL,
                    0U);
            }
            break;

        case P4_S3_MSG_G815_IF0_INPUT:
            handle_hid_report(received, G815_INTERFACE_BOOT_KEYBOARD);
            break;

        case P4_S3_MSG_G815_IF1_INPUT:
            handle_hid_report(received, G815_INTERFACE_EXTENDED_HID);
            break;

        case P4_S3_MSG_G815_ATTACH:
            keyboard_report_queue_clear();
            prepare_status(received->raw[4], STATUS_OK);
            break;

        case P4_S3_MSG_G815_DETACH:
            usb_device_release_all();
            prepare_status(received->raw[4], STATUS_OK);
            break;

        case P4_S3_MSG_CONTROL_RESPONSE_FRAGMENT:
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

    while (true) {
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
    }
}
