#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Digital outputs 0..11 (CONFIG_USB_GATEWAY_DIGITAL_OUTPUT_n_GPIO, -1 = unused).
 * Polarity: CONFIG_USB_GATEWAY_DIGITAL_OUTPUT_ACTIVE_LOW. All outputs start inactive.
 */

#define GPIO_OUTPUT_COUNT 12U

/* ESP_ERR_NOT_SUPPORTED if no output is configured. */
esp_err_t gpio_output_init(void);

/* ESP_ERR_NOT_SUPPORTED if the output is not configured. */
esp_err_t gpio_output_set(uint8_t index, bool active);

void gpio_output_all_off(void);

bool gpio_output_is_configured(uint8_t index);

#ifdef __cplusplus
}
#endif
