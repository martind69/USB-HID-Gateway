#include "analog_self_test.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
esp_err_t analog_self_test_channel(analog_vector_io_t*io,analog_hid_channel_t ch,analog_channel_test_t*r,uint32_t settle){if(!io||!r||ch>=ANALOG_HID_COUNT)return ESP_ERR_INVALID_ARG;static const uint16_t codes[5]={0,1024,2048,3072,4095};for(int i=0;i<5;i++){esp_err_t e=analog_vector_io_write_channel(io,ch,codes[i]);if(e!=ESP_OK)return e;vTaskDelay(pdMS_TO_TICKS(settle));uint16_t adc=0;e=analog_vector_io_read_channel(io,ch,&adc);if(e!=ESP_OK)return e;r->point[i]=(analog_test_point_t){.dac_code=codes[i],.adc_code=adc,.error_code=(int32_t)adc-(int32_t)codes[i]};}return ESP_OK;}
