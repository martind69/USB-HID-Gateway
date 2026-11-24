#include "analog_control_gpio.h"

#include "driver/gpio.h"
#include "sdkconfig.h"

#ifndef CONFIG_USB_GATEWAY_JOYSTICK_ENABLE_OPEN_DRAIN
#define CONFIG_USB_GATEWAY_JOYSTICK_ENABLE_OPEN_DRAIN 0
#endif

#ifndef CONFIG_USB_GATEWAY_JOYSTICK_ENABLE_ACTIVE_LOW
#define CONFIG_USB_GATEWAY_JOYSTICK_ENABLE_ACTIVE_LOW 0
#endif

#define JOYSTICK_ENABLE_GPIO    ((gpio_num_t)CONFIG_USB_GATEWAY_JOYSTICK_ENABLE_GPIO)

static bool s_initialized;

static int level_for(bool enabled)
{
    return enabled != (CONFIG_USB_GATEWAY_JOYSTICK_ENABLE_ACTIVE_LOW != 0) ? 1 : 0;
}

esp_err_t analog_control_gpio_init(void)
{
    /* Latch the inactive level before the pad becomes an output. */
    esp_err_t error = gpio_set_level(JOYSTICK_ENABLE_GPIO, level_for(false));
    if (error != ESP_OK) {
        return error;
    }

    const gpio_config_t config = {
        .pin_bit_mask = 1ULL << CONFIG_USB_GATEWAY_JOYSTICK_ENABLE_GPIO,
        .mode = CONFIG_USB_GATEWAY_JOYSTICK_ENABLE_OPEN_DRAIN
            ? GPIO_MODE_INPUT_OUTPUT_OD
            : GPIO_MODE_INPUT_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    error = gpio_config(&config);
    if (error != ESP_OK) {
        return error;
    }

    s_initialized = true;
    return gpio_set_level(JOYSTICK_ENABLE_GPIO, level_for(false));
}

esp_err_t analog_joystick_enable_set(bool enabled)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    return gpio_set_level(JOYSTICK_ENABLE_GPIO, level_for(enabled));
}

bool analog_joystick_enable_readback(void)
{
    const int level = gpio_get_level(JOYSTICK_ENABLE_GPIO);
    return (level != 0) != (CONFIG_USB_GATEWAY_JOYSTICK_ENABLE_ACTIVE_LOW != 0);
}
