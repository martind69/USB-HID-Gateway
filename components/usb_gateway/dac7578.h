#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * DAC7578 8-channel 12-bit DAC.
 *
 * The outputs share their node with the Hall sensor outputs (about 16 kOhm)
 * and the ADS7128 inputs. dac7578_init() switches all outputs to high-Z right
 * after the device acknowledges. Power-up is done only by analog_engine when
 * entering GENERATE.
 */
#define DAC7578_DEFAULT_ADDRESS     0x4CU
#define DAC7578_ALL_CHANNELS        0xFFU

typedef enum {
    DAC7578_POWER_UP = 0,
    DAC7578_POWER_DOWN_1K = 1,
    DAC7578_POWER_DOWN_100K = 2,
    DAC7578_POWER_DOWN_HIGH_Z = 3,
} dac7578_power_mode_t;

typedef struct {
    uint8_t address;
    i2c_master_dev_handle_t device;
    bool enabled;       /* acknowledged on the bus */
} dac7578_t;

/*
 * Probes the device and sets all outputs to high-Z.
 * ESP_ERR_NOT_FOUND: no acknowledge at address.
 */
esp_err_t dac7578_init(dac7578_t *device, uint8_t address);

/* channel_mask: bit n = channel n (A = bit 0 ... H = bit 7). */
esp_err_t dac7578_set_power(
    dac7578_t *device,
    uint8_t channel_mask,
    dac7578_power_mode_t mode);

/*
 * Raw 16-bit register read-back for diagnostics. This does not verify the
 * physical high-impedance state of the analog outputs.
 */
esp_err_t dac7578_read_power(dac7578_t *device, uint16_t *raw);

/*
 * Optional register plausibility check. ESP_ERR_INVALID_STATE means only that
 * the returned register value differs from the requested mode/channel mask;
 * it is not evidence that the analog output driver is electrically active.
 * raw (may be NULL) receives the value.
 */
esp_err_t dac7578_verify_power(
    dac7578_t *device,
    uint8_t channel_mask,
    dac7578_power_mode_t mode,
    uint16_t *raw);

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
