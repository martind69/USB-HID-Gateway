#pragma once

#include <stdint.h>

void dac_output_init(void);
void dac_output_set_channel(uint8_t channel, uint16_t value);
