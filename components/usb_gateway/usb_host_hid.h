#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "hid_report_decoder.h"

#ifdef __cplusplus
extern "C" {
#endif

#define USB_HOST_HID_MAX_INTERFACES                 8U
#define USB_HOST_HID_MAX_REPORT_DESCRIPTOR_SIZE     2048U

/* Read-only view of an opened HID interface (used by report_tap). */
typedef struct {
    uint8_t interface_id;
    uint8_t device_address;
    uint8_t interface_number;
    uint8_t interface_subclass;
    uint8_t interface_protocol;
    uint16_t vendor_id;
    uint16_t product_id;
    bool descriptor_parsed;
    size_t report_descriptor_length;
    const uint8_t *report_descriptor;
    const hid_decoder_layout_t *layout;
} usb_host_hid_interface_info_t;

/*
 * HID input path on the P4 host port.
 * Raw input reports are routed by the target assigned in usb_host.c:
 *     p4_fs -> usb_device_mirror_input()   (P4 USB1P1 mirror)
 *     s3    -> s3_mirror_input()           (SPI link to the S3 mirror)
 *     tap   -> no PC connection
 *     none  -> dropped
 * Every routed report (p4_fs, s3, tap) is also copied to report_tap (task 3).
 */
esp_err_t usb_host_hid_init(void);

size_t usb_host_hid_get_interface_count(void);

bool usb_host_hid_get_interface_info(
    uint8_t interface_id,
    usb_host_hid_interface_info_t *info);

#ifdef __cplusplus
}
#endif
