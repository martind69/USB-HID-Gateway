#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ADS7128 8-channel 12-bit ADC.
 *
 * The converter runs autonomously after initialization:
 *     - all 8 channels in ascending auto sequence
 *     - raw conversion rate: 31.3 kSPS (OSC_SEL=0, CLK_DIV=10)
 *     - hardware averaging: 4 samples/channel (OSR=2)
 *     - effective averaged update rate: about 978 S/s/channel
 *     - RECENT_CHx holds the 16-bit averaged result
 *
 * The gateway API remains 12 bit (0..4095): ads7128_read_channel() returns
 * bits [15:4] of the hardware-averaged result, so all existing calibration,
 * learning and DAC mapping remain compatible.
 *
 * Analog input network used for this operating point:
 *     Rseries = 100 kOhm, Cext = 4.7 nF directly at AIN
 *     tau = 470 us, fc about 339 Hz
 *
 * Safety: all eight pins share their node with a DAC7578 output. The driver
 * never configures an ADS7128 pin as a digital output:
 *     - PIN_CFG, GPIO_CFG and GPO_DRIVE_CFG are written with 0 only
 *     - any nonzero write to those registers is rejected
 *     - ads7128_verify_inputs() reads the three registers back during init
 */
#define ADS7128_DEFAULT_ADDRESS 0x10U

typedef struct {
    uint8_t address;
    i2c_master_dev_handle_t device;
    bool present;
    bool enabled;
} ads7128_t;

typedef struct {
    uint8_t pin_cfg;
    uint8_t gpio_cfg;
    uint8_t gpo_drive_cfg;
} ads7128_pin_config_t;

/*
 * ESP_ERR_NOT_FOUND: no acknowledge at address.
 * ESP_ERR_INVALID_STATE: present, but pin configuration is not all analog inputs.
 */
esp_err_t ads7128_init(ads7128_t *device, uint8_t address);

/*
 * Reads PIN_CFG, GPIO_CFG and GPO_DRIVE_CFG.
 * ESP_OK only if all are 0 (all pins analog inputs, no output configured).
 * config may be NULL; on mismatch it contains the read values.
 */
esp_err_t ads7128_verify_inputs(
    ads7128_t *device,
    ads7128_pin_config_t *config);

/*
 * Returns the most recent four-sample hardware average as a 12-bit code.
 * Reading does not trigger an ADC conversion; sampling continues autonomously.
 */
esp_err_t ads7128_read_channel(
    ads7128_t *device,
    uint8_t channel,
    uint16_t *code);

esp_err_t ads7128_read_pair(
    ads7128_t *device,
    uint8_t channel_x,
    uint8_t channel_y,
    uint16_t *x,
    uint16_t *y);

#ifdef __cplusplus
}
#endif
