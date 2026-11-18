#include "ssd1306_display.h"
#include "shared_spi_bus.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include <string.h>

#if CONFIG_USB_GATEWAY_SSD1306_ENABLE
static spi_device_handle_t s_device;
static SemaphoreHandle_t s_lock;
static uint8_t s_framebuffer[SSD1306_BUFFER_SIZE];
static ssd1306_status_view_t s_view;
static bool s_dirty = true;

static esp_err_t transmit(bool data, const uint8_t *bytes, size_t length)
{
    gpio_set_level(CONFIG_USB_GATEWAY_SSD1306_DC_GPIO, data ? 1 : 0);
    spi_transaction_t transaction = {.length = length * 8U, .tx_buffer = bytes};
    return spi_device_transmit(s_device, &transaction);
}

static esp_err_t initialize_controller(void)
{
    gpio_set_level(CONFIG_USB_GATEWAY_SSD1306_RESET_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(CONFIG_USB_GATEWAY_SSD1306_RESET_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    const uint8_t commands[] = {
        0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40,
        0x8D, 0x14, 0x20, 0x00, 0xA1, 0xC8, 0xDA, 0x12,
        0x81, CONFIG_USB_GATEWAY_SSD1306_CONTRAST, 0xD9, 0xF1,
        0xDB, 0x40, 0xA4, 0xA6, 0x2E, 0xAF,
    };
    return transmit(false, commands, sizeof(commands));
}

static void render_status(void)
{
    /* TODO(SSD1306-FONT): Retain the existing 5x7 renderer here.
     * This function deliberately changes only the framebuffer and never blocks. */
    memset(s_framebuffer, 0, sizeof(s_framebuffer));
    (void)s_view;
}

static esp_err_t flush(void)
{
    const uint8_t address[] = {0x21, 0, 127, 0x22, 0, 7};
    esp_err_t error = transmit(false, address, sizeof(address));
    return error == ESP_OK ? transmit(true, s_framebuffer, sizeof(s_framebuffer)) : error;
}

static void display_task(void *argument)
{
    (void)argument;
    for (;;) {
        if (s_dirty && xSemaphoreTake(s_lock, pdMS_TO_TICKS(20)) == pdTRUE) {
            render_status();
            if (flush() == ESP_OK) s_dirty = false;
            xSemaphoreGive(s_lock);
        }
        vTaskDelay(pdMS_TO_TICKS(CONFIG_USB_GATEWAY_SSD1306_REFRESH_MS));
    }
}

esp_err_t ssd1306_display_init(void)
{
    esp_err_t error = shared_spi_bus_add_ssd1306(&s_device);
    if (error != ESP_OK) return error;
    const gpio_config_t io = {
        .pin_bit_mask = (1ULL << CONFIG_USB_GATEWAY_SSD1306_DC_GPIO) |
                        (1ULL << CONFIG_USB_GATEWAY_SSD1306_RESET_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .intr_type = GPIO_INTR_DISABLE,
    };
    error = gpio_config(&io);
    if (error != ESP_OK) return error;
    error = initialize_controller();
    if (error != ESP_OK) return error;
    s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL) return ESP_ERR_NO_MEM;
    return xTaskCreate(display_task, "ssd1306", 4096, NULL, 4, NULL) == pdPASS
        ? ESP_OK : ESP_ERR_NO_MEM;
}

void ssd1306_display_set_view(const ssd1306_status_view_t *view)
{
    if (view == NULL || s_lock == NULL) return;
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(20)) == pdTRUE) {
        if (memcmp(&s_view, view, sizeof(s_view)) != 0) {
            s_view = *view;
            s_dirty = true;
        }
        xSemaphoreGive(s_lock);
    }
}
#else
esp_err_t ssd1306_display_init(void){return ESP_ERR_NOT_SUPPORTED;}
void ssd1306_display_set_view(const ssd1306_status_view_t *view){(void)view;}
#endif
