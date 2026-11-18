#include "profile_feedback.h"
#include "driver/gpio.h"
#include "sdkconfig.h"
static int level(bool on, bool active_low){return on != active_low ? 1 : 0;}
static void output(int gpio, bool on, bool active_low){gpio_set_level(gpio, level(on, active_low));}
esp_err_t profile_feedback_init(void)
{
#if CONFIG_USB_GATEWAY_STATUS_LEDS_ENABLE
    const gpio_config_t config = {
        .pin_bit_mask = (1ULL << CONFIG_USB_GATEWAY_STATUS_LED_BUSY_GPIO) |
                        (1ULL << CONFIG_USB_GATEWAY_STATUS_LED_OK_GPIO) |
                        (1ULL << CONFIG_USB_GATEWAY_STATUS_LED_ERROR_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t error = gpio_config(&config);
    if (error != ESP_OK) return error;
#endif
    profile_feedback_set(PROFILE_FEEDBACK_READY);
    return ESP_OK;
}
void profile_feedback_set(profile_feedback_state_t state)
{
#if CONFIG_USB_GATEWAY_STATUS_LEDS_ENABLE
    const bool busy = state == PROFILE_FEEDBACK_LEARNING ||
        state == PROFILE_FEEDBACK_EXPORT_PENDING ||
        state == PROFILE_FEEDBACK_EXPORT_WRITING;
    const bool ok = state == PROFILE_FEEDBACK_EXPORT_SUCCESS;
    const bool error = state == PROFILE_FEEDBACK_EXPORT_ERROR ||
        state == PROFILE_FEEDBACK_ANALOG_ERROR;
    output(CONFIG_USB_GATEWAY_STATUS_LED_BUSY_GPIO, busy, CONFIG_USB_GATEWAY_STATUS_LED_BUSY_ACTIVE_LOW);
    output(CONFIG_USB_GATEWAY_STATUS_LED_OK_GPIO, ok, CONFIG_USB_GATEWAY_STATUS_LED_OK_ACTIVE_LOW);
    output(CONFIG_USB_GATEWAY_STATUS_LED_ERROR_GPIO, error, CONFIG_USB_GATEWAY_STATUS_LED_ERROR_ACTIVE_LOW);
#else
    (void)state;
#endif
}
