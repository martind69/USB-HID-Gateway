#include "usb_device.h"

#include "g815_report_router.h"
#include "g815_usb_descriptors.h"
#include "keyboard_report_queue.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "tusb.h"

#define G815_CONFIGURATION_TOTAL_LENGTH \
    (TUD_CONFIG_DESC_LEN + (2U * TUD_HID_DESC_LEN))
#define G815_IF0_ENDPOINT 0x81U
#define G815_IF1_ENDPOINT 0x82U
#define G815_IF0_PACKET_SIZE 8U
#define G815_IF1_PACKET_SIZE 64U

static const char *TAG = "s3_usb_device";
static uint32_t s_reports_sent;
static uint32_t s_reports_failed;

static const tusb_desc_device_t s_device_descriptor = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = 0x00,
    .bDeviceSubClass = 0x00,
    .bDeviceProtocol = 0x00,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = G815_USB_VID,
    .idProduct = G815_USB_PID,
    .bcdDevice = G815_USB_BCD_DEVICE,
    .iManufacturer = 0x01,
    .iProduct = 0x02,
    .iSerialNumber = 0x03,
    .bNumConfigurations = 0x01,
};

static const uint8_t s_configuration_descriptor[] = {
    TUD_CONFIG_DESCRIPTOR(
        1,
        2,
        0,
        G815_CONFIGURATION_TOTAL_LENGTH,
        TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP,
        500),
    TUD_HID_DESCRIPTOR(
        G815_INTERFACE_BOOT_KEYBOARD,
        0,
        HID_ITF_PROTOCOL_KEYBOARD,
        sizeof(g815_hid_report_descriptor_if0),
        G815_IF0_ENDPOINT,
        G815_IF0_PACKET_SIZE,
        1),
    TUD_HID_DESCRIPTOR(
        G815_INTERFACE_EXTENDED_HID,
        0,
        HID_ITF_PROTOCOL_NONE,
        sizeof(g815_hid_report_descriptor_if1),
        G815_IF1_ENDPOINT,
        G815_IF1_PACKET_SIZE,
        1),
};

static const char *s_string_descriptors[] = {
    (const char[]) { 0x09, 0x04 },
    "Logitech",
    "G815 RGB Mechanical Gaming Keyboard",
    "G815-PROXY-S3",
};

static bool send_report(const s3_hid_report_t *report)
{
    if (report->interface_number == G815_INTERFACE_BOOT_KEYBOARD) {
        return tud_hid_n_report(
            G815_INTERFACE_BOOT_KEYBOARD,
            0U,
            report->data,
            report->length);
    }

    if (report->interface_number == G815_INTERFACE_EXTENDED_HID &&
        report->length >= 1U) {
        return tud_hid_n_report(
            G815_INTERFACE_EXTENDED_HID,
            report->data[0],
            report->data + 1U,
            report->length - 1U);
    }

    return false;
}

static void hid_report_task(void *argument)
{
    (void)argument;
    s3_hid_report_t report;

    while (true) {
        if (!keyboard_report_queue_receive(&report, 100U)) {
            continue;
        }

        bool sent = false;
        for (uint32_t wait_ms = 0U; wait_ms < 1000U; ++wait_ms) {
            if (!tud_mounted()) {
                vTaskDelay(pdMS_TO_TICKS(1U));
                continue;
            }
            if (!tud_hid_n_ready(report.interface_number)) {
                vTaskDelay(pdMS_TO_TICKS(1U));
                continue;
            }
            sent = send_report(&report);
            break;
        }

        if (sent) {
            ++s_reports_sent;
        } else {
            ++s_reports_failed;
            ESP_LOGW(
                TAG,
                "HID report failed: interface=%u length=%u report_id=0x%02X",
                report.interface_number,
                report.length,
                report.interface_number == G815_INTERFACE_EXTENDED_HID
                    ? report.data[0]
                    : 0U);
        }
    }
}

esp_err_t usb_device_init(void)
{
    esp_err_t error = keyboard_report_queue_init();
    if (error != ESP_OK) {
        return error;
    }

    tinyusb_config_t config = TINYUSB_DEFAULT_CONFIG();
    config.port = TINYUSB_PORT_FULL_SPEED_0;
    config.descriptor.device = &s_device_descriptor;
    config.descriptor.qualifier = NULL;
    config.descriptor.string = s_string_descriptors;
    config.descriptor.string_count =
        (int)(sizeof(s_string_descriptors) / sizeof(s_string_descriptors[0]));
    config.descriptor.full_speed_config = s_configuration_descriptor;
    config.descriptor.high_speed_config = NULL;

    error = tinyusb_driver_install(&config);
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "TinyUSB install failed: %s", esp_err_to_name(error));
        return error;
    }

    if (xTaskCreate(
            hid_report_task,
            "s3_g815_hid",
            4096U,
            NULL,
            12U,
            NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(
        TAG,
        "G815 TinyUSB device installed: VID=%04X PID=%04X IF0=0x81 IF1=0x82",
        G815_USB_VID,
        G815_USB_PID);
    return ESP_OK;
}

esp_err_t usb_device_report_enqueue(
    uint8_t interface_number,
    const uint8_t *report,
    size_t length)
{
    const esp_err_t validation = g815_report_validate(
        interface_number,
        report,
        length);
    if (validation != ESP_OK) {
        return validation;
    }
    return keyboard_report_queue_enqueue(interface_number, report, length);
}

void usb_device_release_all(void)
{
    static const uint8_t keyboard_release[G815_BOOT_INPUT_LENGTH] = { 0 };
    static const uint8_t extended_release[G815_EXTENDED_KEYBOARD_LENGTH] = {
        G815_REPORT_ID_EXTENDED_KEYBOARD
    };
    static const uint8_t consumer_release[G815_CONSUMER_LENGTH] = {
        G815_REPORT_ID_CONSUMER, 0U
    };

    keyboard_report_queue_clear();
    (void)keyboard_report_queue_enqueue(
        G815_INTERFACE_BOOT_KEYBOARD,
        keyboard_release,
        sizeof(keyboard_release));
    (void)keyboard_report_queue_enqueue(
        G815_INTERFACE_EXTENDED_HID,
        extended_release,
        sizeof(extended_release));
    (void)keyboard_report_queue_enqueue(
        G815_INTERFACE_EXTENDED_HID,
        consumer_release,
        sizeof(consumer_release));
}

bool usb_device_mounted(void)
{
    return tud_mounted();
}

bool usb_device_interface_ready(uint8_t interface_number)
{
    return interface_number < 2U &&
        tud_mounted() &&
        tud_hid_n_ready(interface_number);
}

uint32_t usb_device_reports_sent(void)
{
    return s_reports_sent;
}

uint32_t usb_device_reports_failed(void)
{
    return s_reports_failed;
}

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance)
{
    if (instance == G815_INTERFACE_BOOT_KEYBOARD) {
        return g815_hid_report_descriptor_if0;
    }
    if (instance == G815_INTERFACE_EXTENDED_HID) {
        return g815_hid_report_descriptor_if1;
    }
    return NULL;
}
