#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Device-independent USB capture session on the SD card (formerly
 * g815_capture). One session at a time, started for the device mirrored
 * by the S3.
 *
 * Session directory: /sdcard/captures/dev-<vid>-<pid>-<time>/
 *     manifest.txt                 device identity, record count
 *     device.bin                   device descriptor
 *     configuration.bin            configuration descriptor
 *     report-ifNN.bin              report descriptor per interface
 *     string-NN-LLLL.bin           string descriptors
 *     events.bin                   event records (decode: tools/usb_capture_decode.py)
 */

#define USB_CAPTURE_MAX_INTERFACES  16U

typedef enum {
    USB_CAPTURE_EVENT_ATTACH = 1,
    USB_CAPTURE_EVENT_DETACH = 2,
    USB_CAPTURE_EVENT_INPUT_REPORT = 3,
    USB_CAPTURE_EVENT_OUTPUT_REPORT = 4,
    USB_CAPTURE_EVENT_FEATURE_GET = 5,
    USB_CAPTURE_EVENT_FEATURE_SET = 6,
    USB_CAPTURE_EVENT_CONTROL_SETUP = 7,
    USB_CAPTURE_EVENT_CONTROL_DATA_IN = 8,
    USB_CAPTURE_EVENT_CONTROL_DATA_OUT = 9,
    USB_CAPTURE_EVENT_ERROR = 10,
} usb_capture_event_type_t;

typedef struct {
    uint8_t bm_request_type;
    uint8_t b_request;
    uint16_t w_value;
    uint16_t w_index;
    uint16_t w_length;
} usb_capture_setup_packet_t;

typedef struct {
    uint16_t vendor_id;
    uint16_t product_id;
    uint16_t bcd_device;
    uint8_t address;
    uint8_t configuration_value;
    uint8_t interface_count;
    uint8_t reserved;
} usb_capture_device_info_t;

esp_err_t usb_capture_init(void);
esp_err_t usb_capture_start(const usb_capture_device_info_t *device);
esp_err_t usb_capture_stop(void);
bool usb_capture_is_active(void);

esp_err_t usb_capture_write_device_descriptor(
    const uint8_t *descriptor,
    size_t length);

esp_err_t usb_capture_write_configuration_descriptor(
    const uint8_t *descriptor,
    size_t length);

esp_err_t usb_capture_write_report_descriptor(
    uint8_t interface_number,
    const uint8_t *descriptor,
    size_t length);

esp_err_t usb_capture_write_string_descriptor(
    uint8_t string_index,
    uint16_t language_id,
    const uint8_t *descriptor,
    size_t length);

/* report_id: 0 if unknown; the payload always contains the raw report. */
esp_err_t usb_capture_event(
    usb_capture_event_type_t type,
    uint8_t interface_number,
    uint8_t report_id,
    uint8_t endpoint,
    const void *data,
    size_t length,
    esp_err_t result);

esp_err_t usb_capture_control(
    usb_capture_event_type_t type,
    const usb_capture_setup_packet_t *setup,
    const void *data,
    size_t length,
    esp_err_t result);

uint32_t usb_capture_dropped(void);
const char *usb_capture_session_path(void);

#ifdef __cplusplus
}
#endif
