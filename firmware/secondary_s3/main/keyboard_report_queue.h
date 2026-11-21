#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define S3_HID_REPORT_MAX_SIZE 64U

typedef struct {
    uint8_t interface_number;
    uint8_t length;
    uint8_t data[S3_HID_REPORT_MAX_SIZE];
} s3_hid_report_t;

esp_err_t keyboard_report_queue_init(void);
esp_err_t keyboard_report_queue_enqueue(
    uint8_t interface_number,
    const uint8_t *data,
    size_t length);
bool keyboard_report_queue_receive(
    s3_hid_report_t *report,
    uint32_t timeout_ms);
void keyboard_report_queue_clear(void);
uint32_t keyboard_report_queue_dropped(void);
uint32_t keyboard_report_queue_high_watermark(void);
