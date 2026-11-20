#include <string.h>
#include "g815_usb_descriptors.h"
#include "tusb.h"

static uint8_t s_keyboard_leds;
static uint8_t s_vendor_short[G815_VENDOR_SHORT_LENGTH];
static uint8_t s_vendor_long[G815_VENDOR_LONG_LENGTH];

uint16_t tud_hid_get_report_cb(uint8_t instance,
                               uint8_t report_id,
                               hid_report_type_t report_type,
                               uint8_t *buffer,
                               uint16_t requested_length)
{
    if (buffer == NULL || instance != G815_INTERFACE_EXTENDED_HID) {
        return 0U;
    }
    if (report_type != HID_REPORT_TYPE_OUTPUT &&
        report_type != HID_REPORT_TYPE_FEATURE) {
        return 0U;
    }
    const uint8_t *source = NULL;
    uint16_t length = 0U;
    if (report_id == G815_REPORT_ID_VENDOR_SHORT) {
        source = s_vendor_short;
        length = sizeof(s_vendor_short);
    } else if (report_id == G815_REPORT_ID_VENDOR_LONG) {
        source = s_vendor_long;
        length = sizeof(s_vendor_long);
    }
    if (source == NULL) {
        return 0U;
    }
    if (length > requested_length) {
        length = requested_length;
    }
    memcpy(buffer, source, length);
    return length;
}

void tud_hid_set_report_cb(uint8_t instance,
                           uint8_t report_id,
                           hid_report_type_t report_type,
                           const uint8_t *buffer,
                           uint16_t buffer_size)
{
    if (buffer == NULL) {
        return;
    }
    if (instance == G815_INTERFACE_BOOT_KEYBOARD &&
        report_type == HID_REPORT_TYPE_OUTPUT &&
        buffer_size >= 1U) {
        s_keyboard_leds = buffer[0] & 0x07U;
        return;
    }
    if (instance != G815_INTERFACE_EXTENDED_HID) {
        return;
    }
    if (report_id == 0U && buffer_size > 0U) {
        report_id = buffer[0];
    }
    if (report_id == G815_REPORT_ID_VENDOR_SHORT) {
        const uint16_t length = buffer_size < sizeof(s_vendor_short)
            ? buffer_size : sizeof(s_vendor_short);
        memcpy(s_vendor_short, buffer, length);
    } else if (report_id == G815_REPORT_ID_VENDOR_LONG) {
        const uint16_t length = buffer_size < sizeof(s_vendor_long)
            ? buffer_size : sizeof(s_vendor_long);
        memcpy(s_vendor_long, buffer, length);
    }
}
