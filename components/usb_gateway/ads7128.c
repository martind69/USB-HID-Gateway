#include "ads7128.h"

#include <stddef.h>

#include "analog_bus.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#define ADS7128_OPCODE_READ_ONE          0x10U
#define ADS7128_OPCODE_WRITE_ONE         0x08U
#define ADS7128_OPCODE_READ_CONTINUOUS   0x30U

#define ADS7128_REG_GENERAL_CFG          0x01U
#define ADS7128_REG_DATA_CFG             0x02U
#define ADS7128_REG_OSR_CFG              0x03U
#define ADS7128_REG_OPMODE_CFG           0x04U
#define ADS7128_REG_PIN_CFG              0x05U
#define ADS7128_REG_GPIO_CFG             0x07U
#define ADS7128_REG_GPO_DRIVE_CFG        0x09U
#define ADS7128_REG_SEQUENCE_CFG         0x10U
#define ADS7128_REG_AUTO_SEQ_CH_SEL      0x12U
#define ADS7128_REG_RECENT_CH0_LSB       0xA0U

#define ADS7128_GENERAL_CFG_RESET        0x01U
#define ADS7128_GENERAL_CFG_STATS_EN     0x20U
#define ADS7128_RESET_DELAY_MS           2U
#define ADS7128_STARTUP_DELAY_MS         2U
#define ADS7128_TIMEOUT_MS               20

/*
 * Autonomous sampling configuration for the 100 kOhm + 4.7 nF input network.
 *
 * OSC_SEL = 0, CLK_DIV = 1010b -> 31.3 kSPS raw conversion rate.
 * OSR = 010b                  -> average 4 conversions per channel.
 * 8 enabled channels          -> about 978 averaged updates/s per channel.
 *
 * External RC: tau = 470 us, fc = about 339 Hz. At the design slew rate of
 * 30 V/s (1.5 V in 50 ms), the RC tracking error is about 14.1 mV.
 */
#define ADS7128_OSR_4_SAMPLES            0x02U
#define ADS7128_OPMODE_AUTON_31K3_SPS    0x2AU
#define ADS7128_AUTO_SEQ_ALL_CHANNELS    0xFFU
#define ADS7128_SEQUENCE_AUTO_START      0x11U

/* Registers that can turn a pin into a digital output. */
static bool is_pin_function_register(uint8_t reg)
{
    return reg == ADS7128_REG_PIN_CFG ||
        reg == ADS7128_REG_GPIO_CFG ||
        reg == ADS7128_REG_GPO_DRIVE_CFG;
}

static esp_err_t write_register(ads7128_t *device, uint8_t reg, uint8_t value)
{
    if (is_pin_function_register(reg) && value != 0U) {
        return ESP_ERR_NOT_ALLOWED;
    }

    const uint8_t command[3] = {
        ADS7128_OPCODE_WRITE_ONE,
        reg,
        value,
    };
    return i2c_master_transmit(
        device->device,
        command,
        sizeof(command),
        ADS7128_TIMEOUT_MS);
}

/* Single register read: write frame {opcode, address}, then a read frame. */
static esp_err_t read_register(ads7128_t *device, uint8_t reg, uint8_t *value)
{
    const uint8_t command[2] = {
        ADS7128_OPCODE_READ_ONE,
        reg,
    };
    const esp_err_t error = i2c_master_transmit(
        device->device,
        command,
        sizeof(command),
        ADS7128_TIMEOUT_MS);
    if (error != ESP_OK) {
        return error;
    }
    return i2c_master_receive(
        device->device,
        value,
        1U,
        ADS7128_TIMEOUT_MS);
}

/* Read consecutive registers starting at first_reg. */
static esp_err_t read_register_block(
    ads7128_t *device,
    uint8_t first_reg,
    uint8_t *data,
    size_t length)
{
    if (data == NULL || length == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    const uint8_t command[2] = {
        ADS7128_OPCODE_READ_CONTINUOUS,
        first_reg,
    };
    const esp_err_t error = i2c_master_transmit(
        device->device,
        command,
        sizeof(command),
        ADS7128_TIMEOUT_MS);
    if (error != ESP_OK) {
        return error;
    }
    return i2c_master_receive(
        device->device,
        data,
        length,
        ADS7128_TIMEOUT_MS);
}

esp_err_t ads7128_verify_inputs(
    ads7128_t *device,
    ads7128_pin_config_t *config)
{
    if (device == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
#if !CONFIG_USB_GATEWAY_ADS7128_ENABLE
    (void)config;
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (!device->present || device->device == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    ads7128_pin_config_t read = { 0 };
    esp_err_t error = read_register(device, ADS7128_REG_PIN_CFG, &read.pin_cfg);
    if (error == ESP_OK) {
        error = read_register(device, ADS7128_REG_GPIO_CFG, &read.gpio_cfg);
    }
    if (error == ESP_OK) {
        error = read_register(
            device,
            ADS7128_REG_GPO_DRIVE_CFG,
            &read.gpo_drive_cfg);
    }
    if (config != NULL) {
        *config = read;
    }
    if (error != ESP_OK) {
        return error;
    }
    return (read.pin_cfg == 0U &&
            read.gpio_cfg == 0U &&
            read.gpo_drive_cfg == 0U)
        ? ESP_OK
        : ESP_ERR_INVALID_STATE;
#endif
}

static esp_err_t configure_autonomous_sampling(ads7128_t *device)
{
    /* Stop/reset all conversion-side functions before reconfiguration. */
    esp_err_t error = write_register(device, ADS7128_REG_SEQUENCE_CFG, 0x00U);
    if (error != ESP_OK) {
        return error;
    }

    /* Four-sample hardware averaging, common to all analog channels. */
    error = write_register(device, ADS7128_REG_OSR_CFG, ADS7128_OSR_4_SAMPLES);
    if (error != ESP_OK) {
        return error;
    }

    /* Keep the most recent 16-bit averaged result in RECENT_CHx registers. */
    error = write_register(
        device,
        ADS7128_REG_GENERAL_CFG,
        ADS7128_GENERAL_CFG_STATS_EN);
    if (error != ESP_OK) {
        return error;
    }

    /* Enable all eight analog inputs in the autonomous scan. */
    error = write_register(
        device,
        ADS7128_REG_AUTO_SEQ_CH_SEL,
        ADS7128_AUTO_SEQ_ALL_CHANNELS);
    if (error != ESP_OK) {
        return error;
    }

    /* Autonomous conversion, high-speed oscillator, CLK_DIV = 10 -> 31.3 kSPS. */
    error = write_register(
        device,
        ADS7128_REG_OPMODE_CFG,
        ADS7128_OPMODE_AUTON_31K3_SPS);
    if (error != ESP_OK) {
        return error;
    }

    /* Auto sequence + sequence start. */
    error = write_register(
        device,
        ADS7128_REG_SEQUENCE_CFG,
        ADS7128_SEQUENCE_AUTO_START);
    if (error != ESP_OK) {
        return error;
    }

    /* One complete 8-channel, OSR=4 scan takes about 1.02 ms. */
    vTaskDelay(pdMS_TO_TICKS(ADS7128_STARTUP_DELAY_MS));
    return ESP_OK;
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
    esp_err_t error = analog_bus_add_device(address, &device->device);
    if (error != ESP_OK) {
        return error;
    }
    device->present = true;

    error = write_register(
        device,
        ADS7128_REG_GENERAL_CFG,
        ADS7128_GENERAL_CFG_RESET);
    if (error != ESP_OK) {
        return error;
    }
    vTaskDelay(pdMS_TO_TICKS(ADS7128_RESET_DELAY_MS));

    /* Reset default is all analog inputs; write it explicitly for safety. */
    static const uint8_t PIN_FUNCTION_REGISTERS[] = {
        ADS7128_REG_PIN_CFG,
        ADS7128_REG_GPIO_CFG,
        ADS7128_REG_GPO_DRIVE_CFG,
    };
    for (size_t index = 0U;
         index < sizeof(PIN_FUNCTION_REGISTERS) / sizeof(PIN_FUNCTION_REGISTERS[0]);
         ++index) {
        error = write_register(device, PIN_FUNCTION_REGISTERS[index], 0x00U);
        if (error != ESP_OK) {
            return error;
        }
    }

    /* No appended status bits; RECENT_CHx contains the 16-bit averaged value. */
    error = write_register(device, ADS7128_REG_DATA_CFG, 0x00U);
    if (error != ESP_OK) {
        return error;
    }

    error = ads7128_verify_inputs(device, NULL);
    if (error != ESP_OK) {
        return error;
    }

    error = configure_autonomous_sampling(device);
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
    if (device == NULL || code == NULL || channel > 7U) {
        return ESP_ERR_INVALID_ARG;
    }

#if !CONFIG_USB_GATEWAY_ADS7128_ENABLE
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (!device->enabled || device->device == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    /*
     * RECENT_CHx_LSB/MSB are updated by the autonomous statistics engine.
     * Read both bytes as one continuous block. The averaged result is 16 bit;
     * its upper 12 bits retain the existing gateway 0..4095 code interface.
     */
    const uint8_t first_reg = (uint8_t)(
        ADS7128_REG_RECENT_CH0_LSB + (uint8_t)(2U * channel));
    uint8_t result[2] = { 0U, 0U };
    const esp_err_t error = read_register_block(
        device,
        first_reg,
        result,
        sizeof(result));
    if (error != ESP_OK) {
        return error;
    }

    const uint16_t averaged = (uint16_t)(
        ((uint16_t)result[1] << 8U) | (uint16_t)result[0]);
    *code = (uint16_t)(averaged >> 4U);
    return ESP_OK;
#endif
}

esp_err_t ads7128_read_pair(
    ads7128_t *device,
    uint8_t channel_x,
    uint8_t channel_y,
    uint16_t *x,
    uint16_t *y)
{
    if (x == NULL || y == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const esp_err_t error = ads7128_read_channel(device, channel_x, x);
    if (error != ESP_OK) {
        return error;
    }
    return ads7128_read_channel(device, channel_y, y);
}
