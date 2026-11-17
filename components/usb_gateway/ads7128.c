#include "ads7128.h"

#include "analog_i3c_bus.h"
#include "sdkconfig.h"

#define ADS7128_OPCODE_WRITE_ONE 0x08U
#define ADS7128_REG_GENERAL_CFG 0x01U
#define ADS7128_REG_DATA_CFG 0x02U
#define ADS7128_REG_OPMODE_CFG 0x04U
#define ADS7128_REG_CHANNEL_SEL 0x11U
#define ADS7128_GENERAL_CFG_RST 0x01U
#define ADS7128_TIMEOUT_MS 20

static esp_err_t ads7128_write_register(
    ads7128_t *device,
    uint8_t reg,
    uint8_t value)
{
    const uint8_t command[3] = {
        ADS7128_OPCODE_WRITE_ONE,
        reg,
        value,
    };

    return i3c_master_i2c_device_transmit(
        device->device,
        command,
        sizeof(command),
        ADS7128_TIMEOUT_MS);
}

esp_err_t ads7128_init(ads7128_t *device, uint8_t address)
{
    if (device == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *device = (ads7128_t) {
        .address = address,
    };

#if !CONFIG_USB_GATEWAY_ADS7128_ENABLE
    return ESP_ERR_NOT_SUPPORTED;
#else
    esp_err_t error = analog_i3c_add_legacy_i2c(
        address,
        &device->device);
    if (error != ESP_OK) {
        return error;
    }

    error = ads7128_write_register(
        device,
        ADS7128_REG_GENERAL_CFG,
        ADS7128_GENERAL_CFG_RST);
    if (error != ESP_OK) {
        return error;
    }

    error = ads7128_write_register(
        device,
        ADS7128_REG_DATA_CFG,
        0x00U);
    if (error != ESP_OK) {
        return error;
    }

    error = ads7128_write_register(
        device,
        ADS7128_REG_OPMODE_CFG,
        0x00U);
    if (error == ESP_OK) {
        device->enabled = true;
    }

    return error;
#endif
}

esp_err_t ads7128_read_channel(
    ads7128_t *device,
    uint8_t channel,
    uint16_t *code)
{
    if ((device == NULL) || (code == NULL) || (channel > 7U)) {
        return ESP_ERR_INVALID_ARG;
    }

#if !CONFIG_USB_GATEWAY_ADS7128_ENABLE
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (!device->enabled || (device->device == NULL)) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t error = ads7128_write_register(
        device,
        ADS7128_REG_CHANNEL_SEL,
        channel);
    if (error != ESP_OK) {
        return error;
    }

    uint8_t result[2] = { 0U, 0U };
    error = i3c_master_i2c_device_receive(
        device->device,
        result,
        sizeof(result),
        ADS7128_TIMEOUT_MS);
    if (error != ESP_OK) {
        return error;
    }

    *code = ((uint16_t)result[0] << 4U)
        | ((uint16_t)result[1] >> 4U);

    return ESP_OK;
#endif
}
