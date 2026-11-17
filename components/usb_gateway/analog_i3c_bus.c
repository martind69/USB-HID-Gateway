#include "analog_i3c_bus.h"

#include "sdkconfig.h"

static i3c_master_bus_handle_t s_bus;

esp_err_t analog_i3c_bus_init(void)
{
    if (s_bus != NULL) {
        return ESP_OK;
    }

    const i3c_master_bus_config_t config = {
        .sda_io_num = CONFIG_USB_GATEWAY_ANALOG_I3C_SDA,
        .scl_io_num = CONFIG_USB_GATEWAY_ANALOG_I3C_SCL,
        .i3c_scl_freq_hz_od = CONFIG_USB_GATEWAY_ANALOG_I3C_FREQ_HZ,
        .i3c_scl_freq_hz_pp = CONFIG_USB_GATEWAY_ANALOG_I3C_FREQ_HZ,
        .i3c_scl_od_duty_cycle = 0.5f,
        .i3c_sda_od_hold_time_ns = 25,
        .i3c_sda_pp_hold_time_ns = 0,
        .entdaa_device_num = 0,
    };

    return i3c_new_master_bus(&config, &s_bus);
}

esp_err_t analog_i3c_add_legacy_i2c(
    uint8_t address,
    i3c_master_i2c_device_handle_t *device)
{
    if (device == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t error = analog_i3c_bus_init();
    if (error != ESP_OK) {
        return error;
    }

    const i3c_device_i2c_config_t config = {
        .device_address = address,
        .scl_freq_hz = CONFIG_USB_GATEWAY_ANALOG_I3C_FREQ_HZ,
    };

    return i3c_master_bus_add_i2c_device(s_bus, &config, device);
}
