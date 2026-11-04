#pragma once

#include <stdbool.h>
#include <stdint.h>

void gpio_output_init(void);
void gpio_output_set(uint8_t output, bool state);
