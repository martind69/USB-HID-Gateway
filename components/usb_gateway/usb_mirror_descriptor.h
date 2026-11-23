#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Device-independent analysis of USB descriptors for the transparent mirror.
 * Pure C, no ESP-IDF or TinyUSB dependency, host-testable.
 */

#define USB_MIRROR_MAX_HID_INTERFACES   4U

typedef struct {
    uint8_t interface_count;        /* bNumInterfaces */
    uint8_t hid_count;              /* HID interfaces, alternate setting 0 */
    uint8_t hid_interface[USB_MIRROR_MAX_HID_INTERFACES];
    uint16_t hid_report_length[USB_MIRROR_MAX_HID_INTERFACES];
    uint16_t hid_descriptor_offset[USB_MIRROR_MAX_HID_INTERFACES];
    bool has_keyboard;              /* boot interface, protocol 1 */
    bool has_mouse;                 /* boot interface, protocol 2 */
} usb_mirror_layout_t;

/*
 * Parses a configuration descriptor (wTotalLength bytes).
 *
 * The layout, including has_keyboard / has_mouse, is filled whenever the
 * descriptor is well-formed, also if the device cannot be mirrored.
 *
 * Returns:
 *     ESP_OK                  mirrorable
 *     ESP_ERR_NOT_SUPPORTED   well-formed but not mirrorable: non-HID
 *                             interface, alternate setting, non-interrupt
 *                             endpoint, wMaxPacketSize > max_packet_size,
 *                             more than max_hid_interfaces HID interfaces
 *     ESP_ERR_INVALID_SIZE    malformed descriptor
 *     ESP_ERR_INVALID_ARG     NULL argument
 */
esp_err_t usb_mirror_parse_configuration(
    const uint8_t *configuration,
    size_t length,
    size_t max_hid_interfaces,
    uint16_t max_packet_size,
    usb_mirror_layout_t *layout);

/* Returns the HID instance (index in layout) of an interface number or -1. */
int usb_mirror_hid_instance(
    const usb_mirror_layout_t *layout,
    uint8_t interface_number);

/*
 * Converts a raw string descriptor (bLength, 0x03, UTF-16LE) to ASCII.
 * Characters >= 0x80 become '?'. Output is always NUL-terminated.
 * Returns the number of characters written; *lossy is set if characters
 * were replaced or truncated.
 */
size_t usb_mirror_string_to_ascii(
    const uint8_t *string_descriptor,
    char *output,
    size_t capacity,
    bool *lossy);

#ifdef __cplusplus
}
#endif
