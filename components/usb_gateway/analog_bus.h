#pragma once

#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * I2C master bus of the analog module (ADS7128, DAC7578).
 * Pins and clock: CONFIG_USB_GATEWAY_ANALOG_I2C_SDA / _SCL / _FREQ_HZ.
 * Internal pull-ups are disabled; the bus needs external pull-ups.
 * Replaces analog_i3c_bus (I3C peripheral in legacy-I2C mode).
 */

/* Creates the bus once; further calls return ESP_OK. */
esp_err_t analog_bus_init(void);

/* ESP_OK if a device acknowledges its address. */
esp_err_t analog_bus_probe(uint8_t address);

/* Probes the address and registers the device on success. */
esp_err_t analog_bus_add_device(uint8_t address, i2c_master_dev_handle_t *device);

/* Logs all acknowledging addresses 0x08..0x77 (boot diagnostic). */
void analog_bus_scan(void);

#ifdef __cplusplus
}
#endif
