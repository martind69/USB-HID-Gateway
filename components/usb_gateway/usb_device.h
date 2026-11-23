#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "usb/usb_host.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Transparent USB mirror on the P4 USB1P1 full-speed device port.
 *
 * The PC sees exactly the device assigned to target "p4_fs" in
 * /sdcard/config/gateway.json: device, configuration, report and string
 * descriptors are copied at runtime from the real device. No device
 * identity is compiled in.
 *
 * Lifecycle (address = USB host address of the real device):
 *     usb_device_mirror_begin()                 descriptors from usb_host.c
 *     usb_device_mirror_set_report_descriptor() per HID interface, any order
 *     -> connect to PC when all report descriptors are present
 *     usb_device_mirror_input()                 raw input reports, unchanged
 *     usb_device_mirror_end()                   disconnect from PC
 *
 * PC -> device: HID SET_REPORT (output, feature, interrupt OUT data) is
 * forwarded unchanged to EP0 of the real device.
 */

void usb_device_init(void);
void usb_device_process(void);

/* Maximum mirrorable HID interfaces (min of TinyUSB CFG_TUD_HID and parser). */
size_t usb_device_mirror_max_hid(void);

esp_err_t usb_device_mirror_begin(
    uint8_t address,
    const usb_device_desc_t *device_descriptor,
    const usb_config_desc_t *configuration_descriptor,
    const usb_device_info_t *device_info);

void usb_device_mirror_set_report_descriptor(
    uint8_t address,
    uint8_t interface_number,
    const uint8_t *descriptor,
    size_t length);

bool usb_device_mirror_input(
    uint8_t address,
    uint8_t interface_number,
    const uint8_t *report,
    size_t length);

void usb_device_mirror_end(uint8_t address);

bool usb_device_mirror_connected(void);

#ifdef __cplusplus
}
#endif
