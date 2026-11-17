#include "hotkey.h"
static usb_gateway_mode_t mode;void hotkey_init(void){mode=USB_GATEWAY_MODE_PASSTHROUGH;}void hotkey_process(void){}void hotkey_set_mode(usb_gateway_mode_t m){mode=m;}usb_gateway_mode_t hotkey_get_mode(void){return mode;}
