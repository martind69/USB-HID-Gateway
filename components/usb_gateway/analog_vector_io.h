#pragma once
#include "ads7128.h"
#include "dac7578.h"
typedef enum{ANALOG_HID_LX=0,ANALOG_HID_LY,ANALOG_HID_RX,ANALOG_HID_RY,ANALOG_HID_LT,ANALOG_HID_RT,ANALOG_HID_L2,ANALOG_HID_R2,ANALOG_HID_COUNT}analog_hid_channel_t;
typedef struct{ads7128_t adc;dac7578_t dac;uint8_t adc_channel[8],dac_channel[8];}analog_vector_io_t;
esp_err_t analog_vector_io_init(analog_vector_io_t*io);esp_err_t analog_vector_io_read_channel(analog_vector_io_t*io,analog_hid_channel_t channel,uint16_t*code);esp_err_t analog_vector_io_write_channel(analog_vector_io_t*io,analog_hid_channel_t channel,uint16_t code);
