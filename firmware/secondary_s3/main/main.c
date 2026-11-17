#include "spi_link.h"
#include "usb_device.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
void app_main(void){usb_device_init();spi_link_init();xTaskCreate(spi_link_task,"spi_link",4096,NULL,18,NULL);}
