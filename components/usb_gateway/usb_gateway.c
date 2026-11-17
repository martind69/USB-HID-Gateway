#include "usb_gateway.h"
#include "diagnostics.h"
#include "hid_proxy.h"
#include "hotkey.h"
#include "profile_feedback.h"
#include "ssd1306_display.h"
#include "usb_device.h"
#include "usb_host.h"
void usb_gateway_init(void){diagnostics_init();hotkey_init();hid_proxy_init();profile_feedback_init();ssd1306_display_init();usb_device_init();usb_host_init();}void usb_gateway_process(void){usb_host_process();usb_device_process();hotkey_process();hid_proxy_process();}void usb_gateway_set_mode(usb_gateway_mode_t m){hotkey_set_mode(m);}usb_gateway_mode_t usb_gateway_get_mode(void){return hotkey_get_mode();}
