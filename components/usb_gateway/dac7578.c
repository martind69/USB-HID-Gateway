#include "dac7578.h"

#include <stddef.h>

#include "analog_bus.h"

#include "sdkconfig.h"

#define DAC7578_COMMAND_WRITE_INPUT     0x00U
#define DAC7578_COMMAND_GLOBAL_LDAC     0x20U
#define DAC7578_COMMAND_WRITE_UPDATE    0x30U
#define DAC7578_TIMEOUT_MS              20

static esp_err_t send(dac7578_t *device, uint8_t command, uint16_t code)
{
    /* 12-bit code, MSB-aligned in 16 bits. */
    const uint8_t data[3] = {
        command,
        (uint8_t)(code >> 4U),
        (uint8_t)(code << 4U),
    };
    return i2c_master_transmit(device->device, data, sizeof(data), DAC7578_TIMEOUT_MS);
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
    if (error == ESP_OK) {
        device->enabled = true;
    }
    return error;
#endif
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
#if !CONFIG_USB_GATEWAY_DAC7578_ENABLE
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (!device->enabled || device->device == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    const uint8_t command = (uint8_t)(
        (update_now ? DAC7578_COMMAND_WRITE_UPDATE : DAC7578_COMMAND_WRITE_INPUT) | channel);
    return send(device, command, code);
#endif
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
