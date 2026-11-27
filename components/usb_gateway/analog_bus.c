#include "analog_bus.h"

#include <stddef.h>
#include <stdio.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "sdkconfig.h"

#define BUS_PROBE_TIMEOUT_MS    10
#define BUS_GLITCH_FILTER       7
#define SCAN_FIRST_ADDRESS      0x08U
#define SCAN_LAST_ADDRESS       0x77U
#define SCAN_LINE_CAPACITY      ((SCAN_LAST_ADDRESS - SCAN_FIRST_ADDRESS + 1U) * 5U + 1U)
#define DAC7578_BROADCAST       0x47U

/* Bus recovery: 9 clocks release any slave stuck in a read (NXP UM10204, 3.1.16). */
#define RECOVERY_CLOCKS         9U
#define RECOVERY_HALF_PERIOD_US 5U      /* 100 kHz, safe for every device */

#define SDA_GPIO    ((gpio_num_t)CONFIG_USB_GATEWAY_ANALOG_I2C_SDA)
#define SCL_GPIO    ((gpio_num_t)CONFIG_USB_GATEWAY_ANALOG_I2C_SCL)

static const char *TAG = "analog_bus";
static i2c_master_bus_handle_t s_bus;

/*
 * A P4 reset during a transfer can leave a slave driving SDA low.
 * Pins as open-drain GPIO (external pull-ups): clock SCL until SDA is
 * released, then generate a STOP condition. Runs before the I2C driver
 * takes the pins.
 */
static void recover_bus(void)
{
    const gpio_config_t config = {
        .pin_bit_mask = (1ULL << CONFIG_USB_GATEWAY_ANALOG_I2C_SDA) |
            (1ULL << CONFIG_USB_GATEWAY_ANALOG_I2C_SCL),
        .mode = GPIO_MODE_INPUT_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    (void)gpio_set_level(SDA_GPIO, 1);
    (void)gpio_set_level(SCL_GPIO, 1);
    if (gpio_config(&config) != ESP_OK) {
        return;
    }
    esp_rom_delay_us(RECOVERY_HALF_PERIOD_US);

    const bool sda_low = gpio_get_level(SDA_GPIO) == 0;
    const bool scl_low = gpio_get_level(SCL_GPIO) == 0;
    if (!sda_low && !scl_low) {
        (void)gpio_reset_pin(SDA_GPIO);
        (void)gpio_reset_pin(SCL_GPIO);
        return;
    }
    if (scl_low) {
        /* SCL held low by a slave (clock stretching) cannot be recovered by the master. */
        ESP_LOGE(TAG, "Bus recovery: SCL held low (GPIO%d), check wiring and devices",
            CONFIG_USB_GATEWAY_ANALOG_I2C_SCL);
    }

    unsigned clocks = 0U;
    for (; clocks < RECOVERY_CLOCKS && gpio_get_level(SDA_GPIO) == 0; ++clocks) {
        (void)gpio_set_level(SCL_GPIO, 0);
        esp_rom_delay_us(RECOVERY_HALF_PERIOD_US);
        (void)gpio_set_level(SCL_GPIO, 1);
        esp_rom_delay_us(RECOVERY_HALF_PERIOD_US);
    }

    /* STOP: SDA low -> high while SCL is high. */
    (void)gpio_set_level(SCL_GPIO, 0);
    esp_rom_delay_us(RECOVERY_HALF_PERIOD_US);
    (void)gpio_set_level(SDA_GPIO, 0);
    esp_rom_delay_us(RECOVERY_HALF_PERIOD_US);
    (void)gpio_set_level(SCL_GPIO, 1);
    esp_rom_delay_us(RECOVERY_HALF_PERIOD_US);
    (void)gpio_set_level(SDA_GPIO, 1);
    esp_rom_delay_us(RECOVERY_HALF_PERIOD_US);

    const bool released = gpio_get_level(SDA_GPIO) != 0;
    if (released) {
        ESP_LOGW(TAG, "Bus recovery: SDA released after %u clock(s), STOP sent", clocks);
    } else {
        ESP_LOGE(TAG, "Bus recovery failed: SDA still low after %u clocks", clocks);
    }
    (void)gpio_reset_pin(SDA_GPIO);
    (void)gpio_reset_pin(SCL_GPIO);
}

esp_err_t analog_bus_init(void)
{
    if (s_bus != NULL) {
        return ESP_OK;
    }

    recover_bus();

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
        if (i2c_master_probe(s_bus, address, BUS_PROBE_TIMEOUT_MS) == ESP_OK &&
            used < sizeof(line)) {
            used += (size_t)snprintf(&line[used], sizeof(line) - used,
                address == DAC7578_BROADCAST ? " 0x%02X(DAC7578 broadcast)" : " 0x%02X",
                address);
            ++count;
        }
    }
    if (count == 0U) {
        ESP_LOGE(TAG, "Scan: no device acknowledged (check supply, pull-ups, wiring)");
    } else {
        ESP_LOGI(TAG, "Scan: %u device(s):%s", count, line);
    }
}
