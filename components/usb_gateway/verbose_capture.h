#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
esp_err_t verbose_capture_init(void);esp_err_t verbose_capture_start(uint32_t duration_s);esp_err_t verbose_capture_stop(void);bool verbose_capture_write(uint16_t source,uint16_t event,uint32_t sequence,const void*payload,size_t length);uint32_t verbose_capture_dropped(void);
