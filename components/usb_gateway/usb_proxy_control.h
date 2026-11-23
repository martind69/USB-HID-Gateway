#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    USB_PROXY_REPORT_OUTPUT = 2,
    USB_PROXY_REPORT_FEATURE = 3,
} usb_proxy_report_type_t;

/* data contains the complete report including a non-zero Report ID. */
typedef esp_err_t (*usb_proxy_host_set_report_fn)(
    uint8_t interface_number,
    uint8_t report_id,
    usb_proxy_report_type_t report_type,
    const uint8_t *data,
    size_t length);

typedef esp_err_t (*usb_proxy_host_get_report_fn)(
    uint8_t interface_number,
    uint8_t report_id,
    usb_proxy_report_type_t report_type,
    uint8_t *data,
    size_t *length);

typedef esp_err_t (*usb_proxy_host_control_fn)(
    const uint8_t setup[8],
    uint8_t *data,
    size_t *length,
    bool direction_in);

/* Creates the request queue and the worker task that executes USB transfers. */
esp_err_t usb_proxy_control_init(void);

void usb_proxy_control_register_host(
    usb_proxy_host_set_report_fn set_fn,
    usb_proxy_host_get_report_fn get_fn,
    usb_proxy_host_control_fn control_fn);

/*
 * Called from the P4/S3 link task. Copies the payload into the worker queue
 * and returns immediately; it never executes a USB transfer itself.
 */
esp_err_t usb_proxy_control_from_s3(
    uint8_t message_type,
    const uint8_t *payload,
    size_t length);

#ifdef __cplusplus
}
#endif
