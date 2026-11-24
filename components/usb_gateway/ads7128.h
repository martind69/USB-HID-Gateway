#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ADS7128 8-channel 12-bit ADC, manual mode.
 *
 * Safety: all eight pins share their node with a DAC7578 output. The driver
 * therefore never configures a pin as digital output:
 *     - PIN_CFG, GPIO_CFG and GPO_DRIVE_CFG are written with 0 only,
 *       any other value is rejected with ESP_ERR_NOT_ALLOWED
 *     - ads7128_verify_inputs() reads these registers back (init)
 */
#define ADS7128_DEFAULT_ADDRESS 0x10U

typedef struct {
    uint8_t address;
    i2c_master_dev_handle_t device;
    bool present;       /* acknowledged on the bus, registers readable */
    bool enabled;       /* configured and verified, conversions allowed */
} ads7128_t;

typedef struct {
    uint8_t pin_cfg;
    uint8_t gpio_cfg;
    uint8_t gpo_drive_cfg;
} ads7128_pin_config_t;

/*
 * ESP_ERR_NOT_FOUND: no acknowledge at address.
 * ESP_ERR_INVALID_STATE: present, but pin configuration not "all analog inputs".
 */
esp_err_t ads7128_init(ads7128_t *device, uint8_t address);

/*
 * Reads PIN_CFG, GPIO_CFG and GPO_DRIVE_CFG.
 * ESP_OK only if all are 0 (all pins analog inputs, no output configured).
 * config may be NULL; on mismatch it contains the read values.
 */
esp_err_t ads7128_verify_inputs(ads7128_t *device, ads7128_pin_config_t *config);

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
