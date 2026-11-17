#pragma once

#include <stdint.h>

#include "driver/i3c_master.h"
#include "driver/i3c_master_i2c.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t analog_i3c_bus_init(void);

esp_err_t analog_i3c_add_legacy_i2c(
    uint8_t address,
    i3c_master_i2c_device_handle_t *device);

#ifdef __cplusplus
}
#endif
