#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/i3c_master_i2c.h"
#include "esp_err.h"

#define DAC7578_DEFAULT_ADDRESS 0x4CU

typedef struct {
    uint8_t address;
    i3c_master_i2c_device_handle_t device;
    bool enabled;
} dac7578_t;

esp_err_t dac7578_init(dac7578_t *device, uint8_t address);

esp_err_t dac7578_write_channel(
    dac7578_t *device,
    uint8_t channel,
    uint16_t code,
    bool update_now);

esp_err_t dac7578_write_pair(
    dac7578_t *device,
    uint8_t channel_x,
    uint8_t channel_y,
    uint16_t x,
    uint16_t y);
