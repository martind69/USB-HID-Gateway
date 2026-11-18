#pragma once
#include <stdint.h>
#include "analog_vector_io.h"
typedef struct { uint16_t dac_code; uint16_t adc_code; int32_t error_code; } analog_test_point_t;
typedef struct { analog_test_point_t point[5]; } analog_channel_test_t;
esp_err_t analog_self_test_channel(analog_vector_io_t *io,analog_hid_channel_t channel,analog_channel_test_t *result,uint32_t settle_ms);
