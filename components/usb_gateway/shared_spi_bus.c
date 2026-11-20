#include "shared_spi_bus.h"
#include "p4_s3_protocol.h"

#include "ssd1306_display.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sdkconfig.h"

static SemaphoreHandle_t s_lock;
static bool s_s3_bus_initialized;
static bool s_oled_bus_initialized;

static esp_err_t ensure_lock(void)
{
    if (s_lock == NULL) {
        s_lock = xSemaphoreCreateMutex();
        if (s_lock == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    return ESP_OK;
}

static esp_err_t initialize_s3_bus(void)
{
    if (s_s3_bus_initialized) {
        return ESP_OK;
    }

    const spi_bus_config_t config = {
        .mosi_io_num = CONFIG_USB_GATEWAY_S3_SPI_MOSI_GPIO,
        .miso_io_num = CONFIG_USB_GATEWAY_S3_SPI_MISO_GPIO,
        .sclk_io_num = CONFIG_USB_GATEWAY_S3_SPI_SCLK_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = P4_S3_FRAME_SIZE,
    };

    esp_err_t error = spi_bus_initialize(
        SPI2_HOST,
        &config,
        SPI_DMA_CH_AUTO);
    if (error == ESP_OK) {
        s_s3_bus_initialized = true;
    }

    return error;
}

static esp_err_t initialize_oled_bus(void)
{
    if (s_oled_bus_initialized) {
        return ESP_OK;
    }

    const spi_bus_config_t config = {
        .mosi_io_num = CONFIG_USB_GATEWAY_SSD1306_MOSI_GPIO,
        .miso_io_num = -1,
        .sclk_io_num = CONFIG_USB_GATEWAY_SSD1306_SCLK_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = SSD1306_BUFFER_SIZE,
    };

    esp_err_t error = spi_bus_initialize(
        SPI3_HOST,
        &config,
        SPI_DMA_CH_AUTO);
    if (error == ESP_OK) {
        s_oled_bus_initialized = true;
    }

    return error;
}

esp_err_t shared_spi_bus_init(void)
{
    esp_err_t error = ensure_lock();
    if (error != ESP_OK) {
        return error;
    }

    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(100U)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    error = initialize_s3_bus();
    if (error == ESP_OK) {
        error = initialize_oled_bus();
    }

    xSemaphoreGive(s_lock);
    return error;
}

esp_err_t shared_spi_bus_add_s3(spi_device_handle_t *device)
{
    if (device == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t error = ensure_lock();
    if (error != ESP_OK) {
        return error;
    }

    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(100U)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    error = initialize_s3_bus();
    if (error == ESP_OK) {
        const spi_device_interface_config_t config = {
            .clock_speed_hz = CONFIG_USB_GATEWAY_S3_SPI_HZ,
            .mode = 0,
            .spics_io_num = CONFIG_USB_GATEWAY_S3_SPI_CS_GPIO,
            .queue_size = 8,
        };

        error = spi_bus_add_device(
            SPI2_HOST,
            &config,
            device);
    }

    xSemaphoreGive(s_lock);
    return error;
}

esp_err_t shared_spi_bus_add_ssd1306(spi_device_handle_t *device)
{
    if (device == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t error = ensure_lock();
    if (error != ESP_OK) {
        return error;
    }

    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(100U)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    error = initialize_oled_bus();
    if (error == ESP_OK) {
        const spi_device_interface_config_t config = {
            .clock_speed_hz = CONFIG_USB_GATEWAY_SSD1306_SPI_HZ,
            .mode = 0,
            .spics_io_num = CONFIG_USB_GATEWAY_SSD1306_CS_GPIO,
            .queue_size = 2,
            .flags = SPI_DEVICE_NO_DUMMY,
        };

        error = spi_bus_add_device(
            SPI3_HOST,
            &config,
            device);
    }

    xSemaphoreGive(s_lock);
    return error;
}
