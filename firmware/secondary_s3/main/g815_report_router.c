#include "g815_report_router.h"
#include "g815_usb_descriptors.h"

static size_t expected_if1_length(uint8_t report_id)
{
    switch (report_id) {
        case G815_REPORT_ID_EXTENDED_KEYBOARD:
            return G815_EXTENDED_KEYBOARD_LENGTH;
        case G815_REPORT_ID_MOUSE:
            return G815_MOUSE_LENGTH;
        case G815_REPORT_ID_CONSUMER:
            return G815_CONSUMER_LENGTH;
        case G815_REPORT_ID_VENDOR_SHORT:
            return G815_VENDOR_SHORT_LENGTH;
        case G815_REPORT_ID_VENDOR_LONG:
            return G815_VENDOR_LONG_LENGTH;
        default:
            return 0U;
    }
}

esp_err_t g815_report_validate(uint8_t interface_number,
                              const uint8_t *report,
                              size_t report_length)
{
    if (report == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (interface_number == G815_INTERFACE_BOOT_KEYBOARD) {
        return report_length == G815_BOOT_INPUT_LENGTH
            ? ESP_OK
            : ESP_ERR_INVALID_SIZE;
    }
    if (interface_number != G815_INTERFACE_EXTENDED_HID || report_length == 0U) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    const size_t expected = expected_if1_length(report[0]);
    if (expected == 0U) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    return report_length == expected ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

bool g815_report_is_release(uint8_t interface_number,
                            const uint8_t *report,
                            size_t report_length)
{
    if (g815_report_validate(interface_number, report, report_length) != ESP_OK) {
        return false;
    }
    size_t first_payload = interface_number == G815_INTERFACE_EXTENDED_HID ? 1U : 0U;
    for (size_t index = first_payload; index < report_length; ++index) {
        if (report[index] != 0U) {
            return false;
        }
    }
    return true;
}
