#include <string.h>

#include "g815_usb_descriptors.h"
#include "s3_proxy_reports.h"
#include "usb_device.h"
#include "esp_log.h"
#include "tusb.h"

static const char *TAG = "g815_usb_callbacks";
static uint32_t s_output_reports_received;
static uint32_t s_output_reports_failed;

uint16_t tud_hid_get_report_cb(
    uint8_t instance,
    uint8_t report_id,
    hid_report_type_t report_type,
    uint8_t *buffer,
    uint16_t requested_length)
{
    if (buffer == NULL || report_type != HID_REPORT_TYPE_FEATURE) {
        return 0U;
    }

    const esp_err_t error = s3_proxy_send_get_report(
        instance,
        report_id,
        (uint8_t)report_type,
        requested_length);
    if (error != ESP_OK) {
        ++s_output_reports_failed;
        return 0U;
    }

    /* TinyUSB expects this callback synchronously. The SPI task completes
     * the queued request while this task waits for the response. */
    const size_t received = s3_proxy_copy_get_response(
        report_id,
        buffer,
        requested_length,
        1000U);
    return (uint16_t)received;
}

void tud_hid_set_report_cb(
    uint8_t instance,
    uint8_t report_id,
    hid_report_type_t report_type,
    const uint8_t *buffer,
    uint16_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0U) {
        return;
    }

    ++s_output_reports_received;

    uint8_t complete_report[64];
    const uint8_t *report = buffer;
    size_t report_length = buffer_size;

    /* TinyUSB may remove the report ID before invoking the callback. The
     * physical G815 SET_REPORT transfer includes the report ID in wLength. */
    if (instance == G815_INTERFACE_EXTENDED_HID &&
        report_id != 0U &&
        buffer[0] != report_id) {
        if ((size_t)buffer_size + 1U > sizeof(complete_report)) {
            ++s_output_reports_failed;
            return;
        }
        complete_report[0] = report_id;
        memcpy(complete_report + 1U, buffer, buffer_size);
        report = complete_report;
        report_length = (size_t)buffer_size + 1U;
    }

    const esp_err_t error = s3_proxy_send_set_report(
        instance,
        report_id,
        (uint8_t)report_type,
        report,
        report_length);
    if (error != ESP_OK) {
        ++s_output_reports_failed;
        ESP_LOGW(
            TAG,
            "SET_REPORT queue failed: iface=%u type=%u id=0x%02X length=%u error=%s",
            instance,
            (unsigned)report_type,
            report_id,
            (unsigned)report_length,
            esp_err_to_name(error));
    }
}

uint32_t usb_device_output_reports_received(void)
{
    return s_output_reports_received;
}
