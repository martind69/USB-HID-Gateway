#include "analog_vector_io.h"

#include <stddef.h>

#include "esp_log.h"

static const char *TAG = "analog_io";

esp_err_t analog_vector_io_init(analog_vector_io_t *io)
{
    if (io == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    for (uint8_t index = 0U; index < 8U; ++index) {
        io->dac_channel[index] = index;
        io->adc_channel[index] = (uint8_t)(7U - index);
    }
    const esp_err_t adc = ads7128_init(&io->adc, ADS7128_DEFAULT_ADDRESS);
    const esp_err_t dac = dac7578_init(&io->dac, DAC7578_DEFAULT_ADDRESS);

    /* Report each converter separately, so a failing device is identified. */
    if (adc == ESP_OK) {
        ESP_LOGI(TAG, "ADS7128 ready at 0x%02X", (unsigned)ADS7128_DEFAULT_ADDRESS);
    } else {
        ESP_LOGE(TAG, "ADS7128 at 0x%02X: %s",
            (unsigned)ADS7128_DEFAULT_ADDRESS, esp_err_to_name(adc));
    }
    if (dac == ESP_OK) {
        ESP_LOGI(TAG, "DAC7578 ready at 0x%02X", (unsigned)DAC7578_DEFAULT_ADDRESS);
    } else {
        ESP_LOGE(TAG, "DAC7578 at 0x%02X: %s",
            (unsigned)DAC7578_DEFAULT_ADDRESS, esp_err_to_name(dac));
    }

    if (adc != ESP_OK && adc != ESP_ERR_NOT_SUPPORTED) {
        return adc;
    }
    if (dac != ESP_OK && dac != ESP_ERR_NOT_SUPPORTED) {
        return dac;
    }
    return (adc == ESP_ERR_NOT_SUPPORTED && dac == ESP_ERR_NOT_SUPPORTED)
        ? ESP_ERR_NOT_SUPPORTED
        : ESP_OK;
}

esp_err_t analog_vector_io_read_channel(
    analog_vector_io_t *io,
    analog_hid_channel_t channel,
    uint16_t *code)
{
    if (io == NULL || code == NULL || channel >= ANALOG_HID_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    return ads7128_read_channel(&io->adc, io->adc_channel[channel], code);
}

esp_err_t analog_vector_io_write_channel(
    analog_vector_io_t *io,
    analog_hid_channel_t channel,
    uint16_t code)
{
    if (io == NULL || channel >= ANALOG_HID_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    return dac7578_write_channel(&io->dac, io->dac_channel[channel], code, true);
}
