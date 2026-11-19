#include "ssd1306_display.h"
#include "ssd1306_status_renderer.h"
#include "shared_spi_bus.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include <string.h>
#if CONFIG_USB_GATEWAY_SSD1306_ENABLE
static spi_device_handle_t s_device;static SemaphoreHandle_t s_lock;static uint8_t s_framebuffer[SSD1306_BUFFER_SIZE];static ssd1306_status_view_t s_view;static bool s_dirty=true;
static esp_err_t transmit(bool data,const uint8_t*bytes,size_t length){if(!bytes||length==0)return ESP_ERR_INVALID_ARG;(void)gpio_set_level(CONFIG_USB_GATEWAY_SSD1306_DC_GPIO,data?1:0);spi_transaction_t t={.length=length*8U,.tx_buffer=bytes};return spi_device_transmit(s_device,&t);}
static esp_err_t initialize_controller(void){(void)gpio_set_level(CONFIG_USB_GATEWAY_SSD1306_RESET_GPIO,0);vTaskDelay(pdMS_TO_TICKS(10));(void)gpio_set_level(CONFIG_USB_GATEWAY_SSD1306_RESET_GPIO,1);vTaskDelay(pdMS_TO_TICKS(10));const uint8_t c[]={0xAE,0xD5,0x80,0xA8,0x3F,0xD3,0x00,0x40,0x8D,0x14,0x20,0x00,0xA1,0xC8,0xDA,0x12,0x81,CONFIG_USB_GATEWAY_SSD1306_CONTRAST,0xD9,0xF1,0xDB,0x40,0xA4,0xA6,0x2E,0xAF};return transmit(false,c,sizeof c);}
static esp_err_t flush(void){const uint8_t a[]={0x21,0,127,0x22,0,7};esp_err_t e=transmit(false,a,sizeof a);return e==ESP_OK?transmit(true,s_framebuffer,sizeof s_framebuffer):e;}
static void display_task(void*a){(void)a;for(;;){if(s_dirty&&xSemaphoreTake(s_lock,pdMS_TO_TICKS(20))==pdTRUE){ssd1306_render_status(s_framebuffer,&s_view);if(flush()==ESP_OK)s_dirty=false;xSemaphoreGive(s_lock);}vTaskDelay(pdMS_TO_TICKS(CONFIG_USB_GATEWAY_SSD1306_REFRESH_MS));}}
esp_err_t ssd1306_display_init(void){esp_err_t e=shared_spi_bus_add_ssd1306(&s_device);if(e!=ESP_OK)return e;const gpio_config_t io={.pin_bit_mask=(1ULL<<CONFIG_USB_GATEWAY_SSD1306_DC_GPIO)|(1ULL<<CONFIG_USB_GATEWAY_SSD1306_RESET_GPIO),.mode=GPIO_MODE_OUTPUT,.pull_up_en=GPIO_PULLUP_DISABLE,.pull_down_en=GPIO_PULLDOWN_DISABLE,.intr_type=GPIO_INTR_DISABLE};e=gpio_config(&io);if(e!=ESP_OK)return e;e=initialize_controller();if(e!=ESP_OK)return e;s_lock=xSemaphoreCreateMutex();if(!s_lock)return ESP_ERR_NO_MEM;return xTaskCreate(display_task,"ssd1306",4096,NULL,4,NULL)==pdPASS?ESP_OK:ESP_ERR_NO_MEM;}
void ssd1306_display_set_view(const ssd1306_status_view_t*v){if(!v||!s_lock)return;if(xSemaphoreTake(s_lock,pdMS_TO_TICKS(20))==pdTRUE){if(memcmp(&s_view,v,sizeof s_view)!=0){s_view=*v;s_dirty=true;}xSemaphoreGive(s_lock);}}
#else
esp_err_t ssd1306_display_init(void){return ESP_ERR_NOT_SUPPORTED;}void ssd1306_display_set_view(const ssd1306_status_view_t*v){(void)v;}
#endif
