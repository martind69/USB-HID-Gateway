#pragma once
#include <stdbool.h>
#include "esp_err.h"
esp_err_t analog_control_gpio_init(void);esp_err_t analog_sensor_power_set(bool enabled);
