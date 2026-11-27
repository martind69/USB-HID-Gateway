#include "ssd1306_display.h"

#include "ssd1306_status_renderer.h"
#include "shared_spi_bus.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include <string.h>

#ifndef CONFIG_USB_GATEWAY_SSD1306_ENABLE
#define CONFIG_USB_GATEWAY_SSD1306_ENABLE 0
#endif

static const char *TAG = "ssd1306";

#if CONFIG_USB_GATEWAY_SSD1306_ENABLE

static spi_device_handle_t s_device;
static SemaphoreHandle_t s_lock;
static uint8_t s_framebuffer[SSD1306_BUFFER_SIZE];
static ssd1306_status_view_t s_view;
static bool s_dirty = true;
static bool s_first_flush_logged;

static esp_err_t transmit(
    bool data,
    const uint8_t *bytes,
    size_t length)
{
    if (bytes == NULL || length == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t error = gpio_set_level(
        CONFIG_USB_GATEWAY_SSD1306_DC_GPIO,
        data ? 1 : 0);
    if (error != ESP_OK) {
        return error;
    }

    spi_transaction_t transaction = {
        .length = length * 8U,
        .tx_buffer = bytes,
    };

    return spi_device_transmit(s_device, &transaction);
}

static esp_err_t initialize_controller(void)
{
    esp_err_t error = gpio_set_level(
        CONFIG_USB_GATEWAY_SSD1306_RESET_GPIO,
        0);
    if (error != ESP_OK) {
        return error;
    }

    vTaskDelay(pdMS_TO_TICKS(10U));

    error = gpio_set_level(
        CONFIG_USB_GATEWAY_SSD1306_RESET_GPIO,
        1);
    if (error != ESP_OK) {
        return error;
    }

    vTaskDelay(pdMS_TO_TICKS(10U));

    const uint8_t commands[] = {
        0xAE,
        0xD5, 0x80,
        0xA8, 0x3F,
        0xD3, 0x00,
        0x40,
        0x8D, 0x14,
        0x20, 0x00,
        0xA1,
        0xC8,
        0xDA, 0x12,
        0x81, CONFIG_USB_GATEWAY_SSD1306_CONTRAST,
        0xD9, 0xF1,
        0xDB, 0x40,
        0xA4,
        0xA6,
        0x2E,
        0xAF,
    };

    return transmit(false, commands, sizeof(commands));
}

static esp_err_t flush_framebuffer(void)
{
    const uint8_t address_commands[] = {
        0x21, 0x00, 0x7F,
        0x22, 0x00, 0x07,
    };

    esp_err_t error = transmit(
        false,
        address_commands,
        sizeof(address_commands));
    if (error != ESP_OK) {
        return error;
    }

    return transmit(
        true,
        s_framebuffer,
        sizeof(s_framebuffer));
}

static void display_task(void *argument)
{
    (void)argument;

    for (;;) {
        if (s_dirty &&
            xSemaphoreTake(
                s_lock,
                pdMS_TO_TICKS(20U)) == pdTRUE) {
            ssd1306_render_status(
                s_framebuffer,
                &s_view);

            const esp_err_t error = flush_framebuffer();
            if (error == ESP_OK) {
                s_dirty = false;
                if (!s_first_flush_logged) {
                    s_first_flush_logged = true;
                    ESP_LOGI(TAG, "First framebuffer transmitted");
                }
            } else {
                ESP_LOGE(
                    TAG,
                    "Framebuffer transfer failed: %s",
                    esp_err_to_name(error));
            }

            xSemaphoreGive(s_lock);
        }

        vTaskDelay(pdMS_TO_TICKS(
            CONFIG_USB_GATEWAY_SSD1306_REFRESH_MS));
    }
}

esp_err_t ssd1306_display_init(void)
{
    ESP_LOGI(
        TAG,
        "Initialize SPI OLED: SCLK=%d MOSI=%d CS=%d DC=%d RESET=%d frequency=%d Hz",
        CONFIG_USB_GATEWAY_SSD1306_SCLK_GPIO,
        CONFIG_USB_GATEWAY_SSD1306_MOSI_GPIO,
        CONFIG_USB_GATEWAY_SSD1306_CS_GPIO,
        CONFIG_USB_GATEWAY_SSD1306_DC_GPIO,
        CONFIG_USB_GATEWAY_SSD1306_RESET_GPIO,
        CONFIG_USB_GATEWAY_SSD1306_SPI_HZ);

    esp_err_t error = shared_spi_bus_add_ssd1306(&s_device);
    if (error != ESP_OK) {
        ESP_LOGE(
            TAG,
            "SPI device registration failed: %s",
            esp_err_to_name(error));
        return error;
    }

    const gpio_config_t io_config = {
        .pin_bit_mask =
            (1ULL << CONFIG_USB_GATEWAY_SSD1306_DC_GPIO) |
            (1ULL << CONFIG_USB_GATEWAY_SSD1306_RESET_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    error = gpio_config(&io_config);
    if (error != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Control GPIO configuration failed: %s",
            esp_err_to_name(error));
        return error;
    }

    error = initialize_controller();
    if (error != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Controller initialization failed: %s",
            esp_err_to_name(error));
        return error;
    }

    ESP_LOGI(TAG, "SSD1306 controller initialized");

    s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }

    return xTaskCreate(
        display_task,
        "ssd1306",
        4096U,
        NULL,
        4U,
        NULL) == pdPASS
        ? ESP_OK
        : ESP_ERR_NO_MEM;
}

void ssd1306_display_set_view(
    const ssd1306_status_view_t *view)
{
    if (view == NULL || s_lock == NULL) {
        return;
    }

    if (xSemaphoreTake(
            s_lock,
            pdMS_TO_TICKS(20U)) == pdTRUE) {
        if (memcmp(&s_view, view, sizeof(s_view)) != 0) {
            s_view = *view;
            s_dirty = true;
        }
        xSemaphoreGive(s_lock);
    }
}

#else

esp_err_t ssd1306_display_init(void)
{
    ESP_LOGW(
        TAG,
        "SSD1306 support disabled: CONFIG_USB_GATEWAY_SSD1306_ENABLE is not set");
    return ESP_ERR_NOT_SUPPORTED;
}

void ssd1306_display_set_view(
    const ssd1306_status_view_t *view)
{
    (void)view;
}

#endif
