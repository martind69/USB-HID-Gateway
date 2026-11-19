#include "usb_gateway.h"
#include "analog_control_gpio.h"
#include "analog_vector_io.h"
#include "diagnostics.h"
#include "gateway_self_test.h"
#include "gpio_output.h"
#include "hid_proxy.h"
#include "hotkey.h"
#include "mapping.h"
#include "motion_profile_cache.h"
#include "p4_s3_link.h"
#include "profile_export.h"
#include "profile_feedback.h"
#include "sd_storage.h"
#include "ssd1306_display.h"
#include "usb_device.h"
#include "usb_host.h"
#include "usb_proxy_control.h"
#include "usb_proxy_transport.h"
#include "verbose_capture.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include <string.h>
#include <stdio.h>
static const char*TAG="usb_gateway";static analog_vector_io_t s_analog;static motion_profile_t s_profile;static bool s_profile_valid;
static void update_display(const char*detail,bool analog_ok){ssd1306_status_view_t v={0};(void)snprintf(v.mode,sizeof v.mode,"%s",hotkey_get_mode()==USB_GATEWAY_MODE_PROXY?"PROXY":"PASS");(void)snprintf(v.profile,sizeof v.profile,"%s",s_profile_valid?s_profile.name:"NO PROFILE");(void)snprintf(v.detail,sizeof v.detail,"%s",detail?detail:"");v.usb_host_ok=true;v.usb_device_ok=true;v.analog_ok=analog_ok;ssd1306_display_set_view(&v);}
void usb_gateway_init(void){diagnostics_init();(void)profile_feedback_init();hotkey_init();gateway_self_test_init();usb_proxy_control_init();(void)usb_proxy_transport_init();esp_err_t e=sd_storage_mount();if(e!=ESP_OK)ESP_LOGW(TAG,"SD unavailable: %s",esp_err_to_name(e));e=verbose_capture_init();if(e!=ESP_OK)ESP_LOGW(TAG,"Capture init: %s",esp_err_to_name(e));
#if CONFIG_USB_GATEWAY_VERBOSE_CAPTURE_ENABLE && CONFIG_USB_GATEWAY_VERBOSE_CAPTURE_BOOT_ENABLE
if(sd_storage_is_mounted())(void)verbose_capture_start(CONFIG_USB_GATEWAY_VERBOSE_CAPTURE_DURATION_S);
#endif
if(analog_control_gpio_init()!=ESP_OK)ESP_LOGE(TAG,"Analog power GPIO init failed");(void)gpio_output_init();e=analog_vector_io_init(&s_analog);bool analog_ok=e==ESP_OK||e==ESP_ERR_NOT_SUPPORTED;if(!analog_ok){ESP_LOGE(TAG,"Analog init: %s",esp_err_to_name(e));profile_feedback_set(PROFILE_FEEDBACK_ANALOG_ERROR);}if(sd_storage_is_mounted()){bool rebuilt=false;e=motion_profile_boot_load("/sdcard/motion/profile-01.json","/sdcard/motion/profile-01.bin",&s_profile,&rebuilt);s_profile_valid=e==ESP_OK;if(s_profile_valid)(void)profile_export_init(&s_profile,"/sdcard/motion/profile-01.json");else ESP_LOGW(TAG,"Profile unavailable: %s",esp_err_to_name(e));}
const mapping_config_t config={0};if(mapping_set_config(&config)!=MAPPING_OK){diagnostics_set_proxy_state(DIAG_STATE_ERROR,ESP_ERR_INVALID_ARG);return;}mapping_set_event_callback(NULL,NULL);hid_proxy_init();e=p4_s3_link_init();if(e==ESP_OK)p4_s3_link_start();else ESP_LOGE(TAG,"P4/S3 link: %s",esp_err_to_name(e));e=ssd1306_display_init();if(e!=ESP_OK&&e!=ESP_ERR_NOT_SUPPORTED)ESP_LOGW(TAG,"OLED: %s",esp_err_to_name(e));usb_device_init();
#if CONFIG_USB_GATEWAY_USB_DEVICE_MOUSE_TEST
ESP_LOGW(TAG,"USB Host disabled for isolated mouse test");
#else
usb_host_init();
#endif
diagnostics_set_proxy_state(DIAG_STATE_READY,0);update_display("READY",analog_ok);}
void usb_gateway_process(void){
#if !CONFIG_USB_GATEWAY_USB_DEVICE_MOUSE_TEST
usb_host_process();
#endif
usb_device_process();hotkey_process();hid_proxy_process();diagnostics_process(hotkey_get_mode());}
void usb_gateway_set_mode(usb_gateway_mode_t mode){hotkey_set_mode(mode);}usb_gateway_mode_t usb_gateway_get_mode(void){return hotkey_get_mode();}
