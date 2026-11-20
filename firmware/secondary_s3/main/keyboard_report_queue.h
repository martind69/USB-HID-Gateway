#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define S3_KEYBOARD_REPORT_SIZE 8U

typedef struct {
    uint8_t data[S3_KEYBOARD_REPORT_SIZE];
} s3_keyboard_report_t;

esp_err_t keyboard_report_queue_init(void);
bool keyboard_report_queue_enqueue(const uint8_t *data, size_t length);
bool keyboard_report_queue_receive(
    s3_keyboard_report_t *report,
    uint32_t timeout_ms);
uint32_t keyboard_report_queue_dropped(void);
uint32_t keyboard_report_queue_high_watermark(void);
