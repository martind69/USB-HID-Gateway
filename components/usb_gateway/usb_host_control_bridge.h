#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "gateway_config.h"
#include "usb/usb_host.h"
#include "usb_proxy_control.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * EP0 bridge from a PC-side mirror to the real device on the P4 host port.
 * One device per mirror target (p4_fs, s3); no device identity in code.
 *
 * Requests from the S3 (usb_proxy_control) are bound to target s3.
 * Requests from the P4 mirror (usb_device.c) are bound to target p4_fs.
 * All calls block up to 1 s and must not run in a USB stack task.
 */

esp_err_t usb_host_control_bridge_init(usb_host_client_handle_t client_handle);

void usb_host_control_bridge_attach(
    gateway_port_target_t target,
    usb_device_handle_t device_handle,
    uint8_t device_address);

void usb_host_control_bridge_detach(uint8_t device_address);

/* data contains the complete report, including a non-zero Report ID. */
esp_err_t usb_host_control_bridge_set_report(
    gateway_port_target_t target,
    uint8_t interface_number,
    uint8_t report_id,
    usb_proxy_report_type_t report_type,
    const uint8_t *data,
    size_t length);

esp_err_t usb_host_control_bridge_get_report(
    gateway_port_target_t target,
    uint8_t interface_number,
    uint8_t report_id,
    usb_proxy_report_type_t report_type,
    uint8_t *data,
    size_t *length);

esp_err_t usb_host_control_bridge_control(
    gateway_port_target_t target,
    const uint8_t setup[8],
    uint8_t *data,
    size_t *length,
    bool direction_in);

#ifdef __cplusplus
}
#endif
