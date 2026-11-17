#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "usb_gateway.h"
void app_main(void){usb_gateway_init();for(;;){usb_gateway_process();vTaskDelay(pdMS_TO_TICKS(1));}}
