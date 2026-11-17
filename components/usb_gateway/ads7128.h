#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/i3c_master_i2c.h"
#include "esp_err.h"

#define ADS7128_DEFAULT_ADDRESS 0x10U

typedef struct {
    uint8_t address;
    i3c_master_i2c_device_handle_t device;
    bool enabled;
} ads7128_t;

esp_err_t ads7128_init(ads7128_t *device, uint8_t address);
esp_err_t ads7128_read_channel(
    ads7128_t *device,
    uint8_t channel,
    uint16_t *code);
