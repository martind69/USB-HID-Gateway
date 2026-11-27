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
 * P4 USB1P1 full-speed device port: transparent mirror of the device
 * assigned to target "p4_fs" in /sdcard/config/gateway.json.
 * The mirror itself is component usb_mirror (shared with the S3); this
 * module adds the P4 specifics: PHY routing, host descriptor conversion and
 * the EP0 path to the real device.
 *
 * address = USB host address of the real device (= mirror session id).
 */

void usb_device_init(void);
void usb_device_process(void);

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
