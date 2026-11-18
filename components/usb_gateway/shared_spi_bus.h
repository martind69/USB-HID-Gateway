#pragma once
#include "driver/spi_master.h"
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif
esp_err_t shared_spi_bus_init(void);
esp_err_t shared_spi_bus_add_s3(spi_device_handle_t *device);
esp_err_t shared_spi_bus_add_ssd1306(spi_device_handle_t *device);
#ifdef __cplusplus
}
#endif
