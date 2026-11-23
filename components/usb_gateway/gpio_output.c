#include "gpio_output.h"

#include "driver/gpio.h"
#include "sdkconfig.h"

#ifndef CONFIG_USB_GATEWAY_DIGITAL_OUTPUT_ACTIVE_LOW
#define CONFIG_USB_GATEWAY_DIGITAL_OUTPUT_ACTIVE_LOW 0
#endif

static const int s_gpio[GPIO_OUTPUT_COUNT] = {
    CONFIG_USB_GATEWAY_DIGITAL_OUTPUT_0_GPIO,
    CONFIG_USB_GATEWAY_DIGITAL_OUTPUT_1_GPIO,
    CONFIG_USB_GATEWAY_DIGITAL_OUTPUT_2_GPIO,
    CONFIG_USB_GATEWAY_DIGITAL_OUTPUT_3_GPIO,
    CONFIG_USB_GATEWAY_DIGITAL_OUTPUT_4_GPIO,
    CONFIG_USB_GATEWAY_DIGITAL_OUTPUT_5_GPIO,
    CONFIG_USB_GATEWAY_DIGITAL_OUTPUT_6_GPIO,
    CONFIG_USB_GATEWAY_DIGITAL_OUTPUT_7_GPIO,
    CONFIG_USB_GATEWAY_DIGITAL_OUTPUT_8_GPIO,
    CONFIG_USB_GATEWAY_DIGITAL_OUTPUT_9_GPIO,
    CONFIG_USB_GATEWAY_DIGITAL_OUTPUT_10_GPIO,
    CONFIG_USB_GATEWAY_DIGITAL_OUTPUT_11_GPIO,
};

static bool s_initialized;

static int level(bool active)
{
    return active != (CONFIG_USB_GATEWAY_DIGITAL_OUTPUT_ACTIVE_LOW != 0) ? 1 : 0;
}

bool gpio_output_is_configured(uint8_t index)
{
    return index < GPIO_OUTPUT_COUNT && s_gpio[index] >= 0;
}

esp_err_t gpio_output_init(void)
{
    uint64_t mask = 0U;
    for (uint8_t index = 0U; index < GPIO_OUTPUT_COUNT; ++index) {
        if (s_gpio[index] >= 0) {
            mask |= 1ULL << (unsigned)s_gpio[index];
        }
    }
    if (mask == 0U) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    const gpio_config_t config = {
        .pin_bit_mask = mask,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    /* Set the inactive level before the pins become outputs. */
    for (uint8_t index = 0U; index < GPIO_OUTPUT_COUNT; ++index) {
        if (s_gpio[index] >= 0) {
            (void)gpio_set_level(s_gpio[index], level(false));
        }
    }
    const esp_err_t error = gpio_config(&config);
    s_initialized = error == ESP_OK;
    return error;
}

esp_err_t gpio_output_set(uint8_t index, bool active)
{
    if (!s_initialized || !gpio_output_is_configured(index)) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    return gpio_set_level(s_gpio[index], level(active));
}

void gpio_output_all_off(void)
{
    for (uint8_t index = 0U; index < GPIO_OUTPUT_COUNT; ++index) {
        (void)gpio_output_set(index, false);
    }
}
