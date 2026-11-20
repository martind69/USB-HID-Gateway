#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

esp_err_t usb_device_init(void);
bool usb_device_keyboard_enqueue(const uint8_t *report, size_t length);
bool usb_device_mounted(void);
uint32_t usb_device_reports_sent(void);
uint32_t usb_device_reports_failed(void);
