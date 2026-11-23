#include "analog_bus.h"

#include <stddef.h>
#include <stdio.h>

#include "esp_log.h"
#include "sdkconfig.h"

#define BUS_PROBE_TIMEOUT_MS    10
#define BUS_GLITCH_FILTER       7
#define SCAN_FIRST_ADDRESS      0x08U
#define SCAN_LAST_ADDRESS       0x77U
#define SCAN_LINE_CAPACITY      ((SCAN_LAST_ADDRESS - SCAN_FIRST_ADDRESS + 1U) * 5U + 1U)

static const char *TAG = "analog_bus";

static i2c_master_bus_handle_t s_bus;

esp_err_t analog_bus_init(void)
{
    if (s_bus != NULL) {
        return ESP_OK;
    }
    const i2c_master_bus_config_t config = {
        .i2c_port = -1,
        .sda_io_num = CONFIG_USB_GATEWAY_ANALOG_I2C_SDA,
        .scl_io_num = CONFIG_USB_GATEWAY_ANALOG_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = BUS_GLITCH_FILTER,
        .flags = {
            .enable_internal_pullup = false,
        },
    };
    const esp_err_t error = i2c_new_master_bus(&config, &s_bus);
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "Bus SDA=%d SCL=%d not created: %s",
            CONFIG_USB_GATEWAY_ANALOG_I2C_SDA, CONFIG_USB_GATEWAY_ANALOG_I2C_SCL,
            esp_err_to_name(error));
        s_bus = NULL;
        return error;
    }
    ESP_LOGI(TAG, "Bus ready: SDA=%d SCL=%d %d Hz",
        CONFIG_USB_GATEWAY_ANALOG_I2C_SDA, CONFIG_USB_GATEWAY_ANALOG_I2C_SCL,
        CONFIG_USB_GATEWAY_ANALOG_I2C_FREQ_HZ);
    return ESP_OK;
}

esp_err_t analog_bus_probe(uint8_t address)
{
    const esp_err_t error = analog_bus_init();
    if (error != ESP_OK) {
        return error;
    }
    return i2c_master_probe(s_bus, address, BUS_PROBE_TIMEOUT_MS);
}

esp_err_t analog_bus_add_device(uint8_t address, i2c_master_dev_handle_t *device)
{
    if (device == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    /* ESP_ERR_NOT_FOUND (NACK): device absent, unpowered or wrong address. */
    esp_err_t error = analog_bus_probe(address);
    if (error != ESP_OK) {
        return error;
    }
    const i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address,
        .scl_speed_hz = CONFIG_USB_GATEWAY_ANALOG_I2C_FREQ_HZ,
    };
    return i2c_master_bus_add_device(s_bus, &config, device);
}

void analog_bus_scan(void)
{
    if (analog_bus_init() != ESP_OK) {
        return;
    }
    static char line[SCAN_LINE_CAPACITY];
    size_t used = 0U;
    unsigned count = 0U;
    line[0] = '\0';
    for (uint8_t address = SCAN_FIRST_ADDRESS; address <= SCAN_LAST_ADDRESS; ++address) {
        if (i2c_master_probe(s_bus, address, BUS_PROBE_TIMEOUT_MS) == ESP_OK) {
            used += (size_t)snprintf(&line[used], sizeof(line) - used, " 0x%02X", address);
            ++count;
        }
    }
    if (count == 0U) {
        ESP_LOGE(TAG, "Scan: no device acknowledged (check supply, pull-ups, wiring)");
    } else {
        ESP_LOGI(TAG, "Scan: %u device(s):%s", count, line);
    }
}
