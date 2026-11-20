#include "usb_device.h"

#include "keyboard_report_queue.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "tusb.h"

#include <string.h>

#define USB_VID 0x303AU
#define USB_PID 0x4001U
#define USB_BCD 0x0100U
#define USB_HID_INTERFACE 0U
#define USB_HID_ENDPOINT 0x81U
#define USB_HID_EP_SIZE 8U
#define USB_CONFIG_TOTAL_LENGTH \
    (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN)

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
    .idVendor = USB_VID,
    .idProduct = USB_PID,
    .bcdDevice = USB_BCD,
    .iManufacturer = 0x01,
    .iProduct = 0x02,
    .iSerialNumber = 0x03,
    .bNumConfigurations = 0x01,
};

static const uint8_t s_hid_report_descriptor[] = {
    TUD_HID_REPORT_DESC_KEYBOARD()
};

static const uint8_t s_configuration_descriptor[] = {
    TUD_CONFIG_DESCRIPTOR(
        1,
        1,
        0,
        USB_CONFIG_TOTAL_LENGTH,
        TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP,
        100),
    TUD_HID_DESCRIPTOR(
        USB_HID_INTERFACE,
        0,
        HID_ITF_PROTOCOL_KEYBOARD,
        sizeof(s_hid_report_descriptor),
        USB_HID_ENDPOINT,
        USB_HID_EP_SIZE,
        1),
};

static const char *s_string_descriptors[] = {
    (const char[]) { 0x09, 0x04 },
    "USB HID Gateway",
    "S3 Test Keyboard",
    "S3-0001",
};

static void keyboard_task(void *argument)
{
    (void)argument;
    s3_keyboard_report_t report;

    for (;;) {
        if (!keyboard_report_queue_receive(&report, 100U)) {
            continue;
        }

        while (!tud_mounted()) {
            vTaskDelay(pdMS_TO_TICKS(10U));
        }

        while (!tud_hid_n_ready(0U)) {
            vTaskDelay(pdMS_TO_TICKS(1U));
        }

        if (tud_hid_n_report(
                0U,
                0U,
                report.data,
                sizeof(report.data))) {
            ++s_reports_sent;
        } else {
            ++s_reports_failed;
            ESP_LOGW(TAG, "TinyUSB rejected keyboard report");
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
        (int)(sizeof(s_string_descriptors) /
            sizeof(s_string_descriptors[0]));
    config.descriptor.full_speed_config =
        s_configuration_descriptor;
    config.descriptor.high_speed_config = NULL;

    error = tinyusb_driver_install(&config);
    if (error != ESP_OK) {
        ESP_LOGE(
            TAG,
            "TinyUSB install failed: %s",
            esp_err_to_name(error));
        return error;
    }

    if (xTaskCreate(
            keyboard_task,
            "s3_hid_keyboard",
            4096U,
            NULL,
            12U,
            NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(
        TAG,
        "TinyUSB boot keyboard installed: VID=%04X PID=%04X EP=0x%02X",
        USB_VID,
        USB_PID,
        USB_HID_ENDPOINT);
    return ESP_OK;
}

bool usb_device_keyboard_enqueue(
    const uint8_t *report,
    size_t length)
{
    return keyboard_report_queue_enqueue(report, length);
}

bool usb_device_mounted(void)
{
    return tud_mounted();
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
    (void)instance;
    return s_hid_report_descriptor;
}

uint16_t tud_hid_get_report_cb(
    uint8_t instance,
    uint8_t report_id,
    hid_report_type_t report_type,
    uint8_t *buffer,
    uint16_t requested_length)
{
    (void)instance;
    (void)report_id;
    (void)report_type;

    if (buffer == NULL || requested_length == 0U) {
        return 0U;
    }

    const uint16_t length = requested_length < S3_KEYBOARD_REPORT_SIZE
        ? requested_length
        : S3_KEYBOARD_REPORT_SIZE;
    memset(buffer, 0, length);
    return length;
}

void tud_hid_set_report_cb(
    uint8_t instance,
    uint8_t report_id,
    hid_report_type_t report_type,
    uint8_t const *buffer,
    uint16_t buffer_size)
{
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)buffer_size;
}
