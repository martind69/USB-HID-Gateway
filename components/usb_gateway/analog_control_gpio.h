#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Joystick enable output (formerly "analog power"): switches the supply of
 * the Hall sensors and buttons of the physical joystick.
 *
 * Pin and polarity: CONFIG_USB_GATEWAY_JOYSTICK_ENABLE_GPIO / _ACTIVE_LOW /
 * _OPEN_DRAIN. The pad is configured as input/output, so the real pad level
 * can be read back. Only analog_interlock may switch this output.
 */
esp_err_t analog_control_gpio_init(void);

esp_err_t analog_joystick_enable_set(bool enabled);

/* Pad level interpreted with the configured polarity (true = supply on). */
bool analog_joystick_enable_readback(void);

#ifdef __cplusplus
}
#endif
