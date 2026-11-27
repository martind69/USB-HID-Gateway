#include "analog_control_gpio.h"

#include "driver/gpio.h"
#include "sdkconfig.h"

#ifndef CONFIG_USB_GATEWAY_GENERATE_LED_GPIO
#define CONFIG_USB_GATEWAY_GENERATE_LED_GPIO -1
#endif

#ifndef CONFIG_USB_GATEWAY_GENERATE_LED_ACTIVE_LOW
#define CONFIG_USB_GATEWAY_GENERATE_LED_ACTIVE_LOW 0
#endif

#if CONFIG_USB_GATEWAY_GENERATE_LED_GPIO >= 0
#define GENERATE_LED_GPIO ((gpio_num_t)CONFIG_USB_GATEWAY_GENERATE_LED_GPIO)

static bool s_initialized;

static uint32_t level_for(bool on)
{
    return (on != (CONFIG_USB_GATEWAY_GENERATE_LED_ACTIVE_LOW != 0)) ? 1U : 0U;
}
#endif

esp_err_t analog_control_gpio_init(void)
{
#if CONFIG_USB_GATEWAY_GENERATE_LED_GPIO < 0
    return ESP_OK;
#else
    /* Latch "off" before the pad becomes an output. */
    esp_err_t error = gpio_set_level(GENERATE_LED_GPIO, level_for(false));
    if (error != ESP_OK) {
        return error;
    }

    const gpio_config_t config = {
        .pin_bit_mask = 1ULL << CONFIG_USB_GATEWAY_GENERATE_LED_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    error = gpio_config(&config);
    if (error != ESP_OK) {
        return error;
    }

    s_initialized = true;
    return gpio_set_level(GENERATE_LED_GPIO, level_for(false));
#endif
}

void analog_generate_led_set(bool on)
{
#if CONFIG_USB_GATEWAY_GENERATE_LED_GPIO >= 0
    if (s_initialized) {
        (void)gpio_set_level(GENERATE_LED_GPIO, level_for(on));
    }
#else
    (void)on;
#endif
}
