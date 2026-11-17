#include "ssd1306_display.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "sdkconfig.h"
static spi_device_handle_t dev;static uint8_t fb[1024];esp_err_t ssd1306_display_init(void){spi_bus_config_t b={.mosi_io_num=CONFIG_USB_GATEWAY_SSD1306_MOSI_GPIO,.miso_io_num=-1,.sclk_io_num=CONFIG_USB_GATEWAY_SSD1306_SCLK_GPIO,.max_transfer_sz=1024};esp_err_t e=spi_bus_initialize(CONFIG_USB_GATEWAY_SSD1306_SPI_HOST,&b,SPI_DMA_CH_AUTO);if(e&&e!=ESP_ERR_INVALID_STATE)return e;spi_device_interface_config_t d={.clock_speed_hz=CONFIG_USB_GATEWAY_SSD1306_SPI_HZ,.mode=0,.spics_io_num=CONFIG_USB_GATEWAY_SSD1306_CS_GPIO,.queue_size=2};return spi_bus_add_device(CONFIG_USB_GATEWAY_SSD1306_SPI_HOST,&d,&dev);}void ssd1306_display_set_view(const ssd1306_status_view_t*v){(void)v;/* TODO(SSD1306-FONT): render view into fb; transport initialized. */(void)fb;}
