#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * GENERATE indicator LED (formerly analog power / joystick enable).
 * LED on = analog engine in GENERATE (DAC drives the joystick nodes).
 *
 * The joystick supply is not switched by the firmware: the Hall outputs are
 * high-impedance (about 16 kOhm), the DAC7578 overrides them completely.
 *
 * Pin and polarity: CONFIG_USB_GATEWAY_GENERATE_LED_GPIO (-1 = none) and
 * CONFIG_USB_GATEWAY_GENERATE_LED_ACTIVE_LOW.
 */
esp_err_t analog_control_gpio_init(void);

void analog_generate_led_set(bool on);

#ifdef __cplusplus
}
#endif
