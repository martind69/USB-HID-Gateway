#include "analog_i3c_bus.h"
#include "sdkconfig.h"
static i3c_master_bus_handle_t s_bus;
esp_err_t analog_i3c_bus_init(void){if(s_bus)return ESP_OK;const i3c_master_bus_config_t c={.clock_source=I3C_MASTER_CLK_SRC_XTAL,.sda_io_num=CONFIG_USB_GATEWAY_ANALOG_I3C_SDA,.scl_io_num=CONFIG_USB_GATEWAY_ANALOG_I3C_SCL,.i3c_scl_freq_hz_od=CONFIG_USB_GATEWAY_ANALOG_I3C_FREQ_HZ,.i3c_scl_freq_hz_pp=CONFIG_USB_GATEWAY_ANALOG_I3C_FREQ_HZ,.i3c_scl_od_duty_cycle=.5f,.i3c_sda_od_hold_time_ns=25,.i3c_sda_pp_hold_time_ns=0,.entdaa_device_num=0};return i3c_new_master_bus(&c,&s_bus);}
esp_err_t analog_i3c_add_legacy_i2c(uint8_t a,i3c_master_i2c_device_handle_t*d){if(!d)return ESP_ERR_INVALID_ARG;esp_err_t e=analog_i3c_bus_init();if(e!=ESP_OK)return e;const i3c_device_i2c_config_t c={.device_address=a,.scl_freq_hz=CONFIG_USB_GATEWAY_ANALOG_I3C_FREQ_HZ};return i3c_master_bus_add_i2c_device(s_bus,&c,d);}
