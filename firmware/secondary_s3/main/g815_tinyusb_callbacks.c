#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "g815_usb_descriptors.h"
#include "s3_proxy_reports.h"

#include "esp_log.h"
#include "tusb.h"

static const char *TAG = "g815_tusb_cb";

static uint8_t s_keyboard_leds;

/* Last PC -> G815 vendor reports, stored including the Report ID byte. */
static uint8_t s_vendor_short[G815_VENDOR_SHORT_LENGTH];
static uint8_t s_vendor_long[G815_VENDOR_LONG_LENGTH];

static uint8_t *cache_for(uint8_t report_id)
{
    switch (report_id) {
        case G815_REPORT_ID_VENDOR_SHORT:
            return s_vendor_short;
        case G815_REPORT_ID_VENDOR_LONG:
            return s_vendor_long;
        default:
            return NULL;
    }
}

static size_t expected_length(uint8_t report_id)
{
    switch (report_id) {
        case G815_REPORT_ID_VENDOR_SHORT:
            return G815_VENDOR_SHORT_LENGTH;
        case G815_REPORT_ID_VENDOR_LONG:
            return G815_VENDOR_LONG_LENGTH;
        default:
            return 0U;
    }
}

/*
 * TinyUSB GET_REPORT: TinyUSB prepends the Report ID itself, so the payload
 * without the ID byte is returned here.
 */
uint16_t tud_hid_get_report_cb(
    uint8_t instance,
    uint8_t report_id,
    hid_report_type_t report_type,
    uint8_t *buffer,
    uint16_t requested_length)
{
    if (buffer == NULL || requested_length == 0U) {
        return 0U;
    }
    if (instance == G815_INTERFACE_BOOT_KEYBOARD &&
        report_type == HID_REPORT_TYPE_OUTPUT) {
        buffer[0] = s_keyboard_leds;
        return 1U;
    }
    if (instance != G815_INTERFACE_EXTENDED_HID) {
        return 0U;
    }
    const uint8_t *cache = cache_for(report_id);
    const size_t length = expected_length(report_id);
    if (cache == NULL || length < 2U) {
        return 0U;
    }
    const size_t payload_length = length - 1U;
    const uint16_t copy_length = payload_length < requested_length
        ? (uint16_t)payload_length
        : requested_length;
    memcpy(buffer, &cache[1], copy_length);
    return copy_length;
}

/*
 * TinyUSB SET_REPORT over EP0 removes a leading Report ID from the data
 * stage before calling this function (buffer = payload, report_id = ID).
 * The G815 expects the complete numbered report (ID + payload), therefore
 * the ID byte is restored here before forwarding to the P4.
 */
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

    if (instance == G815_INTERFACE_BOOT_KEYBOARD) {
        if (report_type == HID_REPORT_TYPE_OUTPUT) {
            s_keyboard_leds = buffer[0] & 0x07U;
            (void)s3_proxy_send_set_report(
                instance,
                0U,
                (uint8_t)report_type,
                buffer,
                G815_BOOT_LED_OUTPUT_LENGTH);
        }
        return;
    }
    if (instance != G815_INTERFACE_EXTENDED_HID) {
        return;
    }

    uint8_t report[G815_VENDOR_LONG_LENGTH];
    size_t length;

    if (report_id == 0U) {
        /* Interrupt-OUT path or unnumbered request: ID is buffer[0]. */
        report_id = buffer[0];
        length = buffer_size;
        if (length > sizeof(report)) {
            length = sizeof(report);
        }
        memcpy(report, buffer, length);
    } else if (buffer_size == expected_length(report_id) &&
        buffer[0] == report_id) {
        /* ID was not stripped. */
        length = buffer_size;
        memcpy(report, buffer, length);
    } else {
        /* ID was stripped by TinyUSB: restore it. */
        if ((size_t)buffer_size + 1U > sizeof(report)) {
            ESP_LOGW(TAG, "SET_REPORT id=0x%02X too long: %u",
                report_id, buffer_size);
            return;
        }
        report[0] = report_id;
        memcpy(&report[1], buffer, buffer_size);
        length = (size_t)buffer_size + 1U;
    }

    const size_t expected = expected_length(report_id);
    uint8_t *cache = cache_for(report_id);
    if (expected == 0U || cache == NULL || length != expected) {
        ESP_LOGW(TAG, "SET_REPORT rejected: id=0x%02X length=%u expected=%u",
            report_id, (unsigned)length, (unsigned)expected);
        return;
    }
    memcpy(cache, report, length);

    const esp_err_t error = s3_proxy_send_set_report(
        instance,
        report_id,
        (uint8_t)report_type,
        report,
        length);
    if (error != ESP_OK) {
        ESP_LOGW(TAG, "SET_REPORT forward failed: id=0x%02X %s",
            report_id, esp_err_to_name(error));
    }
}
