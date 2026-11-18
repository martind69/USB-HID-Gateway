#include "analog_control_gpio.h"
#include "driver/gpio.h"
#include "sdkconfig.h"
static int level(bool on,bool active_low){return on != active_low ? 1 : 0;}
esp_err_t analog_control_gpio_init(void)
{
    const gpio_config_t config = {
        .pin_bit_mask = 1ULL << CONFIG_USB_GATEWAY_ANALOG_POWER_GPIO,
        .mode = CONFIG_USB_GATEWAY_ANALOG_POWER_OPEN_DRAIN ? GPIO_MODE_OUTPUT_OD : GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t error = gpio_config(&config);
    if (error != ESP_OK) return error;
    /* POR/default: off = low for active-high push-pull. */
    return analog_sensor_power_set(false);
}
esp_err_t analog_sensor_power_set(bool enabled)
{
    return gpio_set_level(CONFIG_USB_GATEWAY_ANALOG_POWER_GPIO,
        level(enabled, CONFIG_USB_GATEWAY_ANALOG_POWER_ACTIVE_LOW));
}
