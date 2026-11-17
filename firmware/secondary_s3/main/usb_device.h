#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

esp_err_t usb_device_init(void);
bool usb_device_is_mounted(void);
bool usb_device_send_keyboard_report(const uint8_t report[8]);
bool usb_device_send_vendor_report(uint8_t report_id,
	const uint8_t *payload, size_t payload_length);
