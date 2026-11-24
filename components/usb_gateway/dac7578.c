#include "dac7578.h"

#include <stddef.h>

#include "analog_bus.h"
#include "sdkconfig.h"

#define DAC7578_COMMAND_WRITE_INPUT     0x00U
#define DAC7578_COMMAND_GLOBAL_LDAC     0x20U
#define DAC7578_COMMAND_WRITE_UPDATE    0x30U
#define DAC7578_COMMAND_POWER           0x40U

/*
 * Power-down data word (DAC7578 data sheet, power-down register):
 *     DB14..DB13  PD1..PD0     00 up, 01 1 kOhm, 10 100 kOhm, 11 high-Z
 *     DB12..DB5   DAC H..A     1 = channel affected
 * The same layout is used for the optional register plausibility check.
 * A mismatch does not prove that the analog output is not high-impedance.
 */
#define DAC7578_PD_SHIFT                13U
#define DAC7578_PD_MASK                 0x3U
#define DAC7578_CHANNEL_SHIFT           5U
#define DAC7578_CHANNEL_MASK            0xFFU

#define DAC7578_TIMEOUT_MS              20

/* Channel bits: A = bit 0 -> DB5 ... H = bit 7 -> DB12. */
static uint16_t power_word(uint8_t channel_mask, dac7578_power_mode_t mode)
{
    return (uint16_t)(
        (((uint16_t)mode & DAC7578_PD_MASK) << DAC7578_PD_SHIFT) |
        ((uint16_t)channel_mask << DAC7578_CHANNEL_SHIFT));
}

static esp_err_t send(dac7578_t *device, uint8_t command, uint16_t word)
{
    const uint8_t data[3] = {
        command,
        (uint8_t)(word >> 8U),
        (uint8_t)(word & 0xFFU),
    };
    return i2c_master_transmit(device->device, data, sizeof(data), DAC7578_TIMEOUT_MS);
}

/* 12-bit code, MSB-aligned in 16 bits. */
static uint16_t code_word(uint16_t code)
{
    return (uint16_t)(code << 4U);
}

static esp_err_t check_ready(const dac7578_t *device)
{
#if !CONFIG_USB_GATEWAY_DAC7578_ENABLE
    (void)device;
    return ESP_ERR_NOT_SUPPORTED;
#else
    return (device->enabled && device->device != NULL) ? ESP_OK : ESP_ERR_INVALID_STATE;
#endif
}

esp_err_t dac7578_init(dac7578_t *device, uint8_t address)
{
    if (device == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *device = (dac7578_t) {
        .address = address,
    };

#if !CONFIG_USB_GATEWAY_DAC7578_ENABLE
    return ESP_ERR_NOT_SUPPORTED;
#else
    const esp_err_t error = analog_bus_add_device(address, &device->device);
    if (error != ESP_OK) {
        return error;
    }
    device->enabled = true;

    /* POR drives zero/mid scale: release the outputs as early as possible. */
    return dac7578_set_power(device, DAC7578_ALL_CHANNELS, DAC7578_POWER_DOWN_HIGH_Z);
#endif
}

esp_err_t dac7578_set_power(
    dac7578_t *device,
    uint8_t channel_mask,
    dac7578_power_mode_t mode)
{
    if (device == NULL || (unsigned)mode > (unsigned)DAC7578_POWER_DOWN_HIGH_Z) {
        return ESP_ERR_INVALID_ARG;
    }

    const esp_err_t error = check_ready(device);
    if (error != ESP_OK) {
        return error;
    }
    return send(device, DAC7578_COMMAND_POWER, power_word(channel_mask, mode));
}

esp_err_t dac7578_read_power(dac7578_t *device, uint16_t *raw)
{
    if (device == NULL || raw == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t error = check_ready(device);
    if (error != ESP_OK) {
        return error;
    }

    /* Read: command byte, repeated start, two data bytes. */
    const uint8_t command = DAC7578_COMMAND_POWER;
    uint8_t data[2] = { 0U, 0U };
    error = i2c_master_transmit_receive(
        device->device, &command, 1U, data, sizeof(data), DAC7578_TIMEOUT_MS);
    if (error != ESP_OK) {
        return error;
    }

    *raw = (uint16_t)(((uint16_t)data[0] << 8U) | data[1]);
    return ESP_OK;
}

esp_err_t dac7578_verify_power(
    dac7578_t *device,
    uint8_t channel_mask,
    dac7578_power_mode_t mode,
    uint16_t *raw)
{
    uint16_t value = 0U;
    const esp_err_t error = dac7578_read_power(device, &value);
    if (raw != NULL) {
        *raw = value;
    }
    if (error != ESP_OK) {
        return error;
    }

    const uint16_t relevant =
        (uint16_t)((DAC7578_PD_MASK << DAC7578_PD_SHIFT) |
            (DAC7578_CHANNEL_MASK << DAC7578_CHANNEL_SHIFT));
    return (value & relevant) == power_word(channel_mask, mode)
        ? ESP_OK
        : ESP_ERR_INVALID_STATE;
}

esp_err_t dac7578_write_channel(
    dac7578_t *device,
    uint8_t channel,
    uint16_t code,
    bool update_now)
{
    if (device == NULL || channel > 7U || code > 0x0FFFU) {
        return ESP_ERR_INVALID_ARG;
    }

    const esp_err_t error = check_ready(device);
    if (error != ESP_OK) {
        return error;
    }

    const uint8_t command = (uint8_t)(
        (update_now ? DAC7578_COMMAND_WRITE_UPDATE : DAC7578_COMMAND_WRITE_INPUT) | channel);
    return send(device, command, code_word(code));
}

esp_err_t dac7578_write_pair(
    dac7578_t *device,
    uint8_t channel_x,
    uint8_t channel_y,
    uint16_t x,
    uint16_t y)
{
    if (channel_x == channel_y) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t error = dac7578_write_channel(device, channel_x, x, false);
    if (error != ESP_OK) {
        return error;
    }
    error = dac7578_write_channel(device, channel_y, y, false);
    if (error != ESP_OK) {
        return error;
    }
    return send(device, DAC7578_COMMAND_GLOBAL_LDAC, 0U);
}
