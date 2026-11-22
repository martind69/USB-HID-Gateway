#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "g815_usb_descriptors.h"
#include "s3_proxy_reports.h"
#include "tusb.h"

static uint8_t s_keyboard_leds;
static uint8_t s_vendor_short[G815_VENDOR_SHORT_LENGTH];
static uint8_t s_vendor_long[G815_VENDOR_LONG_LENGTH];

static uint16_t copy_cached_report(
    uint8_t report_id,
    uint8_t *buffer,
    uint16_t requested_length)
{
    const uint8_t *source = NULL;
    uint16_t source_length = 0U;

    switch (report_id) {
        case G815_REPORT_ID_VENDOR_SHORT:
            source = s_vendor_short;
            source_length = sizeof(s_vendor_short);
            break;

        case G815_REPORT_ID_VENDOR_LONG:
            source = s_vendor_long;
            source_length = sizeof(s_vendor_long);
            break;

        default:
            return 0U;
    }

    const uint16_t copy_length = source_length < requested_length
        ? source_length
        : requested_length;

    memcpy(buffer, source, copy_length);
    return copy_length;
}

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

    if (instance != G815_INTERFACE_EXTENDED_HID ||
        (report_type != HID_REPORT_TYPE_OUTPUT &&
         report_type != HID_REPORT_TYPE_FEATURE)) {
        return 0U;
    }

    return copy_cached_report(report_id, buffer, requested_length);
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

    if (instance == G815_INTERFACE_BOOT_KEYBOARD &&
        report_type == HID_REPORT_TYPE_OUTPUT) {
        s_keyboard_leds = buffer[0] & 0x07U;
        (void)s3_proxy_send_set_report(
            instance,
            0U,
            (uint8_t)report_type,
            buffer,
            buffer_size);
        return;
    }

    if (instance != G815_INTERFACE_EXTENDED_HID) {
        return;
    }

    if (report_id == 0U) {
        report_id = buffer[0];
    }

    if (report_id == G815_REPORT_ID_VENDOR_SHORT) {
        const size_t copy_length = buffer_size < sizeof(s_vendor_short)
            ? buffer_size
            : sizeof(s_vendor_short);
        memset(s_vendor_short, 0, sizeof(s_vendor_short));
        memcpy(s_vendor_short, buffer, copy_length);
    } else if (report_id == G815_REPORT_ID_VENDOR_LONG) {
        const size_t copy_length = buffer_size < sizeof(s_vendor_long)
            ? buffer_size
            : sizeof(s_vendor_long);
        memset(s_vendor_long, 0, sizeof(s_vendor_long));
        memcpy(s_vendor_long, buffer, copy_length);
    } else {
        return;
    }

    (void)s3_proxy_send_set_report(
        instance,
        report_id,
        (uint8_t)report_type,
        buffer,
        buffer_size);
}
