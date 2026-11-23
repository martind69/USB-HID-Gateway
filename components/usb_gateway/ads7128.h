#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ADS7128 8-channel 12-bit ADC, manual mode, all pins analog inputs. */

#define ADS7128_DEFAULT_ADDRESS 0x10U

typedef struct {
    uint8_t address;
    i2c_master_dev_handle_t device;
    bool enabled;
} ads7128_t;

/* ESP_ERR_NOT_FOUND: no acknowledge at address. */
esp_err_t ads7128_init(ads7128_t *device, uint8_t address);

esp_err_t ads7128_read_channel(ads7128_t *device, uint8_t channel, uint16_t *code);

esp_err_t ads7128_read_pair(
    ads7128_t *device,
    uint8_t channel_x,
    uint8_t channel_y,
    uint16_t *x,
    uint16_t *y);

#ifdef __cplusplus
}
#endif
