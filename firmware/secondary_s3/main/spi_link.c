#include "spi_link.h"
#include "driver/spi_slave.h"
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
esp_err_t spi_link_init(void){const spi_bus_config_t b={.sclk_io_num=CONFIG_SECONDARY_S3_SPI_SCLK_GPIO,.mosi_io_num=CONFIG_SECONDARY_S3_SPI_MOSI_GPIO,.miso_io_num=CONFIG_SECONDARY_S3_SPI_MISO_GPIO,.quadwp_io_num=-1,.quadhd_io_num=-1,.max_transfer_sz=72};const spi_slave_interface_config_t i={.spics_io_num=CONFIG_SECONDARY_S3_SPI_CS_GPIO,.mode=0,.queue_size=4};esp_err_t e=spi_slave_initialize(SPI2_HOST,&b,&i,SPI_DMA_CH_AUTO);return e;}
void spi_link_task(void*a){(void)a;for(;;)vTaskDelay(portMAX_DELAY);}
