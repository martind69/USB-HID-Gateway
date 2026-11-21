#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

esp_err_t usb_device_init(void);
esp_err_t usb_device_report_enqueue(
    uint8_t interface_number,
    const uint8_t *report,
    size_t length);
void usb_device_release_all(void);
bool usb_device_mounted(void);
bool usb_device_interface_ready(uint8_t interface_number);
uint32_t usb_device_reports_sent(void);
uint32_t usb_device_reports_failed(void);
uint32_t usb_device_output_reports_received(void);
