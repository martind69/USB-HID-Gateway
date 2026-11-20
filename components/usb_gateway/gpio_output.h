#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#define GPIO_OUTPUT_COUNT 12U
esp_err_t gpio_output_init(void);
esp_err_t gpio_output_set(uint8_t output,bool state);
void gpio_output_all_off(void);
