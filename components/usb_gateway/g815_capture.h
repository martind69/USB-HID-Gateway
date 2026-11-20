#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define G815_CAPTURE_MAX_INTERFACES 16U

typedef enum {
    G815_CAPTURE_EVENT_ATTACH = 1,
    G815_CAPTURE_EVENT_DETACH = 2,
    G815_CAPTURE_EVENT_INPUT_REPORT = 3,
    G815_CAPTURE_EVENT_OUTPUT_REPORT = 4,
    G815_CAPTURE_EVENT_FEATURE_GET = 5,
    G815_CAPTURE_EVENT_FEATURE_SET = 6,
    G815_CAPTURE_EVENT_CONTROL_SETUP = 7,
    G815_CAPTURE_EVENT_CONTROL_DATA_IN = 8,
    G815_CAPTURE_EVENT_CONTROL_DATA_OUT = 9,
    G815_CAPTURE_EVENT_ERROR = 10,
} g815_capture_event_type_t;

typedef struct {
    uint8_t bm_request_type;
    uint8_t b_request;
    uint16_t w_value;
    uint16_t w_index;
    uint16_t w_length;
} g815_capture_setup_packet_t;

typedef struct {
    uint16_t vendor_id;
    uint16_t product_id;
    uint16_t bcd_device;
    uint8_t address;
    uint8_t configuration_value;
    uint8_t interface_count;
    uint8_t reserved;
} g815_capture_device_info_t;

esp_err_t g815_capture_init(void);
esp_err_t g815_capture_start(const g815_capture_device_info_t *device);
esp_err_t g815_capture_stop(void);
bool g815_capture_is_active(void);

esp_err_t g815_capture_write_device_descriptor(
    const uint8_t *descriptor,
    size_t length);

esp_err_t g815_capture_write_configuration_descriptor(
    const uint8_t *descriptor,
    size_t length);

esp_err_t g815_capture_write_report_descriptor(
    uint8_t interface_number,
    const uint8_t *descriptor,
    size_t length);

esp_err_t g815_capture_write_string_descriptor(
    uint8_t string_index,
    uint16_t language_id,
    const uint8_t *descriptor,
    size_t length);

esp_err_t g815_capture_event(
    g815_capture_event_type_t type,
    uint8_t interface_number,
    uint8_t report_id,
    uint8_t endpoint,
    const void *data,
    size_t length,
    esp_err_t result);

esp_err_t g815_capture_control(
    g815_capture_event_type_t type,
    const g815_capture_setup_packet_t *setup,
    const void *data,
    size_t length,
    esp_err_t result);

uint32_t g815_capture_dropped(void);
const char *g815_capture_session_path(void);
