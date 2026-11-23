#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* DAC7578 8-channel 12-bit DAC. */

#define DAC7578_DEFAULT_ADDRESS 0x4CU

typedef struct {
    uint8_t address;
    i2c_master_dev_handle_t device;
    bool enabled;
} dac7578_t;

/* Probes the device; ESP_ERR_NOT_FOUND: no acknowledge at address. */
esp_err_t dac7578_init(dac7578_t *device, uint8_t address);

esp_err_t dac7578_write_channel(
    dac7578_t *device,
    uint8_t channel,
    uint16_t code,
    bool update_now);

/* Writes both input registers, then updates all outputs simultaneously. */
esp_err_t dac7578_write_pair(
    dac7578_t *device,
    uint8_t channel_x,
    uint8_t channel_y,
    uint16_t x,
    uint16_t y);

#ifdef __cplusplus
}
#endif
