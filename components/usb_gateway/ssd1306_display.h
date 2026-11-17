#pragma once
#include "esp_err.h"
typedef struct{char mode[12],profile[17],detail[22];uint8_t progress_percent;bool usb_host_ok,usb_device_ok,analog_ok;}ssd1306_status_view_t;esp_err_t ssd1306_display_init(void);void ssd1306_display_set_view(const ssd1306_status_view_t*v);
