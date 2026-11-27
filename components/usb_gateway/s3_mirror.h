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
 * P4 side of the S3 mirror (target "s3" in gateway.json).
 *
 * Collects the descriptors of the assigned device and transfers them to the
 * S3 as a mirror session (BEGIN, DESCRIPTOR chunks, COMMIT). Input reports
 * are forwarded unchanged afterwards. The session is re-sent automatically
 * when the S3 restarts (MIRROR_REQUEST) or reports that it has no session.
 * The S3 statistics, including stalled GET_REPORT requests, are polled
 * periodically and logged with CONFIG_USB_GATEWAY_CAPTURE_ENABLE.
 *
 * address = USB host address of the real device (= session id).
 */

/* Call after p4_s3_link_start(). Resets any session left on the S3. */
esp_err_t s3_mirror_init(void);

/* Returns ESP_ERR_NOT_SUPPORTED if the device cannot be mirrored. */
esp_err_t s3_mirror_begin(
    uint8_t address,
    const usb_device_desc_t *device_descriptor,
    const usb_config_desc_t *configuration_descriptor,
    const usb_device_info_t *device_info);

void s3_mirror_set_report_descriptor(
    uint8_t address,
    uint8_t interface_number,
    const uint8_t *descriptor,
    size_t length);

/* Non-blocking; returns false if no confirmed session or queue full. */
bool s3_mirror_input(
    uint8_t address,
    uint8_t interface_number,
    const uint8_t *report,
    size_t length);

/* Ends the session if address is the active one; no-op otherwise. */
void s3_mirror_end(uint8_t address);

#ifdef __cplusplus
}
#endif
