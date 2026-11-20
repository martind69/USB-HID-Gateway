#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

esp_err_t g815_report_validate(uint8_t interface_number,
                              const uint8_t *report,
                              size_t report_length);
bool g815_report_is_release(uint8_t interface_number,
                            const uint8_t *report,
                            size_t report_length);
