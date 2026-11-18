#include "shared_spi_bus.h"
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static bool s_initialized;
static SemaphoreHandle_t s_init_lock;

esp_err_t shared_spi_bus_init(void)
{
    if (s_initialized) return ESP_OK;
    if (s_init_lock == NULL) {
        s_init_lock = xSemaphoreCreateMutex();
        if (s_init_lock == NULL) return ESP_ERR_NO_MEM;
    }
    if (xSemaphoreTake(s_init_lock, pdMS_TO_TICKS(100)) != pdTRUE) return ESP_ERR_TIMEOUT;
    if (!s_initialized) {
        const spi_bus_config_t config = {
            .mosi_io_num = CONFIG_USB_GATEWAY_SHARED_SPI_MOSI_GPIO,
            .miso_io_num = CONFIG_USB_GATEWAY_SHARED_SPI_MISO_GPIO,
            .sclk_io_num = CONFIG_USB_GATEWAY_SHARED_SPI_SCLK_GPIO,
            .quadwp_io_num = -1,
            .quadhd_io_num = -1,
            .max_transfer_sz = 1024,
        };
        esp_err_t error = spi_bus_initialize(SPI2_HOST, &config, SPI_DMA_CH_AUTO);
        if (error != ESP_OK && error != ESP_ERR_INVALID_STATE) {
            xSemaphoreGive(s_init_lock);
            return error;
        }
        s_initialized = true;
    }
    xSemaphoreGive(s_init_lock);
    return ESP_OK;
}

static esp_err_t add_device(const spi_device_interface_config_t *config, spi_device_handle_t *device)
{
    if (device == NULL) return ESP_ERR_INVALID_ARG;
    esp_err_t error = shared_spi_bus_init();
    return error == ESP_OK ? spi_bus_add_device(SPI2_HOST, config, device) : error;
}

esp_err_t shared_spi_bus_add_s3(spi_device_handle_t *device)
{
    const spi_device_interface_config_t config = {
        .clock_speed_hz = 10000000,
        .mode = 0,
        .spics_io_num = CONFIG_USB_GATEWAY_S3_SPI_CS_GPIO,
        .queue_size = 8,
        .flags = 0,
    };
    return add_device(&config, device);
}

esp_err_t shared_spi_bus_add_ssd1306(spi_device_handle_t *device)
{
    const spi_device_interface_config_t config = {
        .clock_speed_hz = CONFIG_USB_GATEWAY_SSD1306_SPI_HZ,
        .mode = 0,
        .spics_io_num = CONFIG_USB_GATEWAY_SSD1306_CS_GPIO,
        .queue_size = 2,
        .flags = SPI_DEVICE_NO_DUMMY,
    };
    return add_device(&config, device);
}
