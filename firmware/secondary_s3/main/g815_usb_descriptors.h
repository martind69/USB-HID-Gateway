#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define G815_USB_VID 0x046DU
#define G815_USB_PID 0xC33FU
#define G815_USB_BCD_DEVICE 0x3102U

#define G815_INTERFACE_BOOT_KEYBOARD 0U
#define G815_INTERFACE_EXTENDED_HID 1U

#define G815_REPORT_ID_EXTENDED_KEYBOARD 0x01U
#define G815_REPORT_ID_MOUSE 0x02U
#define G815_REPORT_ID_CONSUMER 0x03U
#define G815_REPORT_ID_VENDOR_SHORT 0x11U
#define G815_REPORT_ID_VENDOR_LONG 0x12U

#define G815_BOOT_INPUT_LENGTH 8U
#define G815_BOOT_LED_OUTPUT_LENGTH 1U
#define G815_EXTENDED_KEYBOARD_LENGTH 21U
#define G815_MOUSE_LENGTH 8U
#define G815_CONSUMER_LENGTH 2U
#define G815_VENDOR_SHORT_LENGTH 20U
#define G815_VENDOR_LONG_LENGTH 64U

extern const uint8_t g815_hid_report_descriptor_if0[65];
extern const uint8_t g815_hid_report_descriptor_if1[188];

#ifdef __cplusplus
}
#endif
